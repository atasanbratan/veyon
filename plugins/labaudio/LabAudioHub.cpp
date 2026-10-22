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

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>
#include <QtEndian>

#include <algorithm>

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



LabAudioCallsWindow::LabAudioCallsWindow() :
	QWidget( nullptr, Qt::Tool | Qt::WindowStaysOnTopHint ),
	m_rows( new QVBoxLayout )
{
	setWindowTitle( tr( "Students calling" ) );
	setMinimumWidth( 360 );
	auto layout = new QVBoxLayout( this );
	layout->addLayout( m_rows );
}



void LabAudioCallsWindow::setCalls( const QList<LabAudioCall>& calls )
{
	while( auto item = m_rows->takeAt( 0 ) )
	{
		if( item->widget() )
		{
			item->widget()->deleteLater();
		}
		delete item;
	}

	for( const auto& call : calls )
	{
		auto row = new QWidget( this );
		auto rowLayout = new QHBoxLayout( row );
		rowLayout->setContentsMargins( 0, 0, 0, 0 );
		auto text = call.name.toHtmlEscaped();
		if( call.group > 0 )
		{
			text += tr( " · group %1" ).arg( call.group );
		}
		auto label = new QLabel( QStringLiteral( "✋ <b>%1</b> &nbsp; %2" )
									 .arg( text, call.time.toString( QStringLiteral( "HH:mm" ) ) ), row );
		auto done = new QPushButton( tr( "Done" ), row );
		done->setToolTip( tr( "Answer the call: the student's button is reset." ) );
		const auto clientId = call.clientId;
		connect( done, &QPushButton::clicked, this, [this, clientId]() { Q_EMIT answered( clientId ); } );
		rowLayout->addWidget( label, 1 );
		rowLayout->addWidget( done );
		m_rows->addWidget( row );
	}

	if( calls.isEmpty() )
	{
		hide();
	}
	else
	{
		adjustSize();
		if( isVisible() == false )
		{
			if( const auto screen = QGuiApplication::primaryScreen() )
			{
				const auto area = screen->availableGeometry();
				move( area.right() - width() - 20, area.top() + 60 );
			}
			show();
		}
		if( calls.size() > m_shownCalls )
		{
			// A new call: draw attention without stealing the teacher's keyboard focus.
			QApplication::beep();
			QApplication::alert( this );
			raise();
		}
	}
	m_shownCalls = int( calls.size() );
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
		vWarning() << "LabAudio hub listening on port" << port << "token" << tokenFingerprint( m_token );
	}
}



LabAudioHub::~LabAudioHub()
{
	m_callsWindowEnabled = false;
	const auto ids = m_clients.keys();
	for( const auto id : ids )
	{
		dropClient( id );
	}
	delete m_callsWindow;
}



void LabAudioHub::setTeacherMuted( bool muted )
{
	m_teacherMuted = muted;
	if( m_capture )
	{
		m_capture->setMuted( muted );
	}
}



QList<LabAudioCall> LabAudioHub::calls() const
{
	QList<LabAudioCall> calls;
	for( auto it = m_clients.cbegin(); it != m_clients.cend(); ++it )
	{
		if( it->authenticated && it->callTime.isValid() )
		{
			calls.append( { it.key(), it->callName, it->group, it->callTime } );
		}
	}
	std::sort( calls.begin(), calls.end(), []( const LabAudioCall& a, const LabAudioCall& b ) { return a.time < b.time; } );
	return calls;
}



void LabAudioHub::answerCall( int clientId )
{
	auto it = m_clients.find( clientId );
	if( it == m_clients.end() || it->callTime.isValid() == false )
	{
		return;
	}
	it->callTime = {};
	sendToClient( it.value(), FrameType::CallTeacher, QByteArray( 1, 0 ) );
	updateCalls();
}



void LabAudioHub::handleCall( int id, const QByteArray& payload )
{
	auto& client = m_clients[id];
	const bool calling = payload.at( 0 ) != 0;
	if( calling && client.callTime.isValid() == false )
	{
		client.callName = QString::fromUtf8( payload.mid( 1, MaxNameBytes ) ).trimmed();
		if( client.callName.isEmpty() )
		{
			client.callName = client.socket ? client.socket->peerAddress().toString() : tr( "A student" );
		}
		client.callTime = QDateTime::currentDateTime();
		vInfo() << "LabAudio: student" << client.callName << "is calling the teacher";
	}
	else if( calling == false )
	{
		client.callTime = {};
	}
	updateCalls();
}



