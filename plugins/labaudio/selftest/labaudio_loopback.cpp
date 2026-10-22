/*
 * labaudio_loopback.cpp - runs the teacher hub and a student client on this computer over TCP
 * with the real microphone and headset, and reports packet flow. Also checks that a wrong token
 * is rejected.
 *
 * Usage: labaudio_loopback [seconds] [listen|talk|speak] [record]
 *   speak  (default) teacher microphone -> student headset   (= "Speak to students")
 *   listen student microphone -> teacher headset              (= "Listen to student")
 *   talk   both directions                                    (= "Talk with student")
 *
 * Copyright (c) 2026 TDMUGI
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QApplication>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>
#include <cstdio>

#include "../LabAudioClient.h"
#include "../LabAudioHub.h"

using namespace LabAudioProtocol;

int main( int argc, char** argv )
{
	QApplication app( argc, argv );
	const int seconds = argc > 1 ? QByteArray( argv[1] ).toInt() : 5;
	const QByteArray mode = argc > 2 ? QByteArray( argv[2] ) : QByteArrayLiteral( "speak" );
	quint8 roles = ReceiveTeacher;
	if( mode == "listen" ) roles = SendMicrophone;
	if( mode == "talk" ) roles = ReceiveTeacher | SendMicrophone;

	const int port = 11599;
	const QByteArray token = QByteArrayLiteral( "loopback-test-token-0123456789" );
	LabAudioHub hub( port, token );
	if( hub.isListening() == false )
	{
		std::printf( "FAIL  hub could not listen on %d\n", port );
		return 1;
	}

	// Optional 3rd argument "record": records the conversation (folder: LABAUDIO_RECORDINGS_DIR or Documents).
	if( argc > 3 && QByteArray( argv[3] ) == "record" )
	{
		hub.setRecording( true, false, { { 0, QStringLiteral( "Loopback test" ) } } );
		std::printf( "recording to %s\n", qUtf8Printable( hub.recordingPaths().value( 0 ) ) );
	}

	int teacherSent = 0, studentReceived = 0, hubReceived = 0;
	QObject::connect( &hub, &LabAudioHub::teacherPacketSent, [&]( int receivers ) { teacherSent += receivers > 0; } );
	QObject::connect( &hub, &LabAudioHub::studentPacketReceived, [&]( int ) { ++hubReceived; } );

	// The first address is unreachable on purpose: the client must fall back to the next one.
	LabAudioClient client( QStringList{ QStringLiteral( "192.0.2.1" ), QStringLiteral( "127.0.0.1" ) }, port, token, roles );
	QObject::connect( &client, &LabAudioClient::audioReceived, [&]( int ) { ++studentReceived; } );

	// An intruder with the wrong token must be refused.
	bool intruderRejected = false;
	QTcpSocket intruder;
	QObject::connect( &intruder, &QTcpSocket::connected, [&]() {
		QByteArray hello( 1, char( Version ) );
		hello += LabAudioSession::keyId( QByteArrayLiteral( "wrong" ) );
		hello += LabAudioSession::randomNonce();
		intruder.write( frame( FrameType::Hello, hello ) );
	} );
	QByteArray intruderData;
	QObject::connect( &intruder, &QTcpSocket::readyRead, [&]() { intruderData += intruder.readAll(); } );
	QObject::connect( &intruder, &QTcpSocket::disconnected, [&]() {
		intruderRejected = intruderData.size() >= 6 && intruderData.at( 5 ) == 0;
	} );
	intruder.connectToHost( QStringLiteral( "127.0.0.1" ), port );

	std::printf( "Running '%s' for %d s - speak into the headset microphone...\n", mode.constData(), seconds );
	std::fflush( stdout );
	QTimer::singleShot( seconds * 1000, &app, []() { QCoreApplication::exit( 0 ); } );
	app.exec();

	// 50 packets per second, minus the connect timeout spent on the deliberately unreachable first address.
	const int expected = ( seconds - LabAudioClient::ConnectTimeoutMs / 1000 ) * 50;
	int failures = 0;
	auto check = [&]( bool ok, const char* what ) { std::printf( "%s  %s\n", ok ? "PASS" : "FAIL", what ); failures += !ok; };
	std::printf( "teacher packets sent %d, student received %d, hub received from student %d (expected ~%d each)\n",
				 teacherSent, studentReceived, hubReceived, expected );
	if( roles & ReceiveTeacher )
	{
		check( teacherSent > expected * 0.7, "teacher microphone captured and sent" );
		check( studentReceived > expected * 0.7, "student client received the teacher's audio" );
	}
	if( roles & SendMicrophone )
	{
		check( hubReceived > expected * 0.7, "hub received the student's microphone" );
	}
	check( intruderRejected, "connection with a wrong token was refused" );
	std::printf( failures ? "%d FAILED\n" : "ALL PASSED\n", failures );
	return failures ? 1 : 0;
}
