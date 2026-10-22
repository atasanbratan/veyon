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

#include "LabAudioCrypto.h"
#include "LabAudioProtocol.h"
#include "LabAudioRecorder.h"

class QTcpServer;
class QTcpSocket;
class QTimer;
class LabAudioCapture;
class LabAudioPlayback;

// Accepts student connections authenticated with the session token.
// Clients authenticated with the class token form group 0: they receive the teacher's microphone
// (ReceiveTeacher) and are heard by the teacher (SendMicrophone).
// Clients authenticated with a group token (group work) hear each other through the hub; the
// teacher hears and/or talks to one group at a time (the focus group).
class LabAudioHub : public QObject
{
	Q_OBJECT
public:
	LabAudioHub( int port, const QByteArray& token, QObject* parent = nullptr );
	~LabAudioHub() override;

	bool isListening() const;
	void setToken( const QByteArray& token );
	// group token -> group id (> 0); clients whose token is no longer valid are disconnected
	void setGroups( const QHash<QByteArray, int>& groupTokens );
	// focusGroup < 0: the teacher neither hears nor talks to any group
	void setFocus( int focusGroup, bool teacherTalks );
	// Mutes the teacher's microphone (silence keeps flowing so students' buffers stay steady).
	void setTeacherMuted( bool muted );
	// conversation: what the teacher hears and says; groups: one file per group. labels: 0 = conversation,
	// group id = that group (a changed label starts a new file).
	void setRecording( bool conversation, bool groups, const QHash<int, QString>& labels );
	QStringList recordingPaths() const;

	static constexpr int MaxClients = 100;
	static constexpr qint64 MaxQueuedBytes = 64 * 1024;	// drop audio to a client that falls behind
	static constexpr int ClassGroup = 0;
	static constexpr int NoGroup = -1;

Q_SIGNALS:
	void teacherPacketSent( int receivers );
	void studentPacketReceived( int clientId );
	void peerPacketForwarded( int sourceId, int receivers );
	void recordingChanged();

private:
	struct Client
	{
		QPointer<QTcpSocket> socket;
		LabAudioProtocol::FrameReader reader;
		quint8 roles{0};
		bool authenticated{false};
		QTimer* helloTimer{nullptr};
		QByteArray token;
		int group{ClassGroup};
		LabAudioSession session;	// ready after the hello; authenticated after the Auth frame
	};

	void acceptConnections();
	void readClient( int id );
	void handleHello( int id, const QByteArray& payload );
	void handleAuth( int id, quint8 roles );
	void sendToClient( Client& client, LabAudioProtocol::FrameType type, const QByteArray& payload );
	void dropClient( int id );
	void updateAudio();
	void sendTeacherPacket( const QByteArray& packet );
	void forwardStudentAudio( int sourceId, const QByteArray& audioPayload );
	void sendStatus( int id );
	bool hearsTeacher( const Client& client ) const;
	bool teacherHears( const Client& client ) const;
	bool isRecorded( const Client& client ) const;
	void updateRecorders();
	void sendStatusToAll();

	QTcpServer* m_server{nullptr};
	QByteArray m_token;
	QHash<QByteArray, int> m_groupTokens;
	int m_focusGroup{NoGroup};
	bool m_teacherTalks{false};
	bool m_teacherMuted{false};
	bool m_recordConversation{false};
	bool m_recordGroups{false};
	QHash<int, QString> m_recordingLabels;
	LabAudioRecorder* m_conversationRecorder{nullptr};
	QHash<int, LabAudioRecorder*> m_groupRecorders;
	QHash<int, Client> m_clients;
	int m_nextId{1};
	quint32 m_sequence{0};
	LabAudioCapture* m_capture{nullptr};
	LabAudioPlayback* m_playback{nullptr};
};
