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
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QtEndian>
#include <QtMath>
#include <cstdio>

#include <opus/opus.h>

#include "../LabAudioCallBoard.h"
#include "../LabAudioCrypto.h"
#include "../LabAudioEngine.h"
#include "../LabAudioProtocol.h"
#include "../LabAudioRecorder.h"

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

	// The student's volume slider scales the output.
	LabAudioMixer quiet;
	quiet.setOutputFormat( format );
	quiet.open( QIODevice::ReadOnly | QIODevice::Unbuffered );
	quiet.setGain( 0.5f );
	auto encoderGain = opus_encoder_create( SampleRate, Channels, OPUS_APPLICATION_VOIP, &err );
	for( int i = 0; i < 6; ++i )
	{
		quiet.addPacket( 1, quint32( i ), encodeSineFrame( encoderGain, i ) );
	}
	QByteArray quietOut;
	for( int i = 0; i < 4; ++i )
	{
		quietOut += quiet.read( FrameSamples * 2 );
	}
	const auto quietLevel = rms( quietOut );
	std::printf( "      volume 50%% RMS %.3f\n", quietLevel );
	CHECK( quietLevel > level * 0.4 && quietLevel < level * 0.6, "volume 50% halves the level" );

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

// Opens an Encrypted frame produced by seal() (strips the frame header).
static bool openFrame( LabAudioSession& session, const QByteArray& sealed, FrameType& type, QByteArray& payload )
{
	FrameReader reader;
	reader.append( sealed );
	FrameType outer;
	QByteArray encrypted;
	bool error = false;
	return reader.next( outer, encrypted, error ) && outer == FrameType::Encrypted &&
		   session.open( encrypted, type, payload );
}

static void testCrypto()
{
	const QByteArray token = QByteArrayLiteral( "class token with 64 random bytes in real use" );
	const auto clientNonce = LabAudioSession::randomNonce();
	const auto hubNonce = LabAudioSession::randomNonce();
	CHECK( clientNonce.size() == LabAudioSession::NonceSize && clientNonce != hubNonce, "random nonces" );
	CHECK( LabAudioSession::keyId( token ) == LabAudioSession::keyId( token ) &&
		   LabAudioSession::keyId( token ) != LabAudioSession::keyId( token + "x" ) &&
		   token.contains( LabAudioSession::keyId( token ) ) == false,
		   "key id identifies the token without containing it" );

	LabAudioSession client, hub;
	client.init( token, clientNonce, hubNonce, LabAudioSession::Role::Client );
	hub.init( token, clientNonce, hubNonce, LabAudioSession::Role::Hub );

	const QByteArray secret = QByteArrayLiteral( "this is the student's voice" );
	FrameType type;
	QByteArray payload;
	const auto first = client.seal( FrameType::Audio, secret );
	CHECK( first.contains( secret ) == false, "payload is not visible in the encrypted frame" );
	CHECK( openFrame( hub, first, type, payload ) && type == FrameType::Audio && payload == secret,
		   "hub decrypts what the client sealed" );
	CHECK( openFrame( hub, first, type, payload ) == false, "replayed frame is rejected" );

	auto second = client.seal( FrameType::Audio, secret );
	second[second.size() - 20] = char( second.at( second.size() - 20 ) ^ 0x01 );
	CHECK( openFrame( hub, second, type, payload ) == false, "modified frame is rejected" );

	const auto toClient = hub.seal( FrameType::Status, QByteArray( 1, 1 ) );
	CHECK( openFrame( client, toClient, type, payload ) && type == FrameType::Status, "hub -> client direction" );

	LabAudioSession wrong;
	wrong.init( QByteArrayLiteral( "guessed token" ), clientNonce, hubNonce, LabAudioSession::Role::Hub );
	CHECK( openFrame( wrong, client.seal( FrameType::Audio, secret ), type, payload ) == false,
		   "wrong token cannot decrypt" );

	LabAudioSession otherConnection;
	otherConnection.init( token, LabAudioSession::randomNonce(), hubNonce, LabAudioSession::Role::Hub );
	LabAudioSession freshClient;
	freshClient.init( token, clientNonce, hubNonce, LabAudioSession::Role::Client );
	CHECK( openFrame( otherConnection, freshClient.seal( FrameType::Audio, secret ), type, payload ) == false,
		   "frames of one connection are useless in another" );

	std::printf( "      encryption overhead %d bytes per frame\n", LabAudioSession::Overhead );
}



