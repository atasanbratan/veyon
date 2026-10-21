/*
 * LabAudioProtocol.h - wire format between the teacher's audio hub and student clients
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

#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <QtEndian>

// Framing over one TCP connection per student:
//   [u32 big-endian payload length][u8 type][payload bytes]
// The first frame from the client must be Hello; anything else closes the connection.
namespace LabAudioProtocol
{

static constexpr quint8 Version = 1;
static constexpr int SampleRate = 48000;
static constexpr int Channels = 1;
static constexpr int FrameSamples = 960;			// 20 ms at 48 kHz
static constexpr int MaxFrameBytes = 4000;			// upper bound for one Opus packet
static constexpr int MaxPayload = 8192;
static constexpr int HelloTimeoutMs = 5000;

enum class FrameType : quint8
{
	Hello = 1,		// client -> hub: u8 version, u8 role flags, u16 token length, token bytes
	HelloAck = 2,	// hub -> client: u8 accepted (1/0)
	Audio = 3,		// both directions: u32 sequence, Opus packet
	Bye = 4
};

// What a client does in the current lab mode (bit flags).
enum RoleFlag : quint8
{
	ReceiveTeacher = 0x01,	// play the teacher's voice
	SendMicrophone = 0x02	// send the student's microphone to the teacher
};

inline QByteArray frame( FrameType type, const QByteArray& payload )
{
	QByteArray out;
	out.reserve( 5 + payload.size() );
	quint32 length = quint32( payload.size() + 1 );
	char header[4];
	qToBigEndian( length, header );
	out.append( header, 4 );
	out.append( char( type ) );
	out.append( payload );
	return out;
}

// Short, non-secret fingerprint of a token, so teacher and student logs can be compared.
inline QString tokenFingerprint( const QByteArray& token )
{
	return token.isEmpty() ? QStringLiteral( "empty" )
						   : QString::fromLatin1( QCryptographicHash::hash( token, QCryptographicHash::Sha256 ).toHex().left( 8 ) );
}

inline QByteArray audioPayload( quint32 sequence, const QByteArray& opusPacket )
{
	QByteArray payload( 4, Qt::Uninitialized );
	qToBigEndian( sequence, payload.data() );
	payload.append( opusPacket );
	return payload;
}

// Incremental parser: append received bytes, then take complete frames.
class FrameReader
{
public:
	void append( const QByteArray& data )
	{
		m_buffer.append( data );
	}

	// Returns false and sets error when the peer sent an invalid frame.
	bool next( FrameType& type, QByteArray& payload, bool& error )
	{
		error = false;
		if( m_buffer.size() < 5 )
		{
			return false;
		}
		const auto length = qFromBigEndian<quint32>( m_buffer.constData() );
		if( length < 1 || length > quint32( MaxPayload + 1 ) )
		{
			error = true;
			return false;
		}
		if( quint32( m_buffer.size() ) < 4 + length )
		{
			return false;
		}
		type = FrameType( quint8( m_buffer.at( 4 ) ) );
		payload = m_buffer.mid( 5, int( length ) - 1 );
		m_buffer.remove( 0, 4 + int( length ) );
		return true;
	}

private:
	QByteArray m_buffer;
};

}