void LabAudioHub::updateCalls()
{
	const auto current = calls();
	if( m_callsWindowEnabled && ( current.isEmpty() == false || m_callsWindow ) )
	{
		if( m_callsWindow.isNull() )
		{
			m_callsWindow = new LabAudioCallsWindow;
			connect( m_callsWindow, &LabAudioCallsWindow::answered, this, &LabAudioHub::answerCall );
		}
		m_callsWindow->setCalls( current );
	}
	Q_EMIT callsChanged( int( current.size() ) );
}



bool LabAudioHub::isListening() const
{
	return m_server->isListening();
}



void LabAudioHub::setToken( const QByteArray& token )
{
	if( token != m_token )
	{
		vWarning() << "LabAudio hub token changed to" << tokenFingerprint( token );
	}
	m_token = token;
}



void LabAudioHub::setGroups( const QHash<QByteArray, int>& groupTokens )
{
	if( groupTokens == m_groupTokens )
	{
		return;
	}
	vWarning() << "LabAudio hub: group work with" << groupTokens.size() << "groups";
	m_groupTokens = groupTokens;

	// Regrouping or ending group work: members of old groups have to reconnect with their new token.
	const auto ids = m_clients.keys();
	for( const auto id : ids )
	{
		const auto client = m_clients.value( id );
		if( client.authenticated && client.group != ClassGroup && m_groupTokens.value( client.token ) != client.group )
		{
			dropClient( id );
		}
	}
	updateAudio();
}



void LabAudioHub::setFocus( int focusGroup, bool teacherTalks )
{
	if( focusGroup <= ClassGroup )
	{
		focusGroup = NoGroup;
		teacherTalks = false;
	}
	if( focusGroup == m_focusGroup && teacherTalks == m_teacherTalks )
	{
		return;
	}
	const auto previousGroup = m_focusGroup;
	m_focusGroup = focusGroup;
	m_teacherTalks = teacherTalks;

	const auto ids = m_clients.keys();
	for( const auto id : ids )
	{
		const auto client = m_clients.value( id );
		if( client.authenticated == false )
		{
			continue;
		}
		if( client.group == previousGroup || client.group == m_focusGroup )
		{
			sendStatus( id );
		}
		// Drop what was buffered for a group the teacher no longer hears.
		if( m_playback && teacherHears( client ) == false )
		{
			m_playback->mixer().removeStream( id );
		}
	}
	updateAudio();
}



bool LabAudioHub::hearsTeacher( const Client& client ) const
{
	if( client.authenticated == false || ( client.roles & ReceiveTeacher ) == 0 )
	{
		return false;
	}
	return client.group == ClassGroup || ( client.group == m_focusGroup && m_teacherTalks );
}



bool LabAudioHub::teacherHears( const Client& client ) const
{
	if( client.authenticated == false || ( client.roles & SendMicrophone ) == 0 )
	{
		return false;
	}
	return client.group == ClassGroup || client.group == m_focusGroup;
}



