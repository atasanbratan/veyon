/*
 * LabAudioEngine.cpp - microphone capture/Opus encoding and decoding/mixing playback
 *
 * Copyright (c) 2026 TDMUGI
 *
 * This file is part of Veyon - https://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>
#include <QMutexLocker>
#include <QtMath>

#include <opus/opus.h>

#include "LabAudioEngine.h"
#include "LabAudioProtocol.h"

using namespace LabAudioProtocol;

namespace
{

QMutex s_mixerMutex;

// Reads one sample of one channel from interleaved device data as float -1..1.
float readSample( const char* p, QAudioFormat::SampleFormat format )
{
	switch( format )
	{
	case QAudioFormat::UInt8: return ( float( *reinterpret_cast<const quint8*>( p ) ) - 128.0f ) / 128.0f;
	case QAudioFormat::Int16: return float( *reinterpret_cast<const qint16*>( p ) ) / 32768.0f;
	case QAudioFormat::Int32: return float( *reinterpret_cast<const qint32*>( p ) ) / 2147483648.0f;
	case QAudioFormat::Float: return *reinterpret_cast<const float*>( p );
	default: return 0.0f;
	}
}

void writeSample( char* p, QAudioFormat::SampleFormat format, float value )
{
	value = qBound( -1.0f, value, 1.0f );
	switch( format )
	{
	case QAudioFormat::UInt8: *reinterpret_cast<quint8*>( p ) = quint8( qRound( value * 127.0f ) + 128 ); break;
	case QAudioFormat::Int16: *reinterpret_cast<qint16*>( p ) = qint16( qRound( value * 32767.0f ) ); break;
	case QAudioFormat::Int32: *reinterpret_cast<qint32*>( p ) = qint32( double( value ) * 2147483647.0 ); break;
	case QAudioFormat::Float: *reinterpret_cast<float*>( p ) = value; break;
	default: break;
	}
}

QAudioFormat preferredLabFormat( const QAudioDevice& device )
{
	QAudioFormat format;
	format.setSampleRate( SampleRate );
	format.setChannelCount( Channels );
	format.setSampleFormat( QAudioFormat::Int16 );
	if( device.isFormatSupported( format ) )
	{
		return format;
	}
	// Fall back to the device's native format; samples are converted in software.
	return device.preferredFormat();
}

}



LabAudioCapture::LabAudioCapture( QObject* parent ) :
	QObject( parent )
{
}



LabAudioCapture::~LabAudioCapture()
{
	stop();
}



bool LabAudioCapture::start()
{
	if( m_source )
	{
		return true;
	}

	const auto device = QMediaDevices::defaultAudioInput();
	if( device.isNull() )
	{
		m_error = tr( "No microphone is available." );
		return false;
	}

	int error = 0;
	m_encoder = opus_encoder_create( SampleRate, Channels, OPUS_APPLICATION_VOIP, &error );
	if( error != OPUS_OK || m_encoder == nullptr )
	{
		m_error = tr( "The audio encoder could not be initialised." );
		return false;
	}
	opus_encoder_ctl( m_encoder, OPUS_SET_BITRATE( 32000 ) );
	opus_encoder_ctl( m_encoder, OPUS_SET_INBAND_FEC( 1 ) );
	opus_encoder_ctl( m_encoder, OPUS_SET_PACKET_LOSS_PERC( 5 ) );

	m_format = preferredLabFormat( device );
	m_source = new QAudioSource( device, m_format, this );
	m_source->setBufferSize( m_format.bytesForDuration( 100000 ) );
	m_io = m_source->start();
	if( m_io == nullptr || m_source->error() != QAudio::NoError )
	{
		m_error = tr( "The microphone could not be opened. Check that it is connected and that desktop "
					  "apps may use the microphone in the Windows privacy settings." );
		stop();
		return false;
	}
	connect( m_io, &QIODevice::readyRead, this, &LabAudioCapture::readAvailable );
	connect( m_source, &QAudioSource::stateChanged, this, [this]( QAudio::State state ) {
		if( state == QAudio::StoppedState && m_source && m_source->error() != QAudio::NoError )
		{
			Q_EMIT failed( tr( "The microphone stopped working." ) );
		}
	} );
	return true;
}



void LabAudioCapture::stop()
{
	if( m_source )
	{
		m_source->stop();
		delete m_source;
		m_source = nullptr;
	}
	m_io = nullptr;
	if( m_encoder )
	{
		opus_encoder_destroy( m_encoder );
		m_encoder = nullptr;
	}
	m_pending.clear();
	m_resamplePosition = 0;
}



void LabAudioCapture::readAvailable()
{
	if( m_io == nullptr )
	{
		return;
	}
	const auto data = m_io->readAll();
	const int bytesPerFrame = m_format.bytesPerFrame();
	const int bytesPerSample = m_format.bytesPerSample();
	const int channels = m_format.channelCount();
	if( bytesPerFrame <= 0 )
	{
		return;
	}

	// Downmix to mono and resample linearly to 48 kHz.
	const double step = double( m_format.sampleRate() ) / SampleRate;
	for( int offset = 0; offset + bytesPerFrame <= data.size(); offset += bytesPerFrame )
	{
		float mono = 0;
		for( int c = 0; c < channels; ++c )
		{
			mono += readSample( data.constData() + offset + c * bytesPerSample, m_format.sampleFormat() );
		}
		const auto current = qint16( qBound( -32768, qRound( mono / float( channels ) * 32767.0f ), 32767 ) );
		while( m_resamplePosition <= 1.0 )
		{
			m_pending.append( qint16( m_lastSample + ( current - m_lastSample ) * m_resamplePosition ) );
			m_resamplePosition += step;
		}
		m_resamplePosition -= 1.0;
		m_lastSample = current;
	}

	encodeFrames();
}



void LabAudioCapture::encodeFrames()
{
	unsigned char packet[MaxFrameBytes];
	while( m_pending.size() >= FrameSamples && m_encoder )
	{
		int peak = 0;
		for( int i = 0; i < FrameSamples; ++i )
		{
			peak = qMax( peak, qAbs( int( m_pending.at( i ) ) ) );
		}
		const auto bytes = opus_encode( m_encoder, m_pending.constData(), FrameSamples, packet, MaxFrameBytes );
		m_pending.remove( 0, FrameSamples );
		if( bytes > 0 )
		{
			Q_EMIT packetReady( QByteArray( reinterpret_cast<const char*>( packet ), bytes ) );
		}
		Q_EMIT levelChanged( float( peak ) / 32768.0f );
	}
}



LabAudioMixer::LabAudioMixer( QObject* parent ) :
	QIODevice( parent )
{
}



LabAudioMixer::~LabAudioMixer()
{
	QMutexLocker locker( &s_mixerMutex );
	for( auto& stream : m_streams )
	{
		opus_decoder_destroy( stream.decoder );
	}
	m_streams.clear();
}



void LabAudioMixer::setOutputFormat( const QAudioFormat& format )
{
	m_format = format;
}



void LabAudioMixer::addPacket( int streamId, quint32 sequence, const QByteArray& opusPacket )
{
	QMutexLocker locker( &s_mixerMutex );
	auto it = m_streams.find( streamId );
	if( it == m_streams.end() )
	{
		int error = 0;
		Stream stream;
		stream.decoder = opus_decoder_create( SampleRate, Channels, &error );
		if( error != OPUS_OK || stream.decoder == nullptr )
		{
			return;
		}
		it = m_streams.insert( streamId, stream );
	}
	auto& stream = it.value();

	opus_int16 pcm[FrameSamples];
	if( stream.initialized && sequence > stream.nextSequence )
	{
		// Conceal up to two lost frames, then resynchronise.
		const auto lost = qMin<quint32>( sequence - stream.nextSequence, 2 );
		for( quint32 i = 0; i < lost; ++i )
		{
			const auto n = opus_decode( stream.decoder, nullptr, 0, pcm, FrameSamples, 0 );
			for( int s = 0; s < n; ++s )
			{
				stream.samples.push_back( pcm[s] );
			}
		}
	}
	else if( stream.initialized && sequence < stream.nextSequence )
	{
		return;	// late duplicate
	}

	const auto n = opus_decode( stream.decoder, reinterpret_cast<const unsigned char*>( opusPacket.constData() ),
								opusPacket.size(), pcm, FrameSamples, 0 );
	for( int s = 0; s < n; ++s )
	{
		stream.samples.push_back( pcm[s] );
	}
	stream.nextSequence = sequence + 1;
	stream.initialized = true;

	const auto maxSamples = size_t( MaxBufferedFrames * FrameSamples );
	while( stream.samples.size() > maxSamples )
	{
		stream.samples.pop_front();
	}
}



void LabAudioMixer::removeStream( int streamId )
{
	QMutexLocker locker( &s_mixerMutex );
	auto it = m_streams.find( streamId );
	if( it != m_streams.end() )
	{
		opus_decoder_destroy( it->decoder );
		m_streams.erase( it );
	}
}



qint64 LabAudioMixer::bytesAvailable() const
{
	// Endless stream: silence is produced while no stream has data.
	return qint64( m_format.bytesForDuration( 20000 ) ) + QIODevice::bytesAvailable();
}



qint16 LabAudioMixer::mixedSample()
{
	int sum = 0;
	for( auto& stream : m_streams )
	{
		if( stream.primed == false )
		{
			if( stream.samples.size() >= size_t( TargetBufferedFrames * FrameSamples ) )
			{
				stream.primed = true;
			}
			else
			{
				continue;
			}
		}
		if( stream.samples.empty() )
		{
			stream.primed = false;	// underflow: rebuffer before resuming
			continue;
		}
		sum += stream.samples.front();
		stream.samples.pop_front();
	}
	return qint16( qBound( -32768, sum, 32767 ) );
}



qint64 LabAudioMixer::readData( char* data, qint64 maxSize )
{
	QMutexLocker locker( &s_mixerMutex );
	const int bytesPerFrame = m_format.bytesPerFrame();
	const int bytesPerSample = m_format.bytesPerSample();
	if( bytesPerFrame <= 0 )
	{
		return 0;
	}
	const qint64 frames = maxSize / bytesPerFrame;
	const double step = double( SampleRate ) / m_format.sampleRate();
	for( qint64 f = 0; f < frames; ++f )
	{
		while( m_resamplePosition >= 1.0 )
		{
			m_currentSample = m_nextSample;
			m_nextSample = mixedSample();
			m_resamplePosition -= 1.0;
		}
		const float value = ( m_currentSample + ( m_nextSample - m_currentSample ) * float( m_resamplePosition ) ) / 32768.0f;
		m_resamplePosition += step;
		for( int c = 0; c < m_format.channelCount(); ++c )
		{
			writeSample( data + f * bytesPerFrame + c * bytesPerSample, m_format.sampleFormat(), value );
		}
	}
	return frames * bytesPerFrame;
}



qint64 LabAudioMixer::writeData( const char* data, qint64 maxSize )
{
	Q_UNUSED(data)
	Q_UNUSED(maxSize)
	return -1;
}



LabAudioPlayback::LabAudioPlayback( QObject* parent ) :
	QObject( parent ),
	m_mixer( new LabAudioMixer( this ) )
{
}



LabAudioPlayback::~LabAudioPlayback()
{
	stop();
}



bool LabAudioPlayback::start()
{
	if( m_sink )
	{
		return true;
	}
	const auto device = QMediaDevices::defaultAudioOutput();
	if( device.isNull() )
	{
		m_error = tr( "No speaker or headset is available." );
		return false;
	}
	const auto format = preferredLabFormat( device );
	m_mixer->setOutputFormat( format );
	m_mixer->open( QIODevice::ReadOnly );
	m_sink = new QAudioSink( device, format, this );
	m_sink->setBufferSize( format.bytesForDuration( 60000 ) );
	m_sink->start( m_mixer );
	if( m_sink->error() != QAudio::NoError )
	{
		m_error = tr( "The speaker or headset could not be opened." );
		stop();
		return false;
	}
	return true;
}



void LabAudioPlayback::stop()
{
	if( m_sink )
	{
		m_sink->stop();
		delete m_sink;
		m_sink = nullptr;
	}
	m_mixer->close();
}
