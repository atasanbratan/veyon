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

#include "../LabAudioClient.h"
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
	// claimedToken: the token whose (public) key id is announced; differs from token for a forger
	// who saw a key id on the network but does not know the token.
	FakeStudent( int port, const QByteArray& token, quint8 roles, const QByteArray& claimedToken = {} )
	{
		connect( &socket, &QTcpSocket::connected, this, [this, token, claimedToken]() {
			clientNonce = LabAudioSession::randomNonce();
			QByteArray hello( 1, char( Version ) );
			hello += LabAudioSession::keyId( claimedToken.isEmpty() ? token : claimedToken );
			hello += clientNonce;
			socket.write( frame( FrameType::Hello, hello ) );
		} );
		connect( &socket, &QTcpSocket::readyRead, this, [this, token, roles]() {
			const auto data = socket.readAll();
			raw += data;
			reader.append( data );
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
					accepted = payload.size() == 1 + LabAudioSession::NonceSize && payload.at( 0 ) == 1 &&
							   session.init( token, clientNonce, payload.mid( 1 ), LabAudioSession::Role::Client );
					if( accepted )
					{
						socket.write( session.seal( FrameType::Auth, QByteArray( 1, char( roles ) ) ) );
					}
					continue;
				}
				if( type != FrameType::Encrypted || session.open( payload, type, payload ) == false )
				{
					++notAuthentic;
					continue;
				}
				if( type == FrameType::PeerAudio )
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
				else if( type == FrameType::CallTeacher && payload == QByteArray( 1, char( 0 ) ) )
				{
					callAnswered = true;
				}
			}
		} );
		connect( &socket, &QTcpSocket::disconnected, this, [this]() { disconnected = true; } );
		socket.connectToHost( QStringLiteral( "127.0.0.1" ), quint16( port ) );
	}

	void sendAudio( quint32 sequence )
	{
		socket.write( session.seal( FrameType::Audio, audioPayload( sequence, QByteArray( 60, char( 0x55 ) ) ) ) );
	}

	void call( bool calling, const QString& name )
	{
		QByteArray payload( 1, calling ? 1 : 0 );
		payload.append( name.toUtf8() );
		socket.write( session.seal( FrameType::CallTeacher, payload ) );
	}

	QTcpSocket socket;
	FrameReader reader;
	LabAudioSession session;
	QByteArray clientNonce;
	int notAuthentic{0};
	QByteArray raw;	// everything received, as an eavesdropper on the network would see it
	bool accepted{false};
	bool disconnected{false};
	QList<int> peerSources;
	QList<quint8> status;
	int teacherAudio{0};
	bool callAnswered{false};
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

		LabAudioTeacherPanel teacher;
		teacher.setMode( QStringLiteral( "You are speaking to the students." ),
						 QStringLiteral( "To stop, click \"Speak to students\" in the toolbar again." ) );
		teacher.show();
		wait( 200 );
		teacher.grab().save( dir + QStringLiteral( "/teacher-panel.png" ) );

		LabAudioIndicator speaking( ReceiveTeacher );
		speaking.setStatus( QStringLiteral( "Connected" ) );
		wait( 200 );
		speaking.grab().save( dir + QStringLiteral( "/student-speaking.png" ) );

		LabAudioIndicator group( SendMicrophone | ReceivePeers | ReceiveTeacher, { QStringLiteral( "Ben Koch" ) } );
		group.setStatus( QStringLiteral( "Connected" ) );
		group.setTeacherStatus( TeacherListening );
		group.setLevel( 0.4f );
		wait( 200 );
		group.grab().save( dir + QStringLiteral( "/student-group.png" ) );

		LabAudioCallsWindow calls;
		calls.setCalls( { { 1, QStringLiteral( "Emre Yilmaz (PC-05)" ), 0, QDateTime::currentDateTime().addSecs( -90 ) },
						  { 2, QStringLiteral( "Carla Diaz (PC-03)" ), 2, QDateTime::currentDateTime() } } );
		wait( 200 );
		calls.grab().save( dir + QStringLiteral( "/teacher-calls.png" ) );
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
	hub.setCallsWindowEnabled( false );
	hub.setGroups( { { group1, 1 }, { group2, 2 } } );

	const quint8 groupRoles = SendMicrophone | ReceivePeers | ReceiveTeacher;
	FakeStudent anna( port, group1, groupRoles );
	FakeStudent ben( port, group1, groupRoles );
	FakeStudent carla( port, group2, groupRoles );
	FakeStudent dan( port, group2, groupRoles );
	FakeStudent classStudent( port, classToken, ReceiveTeacher );
	FakeStudent intruder( port, QByteArrayLiteral( "guessed-token-0123456789" ), groupRoles );
	// Knows group 1's public key id (e.g. sniffed from a Hello) but not its token.
	FakeStudent forger( port, QByteArrayLiteral( "guessed-token-0123456789" ), groupRoles, group1 );
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

	// Encryption: the audio bytes never appear in clear on the wire, and knowing a key id is useless.
	CHECK( ben.raw.size() > 10 * 60 && ben.raw.contains( QByteArray( 24, char( 0x55 ) ) ) == false,
		   "audio is encrypted on the wire" );
	CHECK( forger.disconnected && forger.peerSources.isEmpty() && forger.teacherAudio == 0,
		   "forger with a sniffed key id is disconnected and hears nothing" );
	CHECK( anna.notAuthentic == 0 && ben.notAuthentic == 0, "all frames from the hub are authentic" );

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

	// Calling the teacher: two calls listed oldest first, "Done" resets the student's button,
	// a cancelled call disappears.
	classStudent.call( true, QStringLiteral( "Emre Yilmaz (PC-05)" ) );
	wait( 100 );
	carla.call( true, QStringLiteral( "Carla Diaz (PC-03)" ) );
	wait( 300 );
	auto calls = hub.calls();
	CHECK( calls.size() == 2 && calls[0].name == QStringLiteral( "Emre Yilmaz (PC-05)" ) && calls[0].group == 0 &&
		   calls[1].name == QStringLiteral( "Carla Diaz (PC-03)" ) && calls[1].group == 2,
		   "teacher sees both calls with name and group, oldest first" );
	hub.answerCall( calls[0].clientId );
	wait( 300 );
	CHECK( classStudent.callAnswered && hub.calls().size() == 1, "\"Done\" answers the call and resets the student's button" );
	carla.call( false, {} );
	wait( 300 );
	CHECK( hub.calls().isEmpty() && carla.callAnswered == false, "a cancelled call disappears" );
	dan.call( true, QStringLiteral( "Dan Ivanov" ) );
	wait( 300 );

	// Regrouping: new tokens, all old group members are disconnected, the class student stays.
	hub.setFocus( LabAudioHub::NoGroup, false );
	hub.setGroups( { { QByteArrayLiteral( "new-group-token-0123456789" ), 1 } } );
	wait( 300 );
	CHECK( anna.disconnected && ben.disconnected && carla.disconnected && dan.disconnected,
		   "regrouping disconnects the old groups" );
	CHECK( classStudent.disconnected == false, "class student stays connected" );
	CHECK( hub.calls().isEmpty(), "a call ends when the student disconnects" );

	std::printf( failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures );
	return failures ? 1 : 0;
}
