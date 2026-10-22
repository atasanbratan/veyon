/*
 * LabAudioClient.cpp - audio client running in a student's session
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

#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QTcpSocket>
#include <QVBoxLayout>
#include <QtEndian>

#include "LabAudioClient.h"
#include "LabAudioEngine.h"
#include "Logger.h"

using namespace LabAudioProtocol;



LabAudioIndicator::LabAudioIndicator( quint8 roles, const QStringList& groupMembers ) :
	QWidget( nullptr, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus ),
	m_title( new QLabel( this ) ),
	m_status( new QLabel( this ) )
{
	setAttribute( Qt::WA_ShowWithoutActivating );
	setObjectName( QStringLiteral( "LabAudioIndicator" ) );
	setStyleSheet( QStringLiteral( "#LabAudioIndicator { background: #1f2937; border: 2px solid #f59e0b; border-radius: 8px; }"
								   "QLabel { color: white; }" ) );

	auto layout = new QVBoxLayout( this );
	layout->setContentsMargins( 14, 10, 14, 10 );

	QString title;
	if( roles & ReceivePeers )
	{
		title = tr( "Group work" );
	}
	else if( ( roles & ReceiveTeacher ) && ( roles & SendMicrophone ) )
	{
		title = tr( "Talking with the teacher" );
	}
	else if( roles & SendMicrophone )
	{
		title = tr( "The teacher can hear your microphone" );
	}
	else
	{
		title = tr( "The teacher is speaking" );
	}
	m_title->setText( QStringLiteral( "<b>%1</b>" ).arg( title.toHtmlEscaped() ) );
	layout->addWidget( m_title );
	if( roles & ReceivePeers )
	{
		auto members = new QLabel( groupMembers.isEmpty()
									   ? tr( "You are talking with your group." )
									   : tr( "You are talking with: %1" ).arg( groupMembers.join( QStringLiteral( ", " ) ) ),
								   this );
		members->setWordWrap( true );
		members->setMaximumWidth( 360 );
		layout->addWidget( members );
		// Students always see when the teacher is listening to their group.
		m_teacher = new QLabel( this );
		m_teacher->setStyleSheet( QStringLiteral( "color: #fbbf24;" ) );
		m_teacher->hide();
		layout->addWidget( m_teacher );
	}
	layout->addWidget( m_status );
	m_status->setText( tr( "Connecting…" ) );

	if( roles & SendMicrophone )
	{
		m_level = new QProgressBar( this );
		m_level->setRange( 0, 100 );
		m_level->setTextVisible( false );
		m_level->setMaximumHeight( 8 );
		m_level->setAccessibleName( tr( "Microphone level" ) );
		layout->addWidget( m_level );
	}

	auto controls = new QHBoxLayout;
	if( roles & ( ReceiveTeacher | ReceivePeers ) )
	{
		auto volumeLabel = new QLabel( tr( "Volume" ), this );
		m_volume = new QSlider( Qt::Horizontal, this );
		m_volume->setRange( 0, 200 );
		m_volume->setPageStep( 10 );
		m_volume->setValue( savedVolume() );
		m_volume->setToolTip( tr( "How loud you hear the teacher and your group" ) );
		m_volume->setAccessibleName( tr( "Volume" ) );
		connect( m_volume, &QSlider::valueChanged, this, [this]( int percent ) {
			QSettings( QStringLiteral( "Veyon Solutions" ), QStringLiteral( "LabAudio" ) )
				.setValue( QStringLiteral( "volume" ), percent );
			Q_EMIT volumeChanged( percent );
		} );
		controls->addWidget( volumeLabel );
		controls->addWidget( m_volume, 1 );
	}
	m_call = new QPushButton( this );
	m_call->setCheckable( true );
	m_call->setFocusPolicy( Qt::NoFocus );
	m_call->setStyleSheet( QStringLiteral( "QPushButton { color: white; background: #374151; border: 1px solid #6b7280;"
										   " border-radius: 4px; padding: 4px 10px; }"
										   "QPushButton:checked { background: #b45309; border-color: #f59e0b; }" ) );
	connect( m_call, &QPushButton::toggled, this, [this]( bool calling ) {
		updateCallButton();
		Q_EMIT callToggled( calling );
	} );
	controls->addWidget( m_call );
	layout->addLayout( controls );
	updateCallButton();

	placeOnScreen();
	show();
}



int LabAudioIndicator::savedVolume()
{
	return qBound( 0, QSettings( QStringLiteral( "Veyon Solutions" ), QStringLiteral( "LabAudio" ) )
						  .value( QStringLiteral( "volume" ), 100 ).toInt(), 200 );
}



void LabAudioIndicator::setCallAnswered()
{
	// The teacher pressed "Done": reset without sending the change back.
	const QSignalBlocker blocker( m_call );
	m_call->setChecked( false );
	updateCallButton();
}



void LabAudioIndicator::updateCallButton()
{
	m_call->setText( m_call->isChecked() ? tr( "✋ Calling the teacher… (cancel)" ) : tr( "✋ Call the teacher" ) );
	placeOnScreen();
}



void LabAudioIndicator::placeOnScreen()
{
	adjustSize();
	if( const auto screen = QGuiApplication::primaryScreen() )
	{
		const auto area = screen->availableGeometry();
		move( area.right() - width() - 20, area.bottom() - height() - 20 );
	}
}



void LabAudioIndicator::setStatus( const QString& status )
{
	m_status->setText( status );
	placeOnScreen();
}



void LabAudioIndicator::setLevel( float level )
{
	if( m_level )
	{
		m_level->setValue( qRound( qBound( 0.0f, level, 1.0f ) * 100 ) );
	}
}



void LabAudioIndicator::setTeacherStatus( quint8 statusFlags )
{
	if( m_teacher == nullptr )
	{
		return;
	}
	if( statusFlags & TeacherTalking )
	{
		m_teacher->setText( tr( "The teacher has joined your group." ) );
		m_teacher->show();
	}
	else if( statusFlags & TeacherListening )
	{
		m_teacher->setText( tr( "The teacher is listening to your group." ) );
		m_teacher->show();
	}
	else
	{
		m_teacher->hide();
	}
	placeOnScreen();
}



void LabAudioIndicator::closeEvent( QCloseEvent* event )
{
	// The student cannot dismiss the notice while the teacher keeps the audio mode active;
	// programmatic closes (application shutdown) are allowed.
	if( event->spontaneous() )
	{
		event->ignore();
	}
	else
	{
		event->accept();
	}
}



LabAudioClient::LabAudioClient( const QStringList& hosts, int port, const QByteArray& token, quint8 roles,
								const QStringList& groupMembers, const QString& studentName, QObject* parent ) :
	QObject( parent ),
	m_hosts( hosts ),
	m_port( port ),
	m_token( token ),
	m_roles( roles ),
	m_studentName( studentName ),
	m_socket( new QTcpSocket( this ) ),
	m_indicator( new LabAudioIndicator( roles, groupMembers ) )
{
	connect( this, &LabAudioClient::teacherStatusChanged, m_indicator, &LabAudioIndicator::setTeacherStatus );
	connect( this, &LabAudioClient::callAnswered, m_indicator, &LabAudioIndicator::setCallAnswered );
	connect( m_indicator, &LabAudioIndicator::callToggled, this, &LabAudioClient::setCalling );
	connect( m_indicator, &LabAudioIndicator::volumeChanged, this, [this]( int percent ) {
		if( m_playback )
		{
			m_playback->mixer().setGain( float( percent ) / 100.0f );
		}
	} );
	connect( m_socket, &QTcpSocket::connected, this, &LabAudioClient::onConnected );
	connect( m_socket, &QTcpSocket::readyRead, this, &LabAudioClient::onReadyRead );
	connect( m_socket, &QTcpSocket::disconnected, this, &LabAudioClient::onDisconnected );
	connect( m_socket, &QTcpSocket::errorOccurred, this, [this]( QAbstractSocket::SocketError ) {
		if( m_socket->state() != QAbstractSocket::ConnectedState )
		{
			onDisconnected();
		}
	} );

	m_reconnectTimer.setSingleShot( true );
	connect( &m_reconnectTimer, &QTimer::timeout, this, &LabAudioClient::connectToHub );

	// A firewall that silently drops packets would otherwise block each attempt for ~20 s.
	m_connectTimer.setSingleShot( true );
	m_connectTimer.setInterval( ConnectTimeoutMs );
	connect( &m_connectTimer, &QTimer::timeout, this, [this]() {
		if( m_socket->state() != QAbstractSocket::ConnectedState )
		{
			m_failReason = tr( "no answer within %1 seconds (blocked by a firewall?)" ).arg( ConnectTimeoutMs / 1000 );
			m_socket->abort();
			onDisconnected();
		}
	} );

	m_hosts.removeAll( QString() );
	m_hosts.removeDuplicates();
	if( m_hosts.isEmpty() )
	{
		m_indicator->setStatus( tr( "The address of the teacher's computer is unknown." ) );
		return;
	}

	connectToHub();
}



LabAudioClient::~LabAudioClient()
{
	m_reconnectTimer.stop();
	m_connectTimer.stop();
	if( m_accepted && m_socket->state() == QAbstractSocket::ConnectedState )
	{
		send( FrameType::Bye, {} );
		m_socket->flush();
	}
	m_socket->disconnect( this );
	m_socket->abort();
	stopAudio();
	delete m_indicator;
}



void LabAudioClient::setCalling( bool calling )
{
	m_calling = calling;
	if( m_accepted )
	{
		QByteArray payload( 1, calling ? 1 : 0 );
		if( calling )
		{
			payload.append( m_studentName.toUtf8().left( MaxNameBytes ) );
		}
		send( FrameType::CallTeacher, payload );
	}
}



void LabAudioClient::connectToHub()
{
	if( m_socket->state() != QAbstractSocket::UnconnectedState )
	{
		return;
	}
	vDebug() << "LabAudio: connecting to" << currentHost() << m_port;
	m_reader = {};
	m_accepted = false;
	m_wasConnected = false;
	m_failReason.clear();
	m_attemptActive = true;
	m_connectTimer.start();
	m_socket->connectToHost( currentHost(), quint16( m_port ) );
}



QString LabAudioClient::currentHost() const
{
	return m_hosts.value( m_hostIndex % qMax( 1, int( m_hosts.size() ) ) );
}



void LabAudioClient::onConnected()
{
	m_connectTimer.stop();
	m_wasConnected = true;
	m_socket->setSocketOption( QAbstractSocket::LowDelayOption, 1 );
	// Names the token only by its key id; the token itself never leaves this computer.
	m_session = {};
	m_clientNonce = LabAudioSession::randomNonce();
	QByteArray hello( 1, char( Version ) );
	hello += LabAudioSession::keyId( m_token );
	hello += m_clientNonce;
	m_socket->write( frame( FrameType::Hello, hello ) );
}



void LabAudioClient::send( FrameType type, const QByteArray& payload )
{
	if( m_session.isReady() && m_socket->state() == QAbstractSocket::ConnectedState )
	{
		m_socket->write( m_session.seal( type, payload ) );
	}
}



void LabAudioClient::onReadyRead()
{
	m_reader.append( m_socket->readAll() );
	FrameType type;
	QByteArray payload;
	bool error = false;
	while( m_reader.next( type, payload, error ) )
	{
		if( m_accepted == false )
		{
			if( type == FrameType::HelloAck && payload.size() == 1 + LabAudioSession::NonceSize && payload.at( 0 ) == 1 &&
				m_session.init( m_token, m_clientNonce, payload.mid( 1 ), LabAudioSession::Role::Client ) )
			{
				m_accepted = true;
				// The first encrypted frame proves to the hub that this client knows the token.
				send( FrameType::Auth, QByteArray( 1, char( m_roles ) ) );
				startAudio();
				if( m_calling )
				{
					// A call survives a reconnect.
					setCalling( true );
				}
			}
			else
			{
				vWarning() << "LabAudio: hub at" << currentHost() << "rejected the connection (outdated mode, wrong token "
							  "or different plugin version)";
				m_indicator->setStatus( tr( "The connection to the teacher was refused. Is the same Veyon version "
											"installed on this computer and the teacher's computer?" ) );
				// No automatic retry: the teacher has to start the audio mode again.
				m_attemptActive = false;
				m_connectTimer.stop();
				m_socket->abort();
				return;
			}
			continue;
		}

		FrameType innerType;
		QByteArray inner;
		if( type != FrameType::Encrypted || m_session.open( payload, innerType, inner ) == false )
		{
			// Only the real teacher computer (which knows the token) can produce authentic frames.
			vWarning() << "LabAudio: frame from" << currentHost() << "is not authentic - reconnecting";
			m_socket->abort();
			onDisconnected();
			return;
		}
		type = innerType;
		payload = inner;

		if( type == FrameType::Audio && ( m_roles & ReceiveTeacher ) && m_playback && payload.size() > 4 &&
			payload.size() <= 4 + MaxFrameBytes )
		{
			m_playback->mixer().addPacket( TeacherStreamId, qFromBigEndian<quint32>( payload.constData() ), payload.mid( 4 ) );
			Q_EMIT audioReceived( int( payload.size() ) );
		}
		else if( type == FrameType::PeerAudio && ( m_roles & ReceivePeers ) && m_playback && payload.size() > 8 &&
				 payload.size() <= 8 + MaxFrameBytes )
		{
			const auto sourceId = int( qFromBigEndian<quint32>( payload.constData() ) );
			if( sourceId != TeacherStreamId )
			{
				m_playback->mixer().addPacket( sourceId, qFromBigEndian<quint32>( payload.constData() + 4 ),
											   payload.mid( 8 ) );
				Q_EMIT peerAudioReceived( sourceId );
			}
		}
		else if( type == FrameType::Status && payload.size() == 1 )
		{
			Q_EMIT teacherStatusChanged( quint8( payload.at( 0 ) ) );
		}
		else if( type == FrameType::CallTeacher && payload.size() == 1 && payload.at( 0 ) == 0 )
		{
			m_calling = false;
			Q_EMIT callAnswered();
		}
	}
	if( error )
	{
		m_socket->abort();
	}
}



void LabAudioClient::onDisconnected()
{
	// errorOccurred, disconnected and the connect timeout can all report the same failed attempt.
	if( m_attemptActive == false )
	{
		return;
	}
	m_attemptActive = false;
	m_connectTimer.stop();
	stopAudio();

	const auto host = currentHost();
	const auto reason = m_failReason.isEmpty() ? m_socket->errorString() : m_failReason;
	if( m_wasConnected && m_accepted )
	{
		// The route worked: retry the same address after a pause.
		vWarning() << "LabAudio: connection to" << host << m_port << "lost:" << reason;
		m_indicator->setStatus( tr( "Connection to the teacher lost. Reconnecting…" ) );
		m_reconnectTimer.start( ReconnectIntervalMs );
		return;
	}

	vWarning() << "LabAudio: cannot reach the teacher's audio hub at" << host << m_port << "-" << reason;
	m_indicator->setStatus( tr( "Cannot reach the teacher's computer (%1, port %2): %3. Retrying…" )
								.arg( host ).arg( m_port ).arg( reason ) );
	// Try the teacher's next address quickly; pause after a full round.
	++m_hostIndex;
	const bool roundComplete = ( m_hostIndex % int( m_hosts.size() ) ) == 0;
	m_reconnectTimer.start( roundComplete ? ReconnectIntervalMs : NextAddressDelayMs );
}



void LabAudioClient::startAudio()
{
	QStringList problems;
	if( ( m_roles & ( ReceiveTeacher | ReceivePeers ) ) && m_playback == nullptr )
	{
		m_playback = new LabAudioPlayback( this );
		m_playback->mixer().setGain( float( LabAudioIndicator::savedVolume() ) / 100.0f );
		if( m_playback->start() == false )
		{
			problems.append( m_playback->errorString() );
		}
	}
	if( ( m_roles & SendMicrophone ) && m_capture == nullptr )
	{
		m_capture = new LabAudioCapture( this );
		connect( m_capture, &LabAudioCapture::packetReady, this, &LabAudioClient::sendPacket );
		connect( m_capture, &LabAudioCapture::levelChanged, m_indicator, &LabAudioIndicator::setLevel );
		connect( m_capture, &LabAudioCapture::failed, m_indicator, &LabAudioIndicator::setStatus );
		if( m_capture->start() == false )
		{
			problems.append( m_capture->errorString() );
		}
	}
	m_indicator->setStatus( problems.isEmpty() ? tr( "Connected" ) : problems.join( QLatin1Char( '\n' ) ) );
}



void LabAudioClient::stopAudio()
{
	delete m_capture;
	m_capture = nullptr;
	delete m_playback;
	m_playback = nullptr;
}



void LabAudioClient::sendPacket( const QByteArray& packet )
{
	if( m_accepted && m_socket->state() == QAbstractSocket::ConnectedState && m_socket->bytesToWrite() < 64 * 1024 )
	{
		send( FrameType::Audio, audioPayload( m_sequence++, packet ) );
	}
}
