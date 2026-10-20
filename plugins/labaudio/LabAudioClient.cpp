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
#include <QLabel>
#include <QProgressBar>
#include <QScreen>
#include <QTcpSocket>
#include <QVBoxLayout>
#include <QtEndian>

#include "LabAudioClient.h"
#include "LabAudioEngine.h"
#include "Logger.h"

using namespace LabAudioProtocol;



LabAudioIndicator::LabAudioIndicator( quint8 roles ) :
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
	if( ( roles & ReceiveTeacher ) && ( roles & SendMicrophone ) )
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

	adjustSize();
	if( const auto screen = QGuiApplication::primaryScreen() )
	{
		const auto area = screen->availableGeometry();
		move( area.right() - width() - 20, area.bottom() - height() - 20 );
	}
	show();
}



void LabAudioIndicator::setStatus( const QString& status )
{
	m_status->setText( status );
	adjustSize();
}



void LabAudioIndicator::setLevel( float level )
{
	if( m_level )
	{
		m_level->setValue( qRound( qBound( 0.0f, level, 1.0f ) * 100 ) );
	}
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



LabAudioClient::LabAudioClient( const QString& host, int port, const QByteArray& token, quint8 roles, QObject* parent ) :
	QObject( parent ),
	m_host( host ),
	m_port( port ),
	m_token( token ),
	m_roles( roles ),
	m_socket( new QTcpSocket( this ) ),
	m_indicator( new LabAudioIndicator( roles ) )
{
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
	m_reconnectTimer.setInterval( ReconnectIntervalMs );
	connect( &m_reconnectTimer, &QTimer::timeout, this, &LabAudioClient::connectToHub );

	connectToHub();
}



LabAudioClient::~LabAudioClient()
{
	m_reconnectTimer.stop();
	if( m_socket->state() == QAbstractSocket::ConnectedState )
	{
		m_socket->write( frame( FrameType::Bye, {} ) );
		m_socket->flush();
	}
	m_socket->disconnect( this );
	m_socket->abort();
	stopAudio();
	delete m_indicator;
}



void LabAudioClient::connectToHub()
{
	if( m_socket->state() != QAbstractSocket::UnconnectedState )
	{
		return;
	}
	vDebug() << "LabAudio: connecting to" << m_host << m_port;
	m_reader = {};
	m_accepted = false;
	m_socket->connectToHost( m_host, quint16( m_port ) );
}



void LabAudioClient::onConnected()
{
	m_socket->setSocketOption( QAbstractSocket::LowDelayOption, 1 );
	QByteArray hello;
	hello.append( char( Version ) );
	hello.append( char( m_roles ) );
	char length[2];
	qToBigEndian( quint16( m_token.size() ), length );
	hello.append( length, 2 );
	hello.append( m_token );
	m_socket->write( frame( FrameType::Hello, hello ) );
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
			if( type == FrameType::HelloAck && payload.size() == 1 && payload.at( 0 ) == 1 )
			{
				m_accepted = true;
				startAudio();
			}
			else
			{
				vWarning() << "LabAudio: hub rejected the connection";
				m_indicator->setStatus( tr( "The connection to the teacher was refused." ) );
				m_socket->abort();
				return;
			}
			continue;
		}
		if( type == FrameType::Audio && ( m_roles & ReceiveTeacher ) && m_playback && payload.size() > 4 &&
			payload.size() <= 4 + MaxFrameBytes )
		{
			m_playback->mixer().addPacket( 0, qFromBigEndian<quint32>( payload.constData() ), payload.mid( 4 ) );
			Q_EMIT audioReceived( int( payload.size() ) );
		}
	}
	if( error )
	{
		m_socket->abort();
	}
}



void LabAudioClient::onDisconnected()
{
	stopAudio();
	m_indicator->setStatus( tr( "Connection to the teacher lost. Reconnecting…" ) );
	if( m_reconnectTimer.isActive() == false )
	{
		m_reconnectTimer.start();
	}
}



void LabAudioClient::startAudio()
{
	QStringList problems;
	if( ( m_roles & ReceiveTeacher ) && m_playback == nullptr )
	{
		m_playback = new LabAudioPlayback( this );
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
		m_socket->write( frame( FrameType::Audio, audioPayload( m_sequence++, packet ) ) );
	}
}
