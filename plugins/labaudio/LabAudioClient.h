/*
 * LabAudioClient.h - audio client running in a student's session
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

#include <QTimer>
#include <QWidget>

#include "LabAudioCrypto.h"
#include "LabAudioProtocol.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QTcpSocket;
class LabAudioCapture;
class LabAudioPlayback;

// Always-visible notice on the student's screen while lab audio is active, with a volume slider
// and a "Call the teacher" button.
class LabAudioIndicator : public QWidget
{
	Q_OBJECT
public:
	explicit LabAudioIndicator( quint8 roles, const QStringList& groupMembers = {} );

	void setStatus( const QString& status );
	void setLevel( float level );
	void setTeacherStatus( quint8 statusFlags );
	// "Call the teacher" button: state kept by the Veyon Server.
	void setCallState( bool calling, bool allowed );
	// Last volume chosen by this student (percent, 100 = unchanged).
	static int savedVolume();

Q_SIGNALS:
	void volumeChanged( int percent );
	void callToggled( bool calling );

protected:
	void closeEvent( QCloseEvent* event ) override;

private:
	void updateCallButton();
	void placeOnScreen();

	QLabel* m_title;
	QLabel* m_status;
	QLabel* m_teacher{nullptr};
	QLabel* m_recording{nullptr};
	QProgressBar* m_level{nullptr};
	QSlider* m_volume{nullptr};
	QPushButton* m_call{nullptr};
};

// Connects to the teacher's hub, plays the teacher's voice and/or sends the microphone.
class LabAudioClient : public QObject
{
	Q_OBJECT
public:
	// hosts: addresses of the teacher's computer, tried in order (the first one is the preferred route).
	// groupMembers: names of the other students in the group (group work only).
	LabAudioClient( const QStringList& hosts, int port, const QByteArray& token, quint8 roles,
					const QStringList& groupMembers = {}, QObject* parent = nullptr );

	LabAudioIndicator* indicator() const
	{
		return m_indicator;
	}
	// Opus bit rate of this student's microphone (bit/s), set by the teacher.
	void setBitRate( int bitRate )
	{
		m_bitRate = bitRate;
	}
	~LabAudioClient() override;

	static constexpr int ReconnectIntervalMs = 2000;
	static constexpr int NextAddressDelayMs = 200;
	static constexpr int ConnectTimeoutMs = 3000;

Q_SIGNALS:
	void audioReceived( int bytes );
	void peerAudioReceived( int sourceId );
	void teacherStatusChanged( quint8 statusFlags );

private:
	void connectToHub();
	void onConnected();
	void onReadyRead();
	void onDisconnected();
	QString currentHost() const;
	void startAudio();
	void stopAudio();
	void sendPacket( const QByteArray& packet );
	void send( LabAudioProtocol::FrameType type, const QByteArray& payload );

	QStringList m_hosts;
	int m_hostIndex{0};
	bool m_attemptActive{false};
	bool m_wasConnected{false};
	QString m_failReason;
	QTimer m_connectTimer;
	int m_port;
	QByteArray m_token;
	quint8 m_roles;
	QTcpSocket* m_socket;
	LabAudioProtocol::FrameReader m_reader;
	LabAudioSession m_session;
	QByteArray m_clientNonce;
	QTimer m_reconnectTimer;
	bool m_accepted{false};
	quint32 m_sequence{0};
	int m_bitRate{32000};
	LabAudioCapture* m_capture{nullptr};
	LabAudioPlayback* m_playback{nullptr};
	LabAudioIndicator* m_indicator;
};
