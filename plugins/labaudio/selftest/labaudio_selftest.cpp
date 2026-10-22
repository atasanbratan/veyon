/*
 * labaudio_selftest.cpp - checks protocol framing, Opus round trip, jitter buffer and mixing
 * without Veyon or audio hardware.
 *
 * Copyright (c) 2026 TDMUGI
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QCoreApplication>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QtMath>
#include <cstdio>

#include <opus/opus.h>

#include "../LabAudioEngine.h"
#include "../LabAudioProtocol.h"

using namespace LabAudioProtocol;

static int failures = 0;

#define CHECK( cond, what ) \
	do { if( cond ) { std::printf( "PASS  %s\n", what ); } else { std::printf( "FAIL  %s\n", what ); ++failures; } } while( 0 )

// One 20 ms frame of a 440 Hz sine at 48 kHz, continuing phase across frames.
static QByteArray encodeSineFrame( OpusEncoder* encoder, int frameIndex, double amplitude = 0.5 )
{
	opus_int16 pcm[FrameSamples];
	for( int i = 0; i < FrameSamples; ++i )
	{
		const double t = double( frameIndex * FrameSamples + i ) / SampleRate;
		pcm[i] = opus_int16( amplitude * 32767.0 * std::sin( 2 * M_PI * 440.0 * t ) );
	}
	unsigned char out[MaxFrameBytes];
	const int n = opus_encode( encoder, pcm, FrameSamples, out, MaxFrameBytes );
	return QByteArray( reinterpret_cast<const char*>( out ), qMax( 0, n ) );
}

static double rms( const QByteArray& int16Data )
{
	const auto* s = reinterpret_cast<const qint16*>( int16Data.constData() );
	const int n = int( int16Data.size() / 2 );
	double sum = 0;
	for( int i = 0; i < n; ++i )
	{
		sum += double( s[i] ) * s[i];
	}
	return n ? std::sqrt( sum / n ) / 32768.0 : 0;
}

static void testFraming()
{
	FrameReader reader;
	const auto a = frame( FrameType::Audio, audioPayload( 7, QByteArrayLiteral( "abc" ) ) );
	const auto b = frame( FrameType::Bye, {} );
	const auto both = a + b;
	// Deliver byte by byte to exercise partial reads.
	FrameType type;
	QByteArray payload;
	bool error = false;
	int frames = 0;
	for( char c : both )
	{
		reader.append( QByteArray( 1, c ) );
		while( reader.next( type, payload, error ) )
		{
			++frames;
			if( frames == 1 )
			{
				CHECK( type == FrameType::Audio && qFromBigEndian<quint32>( payload.constData() ) == 7 &&
					   payload.mid( 4 ) == "abc", "audio frame parsed across partial reads" );
			}
			else
			{
				CHECK( type == FrameType::Bye && payload.isEmpty(), "second frame parsed" );
			}
		}
	}
	CHECK( frames == 2 && error == false, "exactly two frames, no error" );

	FrameReader bad;
	QByteArray huge( 4, '\0' );
	qToBigEndian( quint32( MaxPayload + 100 ), huge.data() );
	bad.append( huge + "x" );
	bad.next( type, payload, error );
	CHECK( error, "oversized frame rejected" );
}

static void testMixer()
{
	int err = 0;
	auto encoder = opus_encoder_create( SampleRate, Channels, OPUS_APPLICATION_VOIP, &err );
	CHECK( err == OPUS_OK, "opus encoder created" );

	LabAudioMixer mixer;
	QAudioFormat format;
	format.setSampleRate( SampleRate );
	format.setChannelCount( 1 );
	format.setSampleFormat( QAudioFormat::Int16 );
	mixer.setOutputFormat( format );
	mixer.open( QIODevice::ReadOnly | QIODevice::Unbuffered );

	// Before enough data is buffered, output is silence.
	mixer.addPacket( 1, 0, encodeSineFrame( encoder, 0 ) );
	const auto early = mixer.read( FrameSamples * 2 );
	CHECK( early.size() == FrameSamples * 2 && rms( early ) < 0.001, "silence until jitter buffer primed" );

	for( int i = 1; i < 6; ++i )
	{
		mixer.addPacket( 1, quint32( i ), encodeSineFrame( encoder, i ) );
	}
	QByteArray out;
	for( int i = 0; i < 4; ++i )
	{
		out += mixer.read( FrameSamples * 2 );
	}
	const auto level = rms( out );
	std::printf( "      decoded RMS %.3f (sine 0.5 peak -> ~0.354)\n", level );
	CHECK( level > 0.25 && level < 0.45, "decoded audio has the expected level" );

	// Two streams are summed.
	LabAudioMixer two;
	two.setOutputFormat( format );
	two.open( QIODevice::ReadOnly | QIODevice::Unbuffered );
	auto encoder2 = opus_encoder_create( SampleRate, Channels, OPUS_APPLICATION_VOIP, &err );
	for( int i = 0; i < 6; ++i )
	{
		two.addPacket( 1, quint32( i ), encodeSineFrame( encoder, 100 + i, 0.2 ) );
		two.addPacket( 2, quint32( i ), encodeSineFrame( encoder2, 100 + i, 0.2 ) );
	}
	QByteArray mixed;
	for( int i = 0; i < 4; ++i )
	{
		mixed += two.read( FrameSamples * 2 );
	}
	std::printf( "      mixed RMS %.3f (two in-phase 0.2 sines -> ~0.28)\n", rms( mixed ) );
	CHECK( two.streamCount() == 2 && rms( mixed ) > 0.2, "two streams mixed" );

	// Lost packets are concealed rather than stalling the stream.
	LabAudioMixer lossy;
	lossy.setOutputFormat( format );
	lossy.open( QIODevice::ReadOnly | QIODevice::Unbuffered );
	auto encoder3 = opus_encoder_create( SampleRate, Channels, OPUS_APPLICATION_VOIP, &err );
	for( int i = 0; i < 10; ++i )
	{
		const auto packet = encodeSineFrame( encoder3, i );
		if( i != 4 && i != 5 )
		{
			lossy.addPacket( 1, quint32( i ), packet );
		}
	}
	QByteArray lossOut;
	for( int i = 0; i < 8; ++i )
	{
		lossOut += lossy.read( FrameSamples * 2 );
	}
	CHECK( rms( lossOut ) > 0.25, "two lost frames concealed" );

	// A late burst (Wi-Fi) empties the buffer: the target grows, then excess drains gradually.
	LabAudioMixer jittery;
	jittery.setOutputFormat( format );
	jittery.open( QIODevice::ReadOnly | QIODevice::Unbuffered );
	auto encoder5 = opus_encoder_create( SampleRate, Channels, OPUS_APPLICATION_VOIP, &err );
	int seq = 0;
	for( ; seq < 3; ++seq )
	{
		jittery.addPacket( 1, quint32( seq ), encodeSineFrame( encoder5, seq ) );
	}
	for( int i = 0; i < 4; ++i )
	{
		jittery.read( FrameSamples * 2 );
	}
	CHECK( jittery.targetFrames( 1 ) == 5, "underflow grows jitter buffer from 60 to 100 ms" );
	for( ; seq < 33; ++seq )
	{
		jittery.addPacket( 1, quint32( seq ), encodeSineFrame( encoder5, seq ) );
	}
	QByteArray smooth;
	for( int i = 0; i < 10; ++i )
	{
		smooth += jittery.read( FrameSamples * 2 );
	}
	CHECK( rms( smooth ) > 0.25 && jittery.targetFrames( 1 ) == 5, "playback continues smoothly after a burst" );

	// Resampling to a 44.1 kHz stereo float device.
	LabAudioMixer resampled;
	QAudioFormat device;
	device.setSampleRate( 44100 );
	device.setChannelCount( 2 );
	device.setSampleFormat( QAudioFormat::Float );
	resampled.setOutputFormat( device );
	resampled.open( QIODevice::ReadOnly | QIODevice::Unbuffered );
	auto encoder4 = opus_encoder_create( SampleRate, Channels, OPUS_APPLICATION_VOIP, &err );
	for( int i = 0; i < 8; ++i )
	{
		resampled.addPacket( 1, quint32( i ), encodeSineFrame( encoder4, i ) );
	}
	const auto floatOut = resampled.read( 44100 / 50 * 2 * 4 * 4 );
	const auto* f = reinterpret_cast<const float*>( floatOut.constData() );
	double sum = 0;
	int crossings = 0;
	const int n = int( floatOut.size() / 8 );
	for( int i = 1; i < n; ++i )
	{
		sum += double( f[i * 2] ) * f[i * 2];
		if( ( f[i * 2 - 2] < 0 ) != ( f[i * 2] < 0 ) )
		{
			++crossings;
		}
		if( f[i * 2] != f[i * 2 + 1] )
		{
			sum = -1e9;
		}
	}
	const double seconds = double( n ) / 44100.0;
	const double frequency = crossings / 2.0 / seconds;
	std::printf( "      resampled frequency %.0f Hz (expected 440)\n", frequency );
	CHECK( sum > 0 && frequency > 400 && frequency < 480, "44.1 kHz stereo float output keeps pitch, both channels" );

	opus_encoder_destroy( encoder );
	opus_encoder_destroy( encoder2 );
	opus_encoder_destroy( encoder3 );
	opus_encoder_destroy( encoder4 );
}

int main( int argc, char** argv )
{
	QCoreApplication app( argc, argv );
	if( argc > 1 && QByteArray( argv[1] ) == "--devices" )
	{
		for( const auto& d : QMediaDevices::audioInputs() ) { std::printf( "input:  %s\n", qPrintable( d.description() ) ); }
		for( const auto& d : QMediaDevices::audioOutputs() ) { std::printf( "output: %s\n", qPrintable( d.description() ) ); }
		return 0;
	}
	testFraming();
	testMixer();
	std::printf( failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures );
	return failures ? 1 : 0;
}
