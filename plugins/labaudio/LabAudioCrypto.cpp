/*
 * LabAudioCrypto.cpp - authenticated encryption of the lab audio connection
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

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QtEndian>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "LabAudioCrypto.h"

using namespace LabAudioProtocol;

namespace
{

const QByteArray HkdfInfo = QByteArrayLiteral( "TDMUGI LabAudio v2 session keys" );
const QByteArray Aad = QByteArrayLiteral( "LabAudio2" );
constexpr int KeySize = 32;
constexpr int IvSize = 12;

QByteArray hmac( const QByteArray& key, const QByteArray& message )
{
	return QMessageAuthenticationCode::hash( message, key, QCryptographicHash::Sha256 );
}

// RFC 5869 with SHA-256, enough output for two keys.
QByteArray hkdf( const QByteArray& secret, const QByteArray& salt, int length )
{
	const auto prk = hmac( salt, secret );
	QByteArray okm;
	QByteArray block;
	for( char counter = 1; okm.size() < length; ++counter )
	{
		block = hmac( prk, QByteArray( block + HkdfInfo + QByteArray( 1, counter ) ) );
		okm += block;
	}
	return okm.left( length );
}

QByteArray iv( quint64 counter )
{
	QByteArray result( IvSize, 0 );
	qToBigEndian( counter, result.data() + IvSize - 8 );
	return result;
}

}



QByteArray LabAudioSession::keyId( const QByteArray& token )
{
	return QCryptographicHash::hash( QByteArray( QByteArrayLiteral( "LabAudio key id" ) + token ), QCryptographicHash::Sha256 )
		.left( KeyIdSize );
}



QByteArray LabAudioSession::randomNonce()
{
	QByteArray nonce( NonceSize, Qt::Uninitialized );
	if( RAND_bytes( reinterpret_cast<unsigned char*>( nonce.data() ), NonceSize ) != 1 )
	{
		return {};
	}
	return nonce;
}



bool LabAudioSession::init( const QByteArray& token, const QByteArray& clientNonce, const QByteArray& hubNonce, Role role )
{
	m_ready = false;
	if( token.isEmpty() || clientNonce.size() != NonceSize || hubNonce.size() != NonceSize )
	{
		return false;
	}
	const auto keys = hkdf( token, QByteArray( clientNonce + hubNonce ), 2 * KeySize );
	const auto clientToHub = keys.left( KeySize );
	const auto hubToClient = keys.mid( KeySize );
	m_sendKey = role == Role::Client ? clientToHub : hubToClient;
	m_receiveKey = role == Role::Client ? hubToClient : clientToHub;
	m_sendCounter = 0;
	m_receiveCounter = 0;
	m_ready = true;
	return true;
}



QByteArray LabAudioSession::seal( FrameType innerType, const QByteArray& payload )
{
	if( m_ready == false )
	{
		return {};
	}
	QByteArray plain( 1, char( innerType ) );
	plain += payload;

	QByteArray out( plain.size() + TagSize, Qt::Uninitialized );
	auto ctx = EVP_CIPHER_CTX_new();
	int length = 0;
	int total = 0;
	const auto nonce = iv( m_sendCounter++ );
	bool ok = ctx &&
			  EVP_EncryptInit_ex( ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr ) == 1 &&
			  EVP_CIPHER_CTX_ctrl( ctx, EVP_CTRL_GCM_SET_IVLEN, IvSize, nullptr ) == 1 &&
			  EVP_EncryptInit_ex( ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>( m_sendKey.constData() ),
								  reinterpret_cast<const unsigned char*>( nonce.constData() ) ) == 1 &&
			  EVP_EncryptUpdate( ctx, nullptr, &length, reinterpret_cast<const unsigned char*>( Aad.constData() ),
								 int( Aad.size() ) ) == 1 &&
			  EVP_EncryptUpdate( ctx, reinterpret_cast<unsigned char*>( out.data() ), &length,
								 reinterpret_cast<const unsigned char*>( plain.constData() ), int( plain.size() ) ) == 1;
	total = length;
	ok = ok && EVP_EncryptFinal_ex( ctx, reinterpret_cast<unsigned char*>( out.data() ) + total, &length ) == 1;
	total += length;
	ok = ok && EVP_CIPHER_CTX_ctrl( ctx, EVP_CTRL_GCM_GET_TAG, TagSize, out.data() + total ) == 1;
	EVP_CIPHER_CTX_free( ctx );
	if( ok == false )
	{
		return {};
	}
	return frame( FrameType::Encrypted, out );
}



bool LabAudioSession::open( const QByteArray& encryptedPayload, FrameType& innerType, QByteArray& payload )
{
	if( m_ready == false || encryptedPayload.size() < Overhead )
	{
		return false;
	}
	const auto cipherSize = int( encryptedPayload.size() ) - TagSize;
	QByteArray plain( cipherSize, Qt::Uninitialized );
	QByteArray tag = encryptedPayload.right( TagSize );
	auto ctx = EVP_CIPHER_CTX_new();
	int length = 0;
	const auto nonce = iv( m_receiveCounter );
	bool ok = ctx &&
			  EVP_DecryptInit_ex( ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr ) == 1 &&
			  EVP_CIPHER_CTX_ctrl( ctx, EVP_CTRL_GCM_SET_IVLEN, IvSize, nullptr ) == 1 &&
			  EVP_DecryptInit_ex( ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>( m_receiveKey.constData() ),
								  reinterpret_cast<const unsigned char*>( nonce.constData() ) ) == 1 &&
			  EVP_DecryptUpdate( ctx, nullptr, &length, reinterpret_cast<const unsigned char*>( Aad.constData() ),
								 int( Aad.size() ) ) == 1 &&
			  EVP_DecryptUpdate( ctx, reinterpret_cast<unsigned char*>( plain.data() ), &length,
								 reinterpret_cast<const unsigned char*>( encryptedPayload.constData() ), cipherSize ) == 1 &&
			  EVP_CIPHER_CTX_ctrl( ctx, EVP_CTRL_GCM_SET_TAG, TagSize, tag.data() ) == 1;
	int finalLength = 0;
	// Final fails if the tag does not match: the frame was modified, replayed or is out of order.
	ok = ok && EVP_DecryptFinal_ex( ctx, reinterpret_cast<unsigned char*>( plain.data() ) + length, &finalLength ) == 1;
	EVP_CIPHER_CTX_free( ctx );
	if( ok == false || plain.isEmpty() )
	{
		return false;
	}
	++m_receiveCounter;
	innerType = FrameType( quint8( plain.at( 0 ) ) );
	payload = plain.mid( 1 );
	return true;
}
