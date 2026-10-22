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
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTcpSocket>
#include <QtEndian>
#include <cstdio>
#include <cstdlib>

#include "../../labaudiocalls/LabAudioCalls.h"
#include "../LabAudioClient.h"
#include "../LabAudioGroupWork.h"
#include "ui_LabAudioConfigurationPage.h"
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
						socket.write( session.seal( FrameType::Auth, QByteArray( QByteArray( 1, char( roles ) ) + tag ) ) );
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
			}
		} );
		connect( &socket, &QTcpSocket::disconnected, this, [this]() { disconnected = true; } );
		socket.connectToHost( QStringLiteral( "127.0.0.1" ), quint16( port ) );
	}

	void sendAudio( quint32 sequence )
	{
		socket.write( session.seal( FrameType::Audio, audioPayload( sequence, QByteArray( 60, char( 0x55 ) ) ) ) );
	}

	QTcpSocket socket;
	FrameReader reader;
	LabAudioSession session;
	QByteArray clientNonce;
	int notAuthentic{0};
	QByteArray raw;	// everything received, as an eavesdropper on the network would see it
	QByteArray tag;	// sent in the Auth frame, like a Veyon computer uid
	bool accepted{false};
	bool disconnected{false};
	QList<int> peerSources;
	QList<quint8> status;
	int teacherAudio{0};
};

