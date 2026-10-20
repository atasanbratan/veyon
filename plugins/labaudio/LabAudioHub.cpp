/*
 * LabAudioHub.cpp - audio hub running in the teacher's session
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

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include "LabAudioEngine.h"
#include "LabAudioHub.h"
#include "Logger.h"

using namespace LabAudioProtocol;

namespace
{

bool constantTimeEquals( const QByteArray& a, const QByteArray& b )
{
	if( a.size() != b.size() || a.isEmpty() )
	{
		return false;
	}
	unsigned char diff = 0;
	for( int i = 0; i < a.size(); ++i )
	{
		diff |= static_cast<unsigned char>( a[i] ^ b[i] );
	}
	return diff == 0;
}

}



LabAudioHub::LabAudioHub( int port, const QByteArray& token, QObject* parent ) :
	QObject( parent ),
	m_server( new QTcpServer( this ) ),
	m_token( token )
{
	connect( m_server, &QTcpServer::newConnection, this, &LabAudioHub::acceptConnections );
	if( m_server->listen( QHostAddress::Any, quint16( port ) ) == false )
	{
		vCritical() << "LabAudio hub could not listen on port" << port << m_server->errorString();
	}
	else
	{
		vInfo() << "LabAudio hub listening on port" << port;
	}
}



LabAudioHub::~LabAudioHub()
{
	const auto ids = m_clients.keys();
	for( const auto id : ids )
	{
		dropClient( id );
	}
}



bool LabAudioHub::isListening() const
{
	return m_server->isListening();
}



void LabAudioHub::setToken( const QByteArray& token )
{
	m_token = token;
}



void LabAudioHub::acceptConnections()
{
	while( m_server->hasPendingConnections() )
	{
		auto socket = m_server->nextPendingConnection();
		if( m_clients.size() >= MaxClients )
		{
			socket->abort();
			socket->deleteLater();
			continue;
		}
		socket->setSocketOption( QAbstractSocket::LowDelayOption, 1 );

		const int id = m_nextId++;
		Client client;
		client.socket = socket;
		client.helloTimer = new QTimer( this );
		client.helloTimer->setSingleShot( true );
		connect( client.helloTimer, &QTimer::timeout, this, [this, id]() {
			if( m_clients.contains( id ) && m_clients[id].authenticated == false )
			{
				vWarning() << "LabAudio client did not authenticate in time";
				dropClient( id );
			}
		} );
		client.helloTimer->start( HelloTimeoutMs );
		m_clients.insert( id, client );

		connect( socket, &QTcpSocket::readyRead, this, [this, id]() { readClient( id ); } );
		connect( socket, &QTcpSocket::disconnected, this, [this, id]() { dropClient( id ); } );
	}
}



void LabAudioHub::readClient( int id )
{
	auto it = m_clients.find( id );
	if( it == m_clients.end() || it->socket.isNull() )
	{
		return;
	}
	it->reader.append( it->socket->readAll() );

	FrameType type;
	QByteArray payload;
	bool error = false;
	while( m_clients.contains( id ) && m_clients[id].reader.next( type, payload, error ) )
	{
		auto& client = m_clients[id];
		if( client.authenticated == false )
		{
			if( type != FrameType::Hello )
			{
				dropClient( id );
				return;
			}
			handleHello( id, payload );
			continue;
		}

		if( type == FrameType::Audio && ( client.roles & SendMicrophone ) && payload.size() > 4 &&
			payload.size() <= 4 + MaxFrameBytes && m_playback )
		{
			const auto sequence = qFromBigEndian<quint32>( payload.constData() );
			m_playback->mixer().addPacket( id, sequence, payload.mid( 4 ) );
		}
		else if( type == FrameType::Bye )
		{
			dropClient( id );
			return;
		}
	}
	if( error )
	{
		vWarning() << "LabAudio client sent an invalid frame";
		dropClient( id );
	}
}



void LabAudioHub::handleHello( int id, const QByteArray& payload )
{
	auto& client = m_clients[id];
	bool accepted = false;
	if( payload.size() >= 4 && quint8( payload.at( 0 ) ) == Version )
	{
		const auto roles = quint8( payload.at( 1 ) );
		const auto tokenLength = qFromBigEndian<quint16>( payload.constData() + 2 );
		const auto token = payload.mid( 4, tokenLength );
		if( payload.size() == 4 + tokenLength && constantTimeEquals( token, m_token ) )
		{
			client.roles = roles & ( ReceiveTeacher | SendMicrophone );
			client.authenticated = true;
			accepted = true;
		}
	}

	client.socket->write( frame( FrameType::HelloAck, QByteArray( 1, accepted ? 1 : 0 ) ) );
	if( accepted == false )
	{
		vWarning() << "LabAudio client rejected: invalid token or version";
		client.socket->flush();
		dropClient( id );
		return;
	}
	client.helloTimer->stop();
	vInfo() << "LabAudio client" << client.socket->peerAddress().toString() << "joined with roles" << client.roles;
	updateAudio();
}



void LabAudioHub::dropClient( int id )
{
	auto it = m_clients.find( id );
	if( it == m_clients.end() )
	{
		return;
	}
	auto client = it.value();
	m_clients.erase( it );
	if( client.helloTimer )
	{
		client.helloTimer->deleteLater();
	}
	if( client.socket )
	{
		client.socket->disconnect( this );
		client.socket->abort();
		client.socket->deleteLater();
	}
	if( m_playback )
	{
		m_playback->mixer().removeStream( id );
	}
	updateAudio();
}



void LabAudioHub::updateAudio()
{
	bool anyReceiver = false;
	bool anySender = false;
	for( const auto& client : std::as_const( m_clients ) )
	{
		if( client.authenticated )
		{
			anyReceiver |= ( client.roles & ReceiveTeacher ) != 0;
			anySender |= ( client.roles & SendMicrophone ) != 0;
		}
	}

	if( anyReceiver && m_capture == nullptr )
	{
		m_capture = new LabAudioCapture( this );
		connect( m_capture, &LabAudioCapture::packetReady, this, &LabAudioHub::sendTeacherPacket );
		if( m_capture->start() == false )
		{
			vCritical() << "LabAudio: teacher microphone unavailable:" << m_capture->errorString();
		}
	}
	else if( anyReceiver == false && m_capture )
	{
		delete m_capture;
		m_capture = nullptr;
	}

	if( anySender && m_playback == nullptr )
	{
		m_playback = new LabAudioPlayback( this );
		if( m_playback->start() == false )
		{
			vCritical() << "LabAudio: teacher headset unavailable:" << m_playback->errorString();
		}
	}
	else if( anySender == false && m_playback )
	{
		delete m_playback;
		m_playback = nullptr;
	}
}



void LabAudioHub::sendTeacherPacket( const QByteArray& packet )
{
	const auto data = frame( FrameType::Audio, audioPayload( m_sequence++, packet ) );
	for( auto& client : m_clients )
	{
		if( client.authenticated && ( client.roles & ReceiveTeacher ) && client.socket &&
			client.socket->bytesToWrite() < MaxQueuedBytes )
		{
			client.socket->write( data );
		}
	}
}
