/*
 * LabAudioRecorder.cpp - records live lab audio to Ogg Opus files
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

#include <QDateTime>
#include <QDir>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtEndian>

#include <opus/opus.h>

#include <array>

#include "LabAudioProtocol.h"
#include "LabAudioRecorder.h"

using namespace LabAudioProtocol;

namespace
{

constexpr int MaxSegmentsPerPage = 255;
constexpr int PacketsPerPage = 50;	// one page per second keeps the file playable if the recorder is killed

QByteArray le16( quint16 value )
{
	QByteArray out( 2, Qt::Uninitialized );
	qToLittleEndian( value, out.data() );
	return out;
}

QByteArray le32( quint32 value )
{
	QByteArray out( 4, Qt::Uninitialized );
	qToLittleEndian( value, out.data() );
	return out;
}

QByteArray le64( qint64 value )
{
	QByteArray out( 8, Qt::Uninitialized );
	qToLittleEndian( value, out.data() );
	return out;
}

int segmentsFor( const QByteArray& packet )
{
	return int( packet.size() ) / 255 + 1;
}

}



LabAudioOggOpusWriter::~LabAudioOggOpusWriter()
{
	close();
}



quint32 LabAudioOggOpusWriter::crc( const QByteArray& data )
{
	static const auto table = []() {
		std::array<quint32, 256> t{};
		for( quint32 i = 0; i < 256; ++i )
		{
			quint32 r = i << 24;
			for( int b = 0; b < 8; ++b )
			{
				r = ( r & 0x80000000 ) ? ( r << 1 ) ^ 0x04c11db7 : ( r << 1 );
			}
			t[i] = r;
		}
		return t;
	}();

	quint32 value = 0;
	for( const auto byte : data )
	{
		value = ( value << 8 ) ^ table[( ( value >> 24 ) ^ quint8( byte ) ) & 0xff];
	}
	return value;
}



bool LabAudioOggOpusWriter::open( const QString& path, const QString& title, int preSkip )
{
	close();
	m_file.setFileName( path );
	if( m_file.open( QFile::WriteOnly | QFile::Truncate ) == false )
	{
		return false;
	}
	m_serial = QRandomGenerator::global()->generate();
	m_pageSequence = 0;
	m_preSkip = preSkip;
	m_samples = 0;
	m_pending.clear();
	m_pendingSegments = 0;

	// Identification header (RFC 7845 5.1): mono, 48 kHz, channel mapping family 0.
	QByteArray head = QByteArrayLiteral( "OpusHead" );
	head += char( 1 );
	head += char( Channels );
	head += le16( quint16( preSkip ) );
	head += le32( SampleRate );
	head += le16( 0 );
	head += char( 0 );
	writePage( { head }, 0x02, 0 );

	// Comment header (RFC 7845 5.2).
	const QByteArray vendor = QByteArrayLiteral( "Veyon Lab Audio" );
	QList<QByteArray> comments{ QByteArray( "TITLE=" + title.toUtf8() ),
								QByteArray( "DATE=" + QDateTime::currentDateTime().toString( Qt::ISODate ).toUtf8() ),
								QByteArrayLiteral( "ENCODER=Veyon Lab Audio (libopus)" ) };
	QByteArray tags = QByteArrayLiteral( "OpusTags" );
	tags += le32( quint32( vendor.size() ) );
	tags += vendor;
	tags += le32( quint32( comments.size() ) );
	for( const auto& comment : std::as_const( comments ) )
	{
		tags += le32( quint32( comment.size() ) );
		tags += comment;
	}
	writePage( { tags }, 0x00, 0 );
	return true;
}



void LabAudioOggOpusWriter::writePacket( const QByteArray& opusPacket )
{
	if( isOpen() == false )
	{
		return;
	}
	if( m_pendingSegments + segmentsFor( opusPacket ) > MaxSegmentsPerPage )
	{
		flush( false );
	}
	m_pending.append( opusPacket );
	m_pendingSegments += segmentsFor( opusPacket );
	m_samples += FrameSamples;
	if( m_pending.size() >= PacketsPerPage )
	{
		flush( false );
	}
}



void LabAudioOggOpusWriter::close()
{
	if( isOpen() == false )
	{
		return;
	}
	flush( true );
	m_file.close();
}



void LabAudioOggOpusWriter::flush( bool last )
{
	if( m_pending.isEmpty() && last == false )
	{
		return;
	}
	// The granule position counts 48 kHz samples including the pre-skip.
	writePage( m_pending, last ? 0x04 : 0x00, m_preSkip + m_samples );
	m_pending.clear();
	m_pendingSegments = 0;
	m_file.flush();
}



void LabAudioOggOpusWriter::writePage( const QList<QByteArray>& packets, quint8 headerType, qint64 granule )
{
	QByteArray lacing;
	QByteArray body;
	for( const auto& packet : packets )
	{
		auto remaining = int( packet.size() );
		while( remaining >= 255 )
		{
			lacing += char( 255 );
			remaining -= 255;
		}
		lacing += char( remaining );
		body += packet;
	}

	QByteArray page = QByteArrayLiteral( "OggS" );
	page += char( 0 );	// stream structure version
	page += char( headerType );
	page += le64( granule );
	page += le32( m_serial );
	page += le32( m_pageSequence++ );
	page += le32( 0 );	// checksum, filled in below
	page += char( lacing.size() );
	page += lacing;
	page += body;
	qToLittleEndian( crc( page ), page.data() + 22 );
	m_file.write( page );
}



LabAudioRecorder::LabAudioRecorder( const QString& path, const QString& title, int bitRate, QObject* parent ) :
	QObject( parent ),
	m_path( path ),
	m_title( title ),
	m_mixer( this )
{
	QAudioFormat format;
	format.setSampleRate( SampleRate );
	format.setChannelCount( Channels );
	format.setSampleFormat( QAudioFormat::Int16 );
	m_mixer.setOutputFormat( format );
	m_mixer.open( QIODevice::ReadOnly | QIODevice::Unbuffered );

	int error = 0;
	m_encoder = opus_encoder_create( SampleRate, Channels, OPUS_APPLICATION_VOIP, &error );
	if( error != OPUS_OK || m_encoder == nullptr )
	{
		m_encoder = nullptr;
		return;
	}
	opus_encoder_ctl( m_encoder, OPUS_SET_BITRATE( bitRate ) );
	opus_int32 lookahead = 0;
	opus_encoder_ctl( m_encoder, OPUS_GET_LOOKAHEAD( &lookahead ) );

	if( m_writer.open( path, title, int( lookahead ) ) == false )
	{
		return;
	}

	// Driven by the wall clock: every due 20 ms frame is mixed and encoded, whatever the timer jitter.
	m_clock.start();
	m_timer.setTimerType( Qt::PreciseTimer );
	m_timer.setInterval( 10 );
	connect( &m_timer, &QTimer::timeout, this, &LabAudioRecorder::tick );
	m_timer.start();
}



LabAudioRecorder::~LabAudioRecorder()
{
	m_timer.stop();
	if( m_writer.isOpen() )
	{
		tick();
		m_writer.close();
	}
	if( m_encoder )
	{
		opus_encoder_destroy( m_encoder );
	}
}



void LabAudioRecorder::addPacket( int streamId, quint32 sequence, const QByteArray& opusPacket )
{
	if( isRecording() )
	{
		m_mixer.addPacket( streamId, sequence, opusPacket );
	}
}



void LabAudioRecorder::removeStream( int streamId )
{
	m_mixer.removeStream( streamId );
}



void LabAudioRecorder::tick()
{
	if( m_encoder == nullptr || m_writer.isOpen() == false )
	{
		return;
	}
	const auto due = m_clock.elapsed() / 20;
	unsigned char packet[MaxFrameBytes];
	while( m_frames < due )
	{
		const auto pcm = m_mixer.read( FrameSamples * 2 );
		if( pcm.size() != FrameSamples * 2 )
		{
			break;
		}
		const auto bytes = opus_encode( m_encoder, reinterpret_cast<const opus_int16*>( pcm.constData() ), FrameSamples,
										packet, MaxFrameBytes );
		if( bytes > 0 )
		{
			m_writer.writePacket( QByteArray( reinterpret_cast<const char*>( packet ), bytes ) );
		}
		++m_frames;
	}
}



namespace
{
QString configuredFolder;
}



void LabAudioRecorder::setConfiguredFolder( const QString& folder )
{
	// Expand Windows-style %VARIABLE% references, e.g. D:\Recordings\%USERNAME%.
	QString expanded = folder.trimmed();
	const auto environment = QProcessEnvironment::systemEnvironment();
	static const QRegularExpression variable( QStringLiteral( "%([^%]+)%" ) );
	auto match = variable.match( expanded );
	while( match.hasMatch() )
	{
		// Unknown variables stay as they are; replaced text is not expanded again.
		auto next = match.capturedEnd();
		if( environment.contains( match.captured( 1 ) ) )
		{
			const auto value = environment.value( match.captured( 1 ) );
			expanded.replace( match.capturedStart(), match.capturedLength(), value );
			next = match.capturedStart() + value.size();
		}
		match = variable.match( expanded, next );
	}
	configuredFolder = QDir::fromNativeSeparators( expanded );
}



QString LabAudioRecorder::recordingsFolder()
{
	// LABAUDIO_RECORDINGS_DIR: used by the self-tests so they do not write into Documents.
	QString folder = configuredFolder;
	if( qEnvironmentVariableIsSet( "LABAUDIO_RECORDINGS_DIR" ) )
	{
		folder = qEnvironmentVariable( "LABAUDIO_RECORDINGS_DIR" );
	}
	else if( folder.isEmpty() )
	{
		folder = QStandardPaths::writableLocation( QStandardPaths::DocumentsLocation ) +
				 QStringLiteral( "/Lab audio recordings" );
	}
	QDir().mkpath( folder );
	return folder;
}



QString LabAudioRecorder::newRecordingPath( const QString& label )
{
	auto name = QDateTime::currentDateTime().toString( QStringLiteral( "yyyy-MM-dd HH-mm-ss" ) );
	if( label.isEmpty() == false )
	{
		name += QLatin1Char( ' ' ) + label;
	}
	static const QString notAllowed = QStringLiteral( "\\/:*?\"<>|" );
	for( auto& c : name )
	{
		if( notAllowed.contains( c ) || c.unicode() < 32 )
		{
			c = QLatin1Char( '_' );
		}
	}
	return recordingsFolder() + QLatin1Char( '/' ) + name.left( 150 ).trimmed() + QStringLiteral( ".opus" );
}
