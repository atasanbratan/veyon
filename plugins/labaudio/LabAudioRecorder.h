/*
 * LabAudioRecorder.h - records live lab audio to Ogg Opus files
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

#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QObject>
#include <QTimer>

#include "LabAudioEngine.h"

struct OpusEncoder;

// Writes 20 ms Opus packets (48 kHz mono) into an Ogg Opus file (RFC 3533 / RFC 7845).
class LabAudioOggOpusWriter
{
public:
	~LabAudioOggOpusWriter();

	bool open( const QString& path, const QString& title, int preSkip );
	void writePacket( const QByteArray& opusPacket );
	void close();
	bool isOpen() const
	{
		return m_file.isOpen();
	}

	// Ogg page checksum (CRC-32, polynomial 0x04c11db7, no reflection, initial value 0).
	static quint32 crc( const QByteArray& data );

private:
	void writePage( const QList<QByteArray>& packets, quint8 headerType, qint64 granule );
	void flush( bool last );

	QFile m_file;
	quint32 m_serial{0};
	quint32 m_pageSequence{0};
	int m_preSkip{0};
	qint64 m_samples{0};
	QList<QByteArray> m_pending;
	int m_pendingSegments{0};
};

// Mixes live streams (teacher and/or students) in real time and records them into one file.
class LabAudioRecorder : public QObject
{
	Q_OBJECT
public:
	LabAudioRecorder( const QString& path, const QString& title, int bitRate = DefaultBitRate, QObject* parent = nullptr );
	~LabAudioRecorder() override;

	bool isRecording() const
	{
		return m_writer.isOpen();
	}
	const QString& path() const
	{
		return m_path;
	}
	const QString& title() const
	{
		return m_title;
	}
	qint64 recordedFrames() const
	{
		return m_frames;
	}

	void addPacket( int streamId, quint32 sequence, const QByteArray& opusPacket );
	void removeStream( int streamId );

	// The configured folder (environment variables like %USERNAME% expanded) or, if none is configured,
	// Documents\Lab audio recordings of the logged-in user; created if needed.
	static QString recordingsFolder();
	static void setConfiguredFolder( const QString& folder );
	// "<folder>/2026-10-22 13-40-05 Group 2 - Anna, Ben.opus"; characters not allowed in file names are replaced.
	static QString newRecordingPath( const QString& label );

	static constexpr int DefaultBitRate = 32000;

private:
	void tick();

	QString m_path;
	QString m_title;
	LabAudioMixer m_mixer;
	OpusEncoder* m_encoder{nullptr};
	LabAudioOggOpusWriter m_writer;
	QTimer m_timer;
	QElapsedTimer m_clock;
	qint64 m_frames{0};
};