void LabAudioHub::sendStatus( int id )
{
	auto& client = m_clients[id];
	if( client.socket.isNull() || client.group == ClassGroup )
	{
		return;
	}
	quint8 flags = 0;
	if( client.group == m_focusGroup )
	{
		flags |= TeacherListening;
		if( m_teacherTalks )
		{
			flags |= TeacherTalking;
		}
	}
	sendToClient( client, FrameType::Status, QByteArray( 1, char( flags ) ) );
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
		if( client.session.isReady() == false )
		{
			if( type != FrameType::Hello )
			{
				dropClient( id );
				return;
			}
			handleHello( id, payload );
			continue;
		}

		// After the hello, only authentic encrypted frames are accepted.
		FrameType innerType;
		QByteArray inner;
		if( type != FrameType::Encrypted || client.session.open( payload, innerType, inner ) == false )
		{
			vWarning() << "LabAudio client" << ( client.socket ? client.socket->peerAddress().toString() : QString() )
					   << "sent a frame that is not authentic - disconnecting";
			dropClient( id );
			return;
		}
		type = innerType;
		payload = inner;

		if( client.authenticated == false )
		{
			if( type != FrameType::Auth || payload.size() != 1 )
			{
				dropClient( id );
				return;
			}
			handleAuth( id, quint8( payload.at( 0 ) ) );
			continue;
		}

		if( type == FrameType::Audio && ( client.roles & SendMicrophone ) && payload.size() > 4 &&
			payload.size() <= 4 + MaxFrameBytes )
		{
			if( m_playback && teacherHears( client ) )
			{
				const auto sequence = qFromBigEndian<quint32>( payload.constData() );
				m_playback->mixer().addPacket( id, sequence, payload.mid( 4 ) );
				Q_EMIT studentPacketReceived( id );
			}
			if( client.group != ClassGroup )
			{
				forwardStudentAudio( id, payload );
			}
		}
		else if( type == FrameType::CallTeacher && payload.size() >= 1 && payload.size() <= 1 + MaxNameBytes )
		{
			handleCall( id, payload );
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
	constexpr int HelloSize = 1 + LabAudioSession::KeyIdSize + LabAudioSession::NonceSize;
	QString reason = QStringLiteral( "malformed hello" );
	QByteArray token;
	int group = NoGroup;
	if( payload.size() >= 1 && quint8( payload.at( 0 ) ) != Version )
	{
		reason = QStringLiteral( "protocol version %1, expected %2 (update Veyon on this computer)" )
					 .arg( quint8( payload.at( 0 ) ) ).arg( Version );
	}
	else if( payload.size() == HelloSize )
	{
		// The client names its token only by its public key id; the token itself never travels.
		const auto keyId = payload.mid( 1, LabAudioSession::KeyIdSize );
		reason = QStringLiteral( "unknown key (outdated mode or wrong teacher)" );
		if( constantTimeEquals( keyId, LabAudioSession::keyId( m_token ) ) )
		{
			token = m_token;
			group = ClassGroup;
		}
		for( auto it = m_groupTokens.cbegin(); it != m_groupTokens.cend(); ++it )
		{
			if( constantTimeEquals( keyId, LabAudioSession::keyId( it.key() ) ) )
			{
				token = it.key();
				group = it.value();
			}
		}
	}

	const auto hubNonce = LabAudioSession::randomNonce();
	if( group == NoGroup || hubNonce.isEmpty() ||
		client.session.init( token, payload.mid( 1 + LabAudioSession::KeyIdSize ), hubNonce,
							 LabAudioSession::Role::Hub ) == false )
	{
		client.socket->write( frame( FrameType::HelloAck, QByteArray( 1, 0 ) ) );
		vWarning() << "LabAudio client" << client.socket->peerAddress().toString() << "rejected:" << reason;
		client.socket->flush();
		dropClient( id );
		return;
	}
	client.group = group;
	client.token = token;
	client.socket->write( frame( FrameType::HelloAck, QByteArray( 1, 1 ) + hubNonce ) );
	// Not yet authenticated: the client still has to prove that it knows the token (Auth frame).
}



void LabAudioHub::handleAuth( int id, quint8 roles )
{
	auto& client = m_clients[id];
	client.roles = roles & ( ReceiveTeacher | SendMicrophone | ReceivePeers );
	client.authenticated = true;
	client.helloTimer->stop();
	vInfo() << "LabAudio client" << client.socket->peerAddress().toString() << "joined group" << client.group
			<< "with roles" << client.roles;
	sendStatus( id );
	updateAudio();
}



void LabAudioHub::sendToClient( Client& client, FrameType type, const QByteArray& payload )
{
	if( client.authenticated && client.socket && client.session.isReady() )
	{
		client.socket->write( client.session.seal( type, payload ) );
	}
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
	if( client.callTime.isValid() )
	{
		updateCalls();
	}
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
		anyReceiver |= hearsTeacher( client );
		anySender |= teacherHears( client );
	}

	if( anyReceiver && m_capture == nullptr )
	{
		m_capture = new LabAudioCapture( this );
		m_capture->setMuted( m_teacherMuted );
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
	const auto payload = audioPayload( m_sequence++, packet );
	int receivers = 0;
	for( auto& client : m_clients )
	{
		if( hearsTeacher( client ) && client.socket && client.socket->bytesToWrite() < MaxQueuedBytes )
		{
			// Encrypted for each student with that connection's key.
			sendToClient( client, FrameType::Audio, payload );
			++receivers;
		}
	}
	Q_EMIT teacherPacketSent( receivers );
}



void LabAudioHub::forwardStudentAudio( int sourceId, const QByteArray& audioPayload )
{
	const auto group = m_clients.value( sourceId ).group;
	QByteArray data;
	int receivers = 0;
	for( auto it = m_clients.begin(); it != m_clients.end(); ++it )
	{
		auto& client = it.value();
		if( it.key() == sourceId || client.authenticated == false || client.group != group ||
			( client.roles & ReceivePeers ) == 0 || client.socket.isNull() ||
			client.socket->bytesToWrite() >= MaxQueuedBytes )
		{
			continue;
		}
		if( data.isEmpty() )
		{
			data = peerAudioPayload( quint32( sourceId ), audioPayload );
		}
		// Decrypted from the speaker, re-encrypted for each listener with that listener's key.
		sendToClient( client, FrameType::PeerAudio, data );
		++receivers;
	}
	Q_EMIT peerPacketForwarded( sourceId, receivers );
}
