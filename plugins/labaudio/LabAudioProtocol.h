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
#include <QList>
#include <QString>
#include <QtEndian>

// Framing over one TCP connection per student:
//   [u32 big-endian payload length][u8 type][payload bytes]
// Handshake (plain): client Hello, hub HelloAck, then the client's first Encrypted frame (Auth)
// proves it knows the token. From then on every frame is Encrypted (see LabAudioCrypto.h); the
// types below Encrypted travel inside it. Anything else closes the connection.
namespace LabAudioProtocol
{

static constexpr quint8 Version = 2;
static constexpr int SampleRate = 48000;
static constexpr int Channels = 1;
static constexpr int FrameSamples = 960;			// 20 ms at 48 kHz
static constexpr int MaxFrameBytes = 4000;			// upper bound for one Opus packet
static constexpr int MaxPayload = 8192;
static constexpr int HelloTimeoutMs = 5000;

enum class FrameType : quint8
{
	Hello = 1,		// client -> hub (plain): u8 version, u8 role flags, 8 byte key id, 16 byte client nonce
	HelloAck = 2,	// hub -> client (plain): u8 accepted (1/0) [, 16 byte hub nonce]
	Audio = 3,		// both directions: u32 sequence, Opus packet
	Bye = 4,
	Status = 5,		// hub -> client: u8 StatusFlag bits (what the teacher does with the client's group)
	PeerAudio = 6,	// hub -> client: u32 source id, u32 sequence, Opus packet of a group member
	// 7 was CallTeacher; calls now travel through Veyon (LabAudioCallBoard), not the audio connection.
	Auth = 8,		// client -> hub, first encrypted frame: u8 role flags
	Encrypted = 16	// AES-256-GCM of u8 inner type + inner payload, followed by the 16 byte tag
};

// What a client does in the current lab mode (bit flags).
enum RoleFlag : quint8
{
	ReceiveTeacher = 0x01,	// play the teacher's voice
	SendMicrophone = 0x02,	// send the student's microphone to the teacher (or group)
	ReceivePeers = 0x04		// play the other members of the student's group
};

enum StatusFlag : quint8
{
	TeacherListening = 0x01,
	TeacherTalking = 0x02
};

// Source id 0 is the teacher; group members use their hub client id.
static constexpr int TeacherStreamId = 0;

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

// Forwards a student's Audio payload (u32 sequence + Opus) to the other group members.
inline QByteArray peerAudioPayload( quint32 sourceId, const QByteArray& audioFramePayload )
{
	QByteArray payload( 4, Qt::Uninitialized );
	qToBigEndian( sourceId, payload.data() );
	payload.append( audioFramePayload );
	return payload;
}

// Splits count members into groups of the given size (indices into the member list). A single
// member left over joins the last group instead of staying alone.
inline QList<QList<int>> makeGroups( const QList<int>& order, int size )
{
	QList<QList<int>> groups;
	if( size < 2 || order.size() < 2 )
	{
		return groups;
	}
	for( int i = 0; i < order.size(); i += size )
	{
		groups.append( order.mid( i, size ) );
	}
	if( groups.size() > 1 && groups.last().size() == 1 )
	{
		groups[groups.size() - 2].append( groups.takeLast() );
	}
	return groups;
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
