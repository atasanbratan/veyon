/*
 * LabAudioHub.h - audio hub running in the teacher's session
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

#include <QHash>
#include <QObject>
#include <QPointer>

#include "LabAudioProtocol.h"

class QTcpServer;
class QTcpSocket;
class QTimer;
class LabAudioCapture;
class LabAudioPlayback;

// Accepts student connections authenticated with the session token.
// Sends the teacher's microphone to clients with ReceiveTeacher and plays (mixed) microphones of
// clients with SendMicrophone on the teacher's headset.
class LabAudioHub : public QObject
{
	Q_OBJECT
public:
	LabAudioHub( int port, const QByteArray& token, QObject* parent = nullptr );
	~LabAudioHub() override;

	bool isListening() const;
	void setToken( const QByteArray& token );

	static constexpr int MaxClients = 100;
	static constexpr qint64 MaxQueuedBytes = 64 * 1024;	// drop audio to a client that falls behind

private:
	struct Client
	{
		QPointer<QTcpSocket> socket;
		LabAudioProtocol::FrameReader reader;
		quint8 roles{0};
		bool authenticated{false};
		QTimer* helloTimer{nullptr};
	};

	void acceptConnections();
	void readClient( int id );
	void handleHello( int id, const QByteArray& payload );
	void dropClient( int id );
	void updateAudio();
	void sendTeacherPacket( const QByteArray& packet );

	QTcpServer* m_server{nullptr};
	QByteArray m_token;
	QHash<int, Client> m_clients;
	int m_nextId{1};
	quint32 m_sequence{0};
	LabAudioCapture* m_capture{nullptr};
	LabAudioPlayback* m_playback{nullptr};
};