// Minimal Ogg reader for checking recordings: returns the packets, verifies every page checksum.
struct OggCheck
{
	QList<QByteArray> packets;
	QList<qint64> granules;	// per page
	QList<quint8> headerTypes;
	bool checksumsOk{true};
	bool sequenceOk{true};
};

static OggCheck readOgg( const QString& path )
{
	OggCheck result;
	QFile file( path );
	if( file.open( QFile::ReadOnly ) == false )
	{
		result.checksumsOk = false;
		return result;
	}
	const auto data = file.readAll();
	qsizetype pos = 0;
	quint32 expectedSequence = 0;
	QByteArray partial;
	while( pos + 27 <= data.size() && data.mid( pos, 4 ) == "OggS" )
	{
		const auto segments = quint8( data.at( pos + 26 ) );
		const auto headerSize = 27 + segments;
		qsizetype bodySize = 0;
		for( int i = 0; i < segments; ++i )
		{
			bodySize += quint8( data.at( pos + 27 + i ) );
		}
		auto page = data.mid( pos, headerSize + bodySize );
		const auto storedCrc = qFromLittleEndian<quint32>( page.constData() + 22 );
		qToLittleEndian( quint32( 0 ), page.data() + 22 );
		result.checksumsOk &= LabAudioOggOpusWriter::crc( page ) == storedCrc;
		result.sequenceOk &= qFromLittleEndian<quint32>( page.constData() + 18 ) == expectedSequence++;
		result.headerTypes.append( quint8( page.at( 5 ) ) );
		result.granules.append( qFromLittleEndian<qint64>( page.constData() + 6 ) );
		qsizetype offset = headerSize;
		for( int i = 0; i < segments; ++i )
		{
			const auto length = quint8( page.at( 27 + i ) );
			partial += page.mid( offset, length );
			offset += length;
			if( length < 255 )
			{
				result.packets.append( partial );
				partial.clear();
			}
		}
		pos += headerSize + bodySize;
	}
	return result;
}

static void testRecording()
{
	CHECK( LabAudioOggOpusWriter::crc( QByteArrayLiteral( "123456789" ) ) == 0x89a1897f,
		   "Ogg checksum matches the CRC-32 reference value" );

	const auto path = QDir::temp().filePath( QStringLiteral( "labaudio-selftest.opus" ) );
	{
		int err = 0;
		auto encoder = opus_encoder_create( SampleRate, Channels, OPUS_APPLICATION_VOIP, &err );
		LabAudioRecorder recorder( path, QStringLiteral( "Selftest – Grüße" ) );
		CHECK( recorder.isRecording(), "recording file created" );
		// 2 s of a sine sent in real time, like a microphone stream.
		QElapsedTimer clock;
		clock.start();
		int sent = 0;
		while( clock.elapsed() < 2000 )
		{
			while( sent < clock.elapsed() / 20 )
			{
				recorder.addPacket( 7, quint32( sent ), encodeSineFrame( encoder, sent ) );
				++sent;
			}
			QCoreApplication::processEvents( QEventLoop::AllEvents, 5 );
		}
		opus_encoder_destroy( encoder );
	}

	const auto ogg = readOgg( path );
	CHECK( ogg.checksumsOk && ogg.sequenceOk && ogg.packets.size() > 90, "file consists of valid Ogg pages" );
	CHECK( ogg.packets.value( 0 ).startsWith( "OpusHead" ) && ogg.packets.value( 0 ).size() == 19 &&
		   quint8( ogg.packets.value( 0 ).at( 9 ) ) == 1 &&
		   qFromLittleEndian<quint32>( ogg.packets.value( 0 ).constData() + 12 ) == 48000 &&
		   ogg.headerTypes.value( 0 ) == 0x02,
		   "OpusHead: version 1, mono, 48 kHz, first page" );
	CHECK( ogg.packets.value( 1 ).startsWith( "OpusTags" ) &&
		   ogg.packets.value( 1 ).contains( QStringLiteral( "TITLE=Selftest – Grüße" ).toUtf8() ),
		   "OpusTags with the recording title (UTF-8)" );
	CHECK( ogg.headerTypes.last() == 0x04, "last page marks the end of the stream" );

	int err = 0;
	auto decoder = opus_decoder_create( SampleRate, Channels, &err );
	QByteArray pcm;
	opus_int16 frame[FrameSamples];
	for( int i = 2; i < ogg.packets.size(); ++i )
	{
		const auto n = opus_decode( decoder, reinterpret_cast<const unsigned char*>( ogg.packets[i].constData() ),
									int( ogg.packets[i].size() ), frame, FrameSamples, 0 );
		if( n > 0 )
		{
			pcm.append( reinterpret_cast<const char*>( frame ), n * 2 );
		}
	}
	opus_decoder_destroy( decoder );
	const auto audioPackets = ogg.packets.size() - 2;
	const auto preSkip = qFromLittleEndian<quint16>( ogg.packets.value( 0 ).constData() + 10 );
	std::printf( "      recorded %lld packets (%.2f s), granule %lld, pre-skip %d, RMS of the second half %.3f\n",
				 qint64( audioPackets ), audioPackets * 0.02, ogg.granules.last(), preSkip,
				 rms( pcm.mid( pcm.size() / 2 ) ) );
	CHECK( audioPackets >= 95 && audioPackets <= 102, "about 2 s recorded (100 packets of 20 ms)" );
	CHECK( ogg.granules.last() == preSkip + qint64( audioPackets ) * FrameSamples, "granule position counts all samples" );
	CHECK( rms( pcm.mid( pcm.size() / 2 ) ) > 0.25, "recorded audio is the sine at the expected level" );
	QFile::remove( path );
}