int main( int argc, char** argv )
{
	QApplication app( argc, argv );
	const auto recordingsDir = QDir::temp().filePath( QStringLiteral( "labaudio-grouptest-recordings" ) );
	QDir( recordingsDir ).removeRecursively();
	qputenv( "LABAUDIO_RECORDINGS_DIR", recordingsDir.toLocal8Bit() );

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
		panel.setConnectionStatus( 7, 7, {} );
		panel.show();
		wait( 200 );
		panel.grab().save( dir + QStringLiteral( "/group-panel.png" ) );

		LabAudioTeacherPanel teacher;
		teacher.setMode( QStringLiteral( "You are speaking to the students." ),
						 QStringLiteral( "To stop, click \"Speak to students\" in the toolbar again." ) );
		teacher.setConnectionStatus( 26, 30, { QStringLiteral( "Anna Petrova" ), QStringLiteral( "PC-07" ),
											 QStringLiteral( "Ben Koch" ), QStringLiteral( "Dan Ivanov" ) } );
		teacher.show();
		wait( 200 );
		teacher.grab().save( dir + QStringLiteral( "/teacher-panel.png" ) );

		LabAudioIndicator speaking( ReceiveTeacher );
		speaking.setStatus( QStringLiteral( "Connected" ) );
		wait( 200 );
		speaking.grab().save( dir + QStringLiteral( "/student-speaking.png" ) );

		LabAudioIndicator group( SendMicrophone | ReceivePeers | ReceiveTeacher, { QStringLiteral( "Ben Koch" ) } );
		group.setStatus( QStringLiteral( "Connected" ) );
		group.setTeacherStatus( TeacherListening | Recording );
		group.setLevel( 0.4f );
		wait( 200 );
		group.grab().save( dir + QStringLiteral( "/student-group.png" ) );

		LabAudioCallsWindow noCalls;
		noCalls.present();
		wait( 200 );
		noCalls.grab().save( dir + QStringLiteral( "/teacher-calls-empty.png" ) );

		LabAudioCallsWindow calls;
		calls.setCalls( { { 1, QStringLiteral( "Emre Yilmaz (PC-05)" ), 0, QDateTime::currentDateTime().addSecs( -90 ) },
						  { 2, QStringLiteral( "Carla Diaz (PC-03)" ), 2, QDateTime::currentDateTime() } } );
		wait( 200 );
		calls.grab().save( dir + QStringLiteral( "/teacher-calls.png" ) );

		// Layout of the Veyon Configurator page (without the Veyon configuration behind it).
		QWidget settings;
		Ui::LabAudioConfigurationPage settingsUi;
		settingsUi.setupUi( &settings );
		settingsUi.hubPort->setValue( 11500 );
		settingsUi.audioQuality->setCurrentIndex( 1 );
		settingsUi.callTrayEnabled->setChecked( true );
		settings.resize( 640, 460 );
		settings.show();
		wait( 200 );
		settings.grab().save( dir + QStringLiteral( "/settings-page.png" ) );
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
	anna.tag = "pc-anna";
	ben.tag = "pc-ben";
	carla.tag = "pc-carla";
	dan.tag = "pc-dan";
	classStudent.tag = "pc-emre";
	int connectionReports = 0;
	QObject::connect( &hub, &LabAudioHub::connectedClientsChanged, [&]() { ++connectionReports; } );
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
	auto connectedTags = hub.connectedTags();
	connectedTags.sort();
	CHECK( connectedTags == QStringList( { "pc-anna", "pc-ben", "pc-carla", "pc-dan", "pc-emre" } ) && connectionReports >= 5,
		   "hub knows which computers are connected (for the teacher's status)" );
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

	// Recording each group: one file per group, the members see it, the class student does not.
	hub.setRecording( false, true, { { 1, QStringLiteral( "Group 1 - Anna, Ben" ) }, { 2, QStringLiteral( "Group 2 - Carla, Dan" ) } } );
	wait( 300 );
	const auto recordingFlag = []( const FakeStudent& student ) {
		return student.status.isEmpty() == false && ( student.status.last() & Recording );
	};
	CHECK( recordingFlag( anna ) && recordingFlag( ben ) && recordingFlag( carla ) && recordingFlag( dan ),
		   "every group member sees that the group is recorded" );
	CHECK( recordingFlag( classStudent ) == false, "the class student is not told about a recording" );
	const auto paths = hub.recordingPaths();
	CHECK( paths.size() == 2 && QFile::exists( paths.value( 0 ) ) && QFile::exists( paths.value( 1 ) ) &&
		   paths.join( QLatin1Char( '|' ) ).contains( QStringLiteral( "Group 2 - Carla, Dan.opus" ) ),
		   "one recording file per group, named after the group" );

	// Regrouping: new tokens, all old group members are disconnected, the class student stays.
	hub.setFocus( LabAudioHub::NoGroup, false );
	hub.setGroups( { { QByteArrayLiteral( "new-group-token-0123456789" ), 1 } } );
	wait( 300 );
	CHECK( anna.disconnected && ben.disconnected && carla.disconnected && dan.disconnected,
		   "regrouping disconnects the old groups" );
	CHECK( classStudent.disconnected == false, "class student stays connected" );
	CHECK( hub.recordingPaths().size() == 1, "the recording of a group that no longer exists is closed" );

	hub.setRecording( false, false, {} );
	wait( 100 );
	QFile closed( paths.value( 1 ) );
	const bool opened = closed.open( QFile::ReadOnly );
	const auto closedData = closed.readAll();
	CHECK( opened && hub.recordingPaths().isEmpty() && closedData.startsWith( "OggS" ) && closedData.contains( "OpusHead" ),
		   "stopping the recording leaves complete Ogg Opus files" );

	// Veyon starts the hub and the students at the same moment, so students usually come first. A real
	// client must connect right after the hub is up, even with a second teacher address that would time out.
	{
		const int latePort = 11597;
		const QByteArray lateToken = QByteArrayLiteral( "late-hub-token-0123456789" );
		LabAudioClient early( { QStringLiteral( "127.0.0.1" ), QStringLiteral( "192.0.2.1" ) }, latePort, lateToken,
							  ReceiveTeacher );
		early.setTag( QStringLiteral( "pc-early" ) );
		// Long enough for a first "refused" to come back (Windows retries a refused connection for about 2 s).
		wait( 3000 );
		LabAudioHub lateHub( latePort, lateToken );
		QElapsedTimer sinceHubStart;
		sinceHubStart.start();
		while( lateHub.connectedTags().isEmpty() && sinceHubStart.elapsed() < 8000 )
		{
			wait( 10 );
		}
		const auto delay = sinceHubStart.elapsed();
		std::printf( "      student connected %lld ms after the hub started\n", delay );
		CHECK( lateHub.connectedTags() == QStringList( { QStringLiteral( "pc-early" ) } ) && delay < 1000,
			   "a student started before the hub connects within a second of the hub starting" );
	}

	std::printf( failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures );
	return failures ? 1 : 0;
}
