/*
 * labaudio_grouptest.cpp - checks group work routing of the teacher hub with simulated students
 * (raw TCP connections, no audio devices): group members hear each other, other groups and the
 * class do not, students see when the teacher listens, and regrouping disconnects old groups.
 *
 * Copyright (c) 2026 TDMUGI
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QApplication>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QtEndian>
#include <cstdio>
#include <cstdlib>

#include "../LabAudioGroupWork.h"
#include "../LabAudioHub.h"

using namespace LabAudioProtocol;

static int failures = 0;
#define CHECK( condition, text ) \
	do { const bool ok = ( condition ); std::printf( "%s  %s\n", ok ? "PASS" : "FAIL", text ); if( !ok ) ++failures; } while( 0 )

static void wait( int ms )
{
	QElapsedTimer timer;
	timer.start();
	while( timer.elapsed() < ms )
	{
		QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
	}
}

// A student connection that records what the hub sends.
class FakeStudent : public QObject
{
public:
	FakeStudent( int port, const QByteArray& token, quint8 roles )
	{
		connect( &socket, &QTcpSocket::connected, this, [this, token, roles]() {
			QByteArray hello;
			hello.append( char( Version ) );
			hello.append( char( roles ) );
			char length[2];
			qToBigEndian( quint16( token.size() ), length );
			hello.append( length, 2 );
			hello.append( token );
			socket.write( frame( FrameType::Hello, hello ) );
		} );
		connect( &socket, &QTcpSocket::readyRead, this, [this]() {
			reader.append( socket.readAll() );
			FrameType type;
			QByteArray payload;
			bool error = false;
			while( reader.next( type, payload, error ) )
			{
				if( std::getenv( "GROUPTEST_DEBUG" ) )
				{
					std::printf( "      frame %d size %d\n", int( type ), int( payload.size() ) );
				}
				if( type == FrameType::HelloAck )
				{
					accepted = payload.size() == 1 && payload.at( 0 ) == 1;
				}
				else if( type == FrameType::PeerAudio )
				{
					peerSources.append( int( qFromBigEndian<quint32>( payload.constData() ) ) );
				}
				else if( type == FrameType::Status && payload.size() == 1 )
				{
					status.append( quint8( payload.at( 0 ) ) );
				}
				else if( type == FrameType::Audio )
				{
					++teacherAudio;
				}
			}
		} );
		connect( &socket, &QTcpSocket::disconnected, this, [this]() { disconnected = true; } );
		socket.connectToHost( QStringLiteral( "127.0.0.1" ), quint16( port ) );
	}

	void sendAudio( quint32 sequence )
	{
		socket.write( frame( FrameType::Audio, audioPayload( sequence, QByteArray( 60, char( 0x55 ) ) ) ) );
	}

	QTcpSocket socket;
	FrameReader reader;
	bool accepted{false};
	bool disconnected{false};
	QList<int> peerSources;
	QList<quint8> status;
	int teacherAudio{0};
};

int main( int argc, char** argv )
{
	QApplication app( argc, argv );

	// --ui <dir>: render the teacher's dialog and panel to PNG files for a visual check.
	if( argc > 2 && QByteArray( argv[1] ) == "--ui" )
	{
		const QString dir = QString::fromLocal8Bit( argv[2] );
		LabAudioGroupDialog dialog( { QStringLiteral( "Anna Petrova" ), QStringLiteral( "Ben Koch" ),
									  QStringLiteral( "Carla Diaz" ), QStringLiteral( "Dan Ivanov" ),
									  QStringLiteral( "Emre Yilmaz" ), QStringLiteral( "PC-07" ),
									  QStringLiteral( "Gulnara Abenova" ) } );
		dialog.show();
		wait( 200 );
		dialog.grab().save( dir + QStringLiteral( "/group-dialog.png" ) );

		LabAudioGroupPanel panel;
		panel.setGroups( { { QStringLiteral( "Anna Petrova" ), QStringLiteral( "Ben Koch" ) },
						   { QStringLiteral( "Carla Diaz" ), QStringLiteral( "Dan Ivanov" ) },
						   { QStringLiteral( "Emre Yilmaz" ), QStringLiteral( "PC-07" ), QStringLiteral( "Gulnara Abenova" ) } } );
		panel.setFocus( 2, false );
		panel.show();
		wait( 200 );
		panel.grab().save( dir + QStringLiteral( "/group-panel.png" ) );
		return 0;
	}

	// Group building: 7 students in pairs -> 2 + 2 + 3 (nobody stays alone).
	const auto pairs = LabAudioGroupDialog::makeGroups( 7, 2, false );
	CHECK( pairs.size() == 3 && pairs[0] == QList<int>( { 0, 1 } ) && pairs[2] == QList<int>( { 4, 5, 6 } ),
		   "7 students in pairs make 2 + 2 + 3" );
	const auto random = LabAudioGroupDialog::makeGroups( 30, 4, true );
	int members = 0;
	for( const auto& group : random )
	{
		members += int( group.size() );
	}
	CHECK( random.size() == 8 && members == 30, "30 students in groups of 4 make 8 groups with everyone" );

	const int port = 11598;
	const QByteArray classToken = QByteArrayLiteral( "class-token-0123456789" );
	const QByteArray group1 = QByteArrayLiteral( "group-one-token-0123456789" );
	const QByteArray group2 = QByteArrayLiteral( "group-two-token-0123456789" );
	LabAudioHub hub( port, classToken );
	hub.setGroups( { { group1, 1 }, { group2, 2 } } );

	const quint8 groupRoles = SendMicrophone | ReceivePeers | ReceiveTeacher;
	FakeStudent anna( port, group1, groupRoles );
	FakeStudent ben( port, group1, groupRoles );
	FakeStudent carla( port, group2, groupRoles );
	FakeStudent dan( port, group2, groupRoles );
	FakeStudent classStudent( port, classToken, ReceiveTeacher );
	FakeStudent intruder( port, QByteArrayLiteral( "guessed-token-0123456789" ), groupRoles );
	const auto allAccepted = [&]() {
		return anna.accepted && ben.accepted && carla.accepted && dan.accepted && classStudent.accepted;
	};
	QElapsedTimer connectTimer;
	connectTimer.start();
	while( allAccepted() == false && connectTimer.elapsed() < 5000 )
	{
		wait( 50 );
	}
	wait( 200 );
	CHECK( allAccepted(), "group members and class student accepted" );
	std::printf( "      accepted: anna %d ben %d carla %d dan %d class %d\n", anna.accepted, ben.accepted,
				 carla.accepted, dan.accepted, classStudent.accepted );
	CHECK( intruder.accepted == false, "unknown group token refused" );

	for( quint32 i = 0; i < 10; ++i )
	{
		anna.sendAudio( i );
	}
	wait( 300 );
	CHECK( ben.peerSources.size() == 10, "partner hears all 10 packets" );
	CHECK( anna.peerSources.isEmpty(), "speaker does not hear herself" );
	CHECK( carla.peerSources.isEmpty() && dan.peerSources.isEmpty(), "other group hears nothing" );
	CHECK( classStudent.peerSources.isEmpty(), "class student hears nothing" );

	const int carlaStatusBefore = int( carla.status.size() );
	hub.setFocus( 1, false );
	wait( 300 );
	CHECK( anna.status.value( anna.status.size() - 1 ) == TeacherListening &&
		   ben.status.value( ben.status.size() - 1 ) == TeacherListening,
		   "group 1 sees that the teacher is listening" );
	CHECK( carla.status.size() == carlaStatusBefore, "group 2 gets no status change" );

	hub.setFocus( 2, true );
	wait( 300 );
	CHECK( anna.status.value( anna.status.size() - 1 ) == 0, "group 1 sees that the teacher left" );
	CHECK( carla.status.value( carla.status.size() - 1 ) == ( TeacherListening | TeacherTalking ),
		   "group 2 sees that the teacher joined" );

	// Regrouping: new tokens, all old group members are disconnected, the class student stays.
	hub.setFocus( LabAudioHub::NoGroup, false );
	hub.setGroups( { { QByteArrayLiteral( "new-group-token-0123456789" ), 1 } } );
	wait( 300 );
	CHECK( anna.disconnected && ben.disconnected && carla.disconnected && dan.disconnected,
		   "regrouping disconnects the old groups" );
	CHECK( classStudent.disconnected == false, "class student stays connected" );

	std::printf( failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures );
	return failures ? 1 : 0;
}
