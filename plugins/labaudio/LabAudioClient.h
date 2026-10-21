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

#include "LabAudioProtocol.h"

class QLabel;
class QProgressBar;
class QTcpSocket;
class LabAudioCapture;
class LabAudioPlayback;

// Always-visible notice on the student's screen while lab audio is active.
class LabAudioIndicator : public QWidget
{
	Q_OBJECT
public:
	explicit LabAudioIndicator( quint8 roles );

	void setStatus( const QString& status );
	void setLevel( float level );

protected:
	void closeEvent( QCloseEvent* event ) override;

private:
	QLabel* m_title;
	QLabel* m_status;
	QProgressBar* m_level{nullptr};
};

// Connects to the teacher's hub, plays the teacher's voice and/or sends the microphone.
class LabAudioClient : public QObject
{
	Q_OBJECT
public:
	// hosts: addresses of the teacher's computer, tried in order (the first one is the preferred route).
	LabAudioClient( const QStringList& hosts, int port, const QByteArray& token, quint8 roles, QObject* parent = nullptr );
	~LabAudioClient() override;

	static constexpr int ReconnectIntervalMs = 2000;
	static constexpr int NextAddressDelayMs = 200;
	static constexpr int ConnectTimeoutMs = 3000;

Q_SIGNALS:
	void audioReceived( int bytes );

private:
	void connectToHub();
	void onConnected();
	void onReadyRead();
	void onDisconnected();
	QString currentHost() const;
	void startAudio();
	void stopAudio();
	void sendPacket( const QByteArray& packet );

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
	QTimer m_reconnectTimer;
	bool m_accepted{false};
	quint32 m_sequence{0};
	LabAudioCapture* m_capture{nullptr};
	LabAudioPlayback* m_playback{nullptr};
	LabAudioIndicator* m_indicator;
};
