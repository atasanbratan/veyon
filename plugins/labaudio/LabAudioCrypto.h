/*
 * LabAudioCrypto.h - authenticated encryption of the lab audio connection
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

#include <QByteArray>

#include "LabAudioProtocol.h"

// The token (class or group) is a secret shared through Veyon's authenticated channel and never
// sent over the audio connection. Each connection derives its own keys from the token and two
// fresh nonces (HKDF-SHA256), one key per direction. Every frame after the handshake is
// AES-256-GCM with an implicit per-direction counter as nonce, so modified, replayed, reordered or
// dropped frames fail to decrypt and end the connection.
class LabAudioSession
{
public:
	static constexpr int NonceSize = 16;
	static constexpr int KeyIdSize = 8;
	static constexpr int TagSize = 16;
	// Bytes an encrypted frame adds to its payload (inner type + GCM tag).
	static constexpr int Overhead = 1 + TagSize;

	enum class Role
	{
		Client,
		Hub
	};

	// Public identifier of a token: lets the hub pick the right token without the token being sent.
	static QByteArray keyId( const QByteArray& token );
	static QByteArray randomNonce();

	bool init( const QByteArray& token, const QByteArray& clientNonce, const QByteArray& hubNonce, Role role );
	bool isReady() const
	{
		return m_ready;
	}

	// Returns a complete Encrypted frame carrying innerType + payload.
	QByteArray seal( LabAudioProtocol::FrameType innerType, const QByteArray& payload );
	// Decrypts the payload of an Encrypted frame; false if it is not authentic.
	bool open( const QByteArray& encryptedPayload, LabAudioProtocol::FrameType& innerType, QByteArray& payload );

private:
	QByteArray m_sendKey;
	QByteArray m_receiveKey;
	quint64 m_sendCounter{0};
	quint64 m_receiveCounter{0};
	bool m_ready{false};
};