static void testCallBoard()
{
	LabAudioCallBoard board;
	const auto v0 = board.version();
	CHECK( board.state().calling == false && board.state().allowed, "nobody is calling at the start, calls allowed" );

	CHECK( board.setCalling( true ) && board.state().calling && board.state().since.isValid() && board.version() > v0,
		   "a call is stored with its time and announced (new version)" );
	const auto v1 = board.version();
	CHECK( board.setCalling( true ) == false && board.version() == v1,
		   "pressing again from the other button (tray / notice) changes nothing" );
	CHECK( board.answer() && board.state().calling == false && board.version() > v1, "teacher's \"Done\" ends the call" );

	board.setCalling( true );
	CHECK( board.setAllowed( false ) && board.state().calling == false, "turning calls off also ends a running call" );
	CHECK( board.setCalling( true ) == false && board.state().calling == false, "no call while calls are turned off" );
	board.setAllowed( true );
	CHECK( board.setCalling( true ) && board.state().calling, "calls work again after turning them on" );
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
	if( argc > 2 && QByteArray( argv[1] ) == "--check-ogg" )
	{
		// Checks a recording made by Veyon: page checksums, headers, length and level.
		const auto ogg = readOgg( QString::fromLocal8Bit( argv[2] ) );
		int err = 0;
		auto decoder = opus_decoder_create( SampleRate, Channels, &err );
		QByteArray pcm;
		opus_int16 frame[FrameSamples];
		for( int i = 2; i < ogg.packets.size(); ++i )
		{
			const auto n = opus_decode( decoder, reinterpret_cast<const unsigned char*>( ogg.packets[i].constData() ),
										int( ogg.packets[i].size() ), frame, FrameSamples, 0 );
			pcm.append( reinterpret_cast<const char*>( frame ), qMax( 0, n ) * 2 );
		}
		opus_decoder_destroy( decoder );
		std::printf( "checksums %s, page order %s, head %s, end marker %s, %.1f s, RMS %.3f\n",
					 ogg.checksumsOk ? "ok" : "BAD", ogg.sequenceOk ? "ok" : "BAD",
					 ogg.packets.value( 0 ).startsWith( "OpusHead" ) ? "ok" : "BAD",
					 ogg.headerTypes.value( ogg.headerTypes.size() - 1 ) == 0x04 ? "ok" : "missing",
					 ( ogg.packets.size() - 2 ) * 0.02, rms( pcm ) );
		return ogg.checksumsOk && ogg.sequenceOk ? 0 : 1;
	}
	testFraming();
	testMixer();
	testCrypto();
	testCallBoard();
	testRecording();
	std::printf( failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures );
	return failures ? 1 : 0;
}
