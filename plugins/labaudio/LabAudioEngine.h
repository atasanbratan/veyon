/*
 * LabAudioEngine.h - microphone capture/Opus encoding and decoding/mixing playback
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

#include <QAudioFormat>
#include <QHash>
#include <QIODevice>
#include <QObject>
#include <QVector>
#include <atomic>
#include <deque>

class QAudioSink;
class QAudioSource;
struct OpusEncoder;
struct OpusDecoder;

// Captures the default microphone, converts to 48 kHz mono and emits one Opus packet per 20 ms.
class LabAudioCapture : public QObject
{
	Q_OBJECT
public:
	explicit LabAudioCapture( QObject* parent = nullptr );
	~LabAudioCapture() override;

	bool start();
	void stop();
	bool isRunning() const
	{
		return m_source != nullptr;
	}
	QString errorString() const
	{
		return m_error;
	}
	void setMuted( bool muted )
	{
		m_muted = muted;
	}
	bool isMuted() const
	{
		return m_muted;
	}

Q_SIGNALS:
	void packetReady( const QByteArray& opusPacket );
	void levelChanged( float level );	// 0..1 peak of the last frame
	void failed( const QString& error );

private:
	void readAvailable();
	void encodeFrames();

	QAudioSource* m_source{nullptr};
	QIODevice* m_io{nullptr};
	QAudioFormat m_format;
	OpusEncoder* m_encoder{nullptr};
	QVector<qint16> m_pending;	// 48 kHz mono samples not yet encoded
	double m_resamplePosition{0};
	qint16 m_lastSample{0};
	bool m_muted{false};
	QString m_error;
};

// Pull device for QAudioSink: decodes each stream into a small jitter buffer and mixes them.
class LabAudioMixer : public QIODevice
{
	Q_OBJECT
public:
	explicit LabAudioMixer( QObject* parent = nullptr );
	~LabAudioMixer() override;

	void setOutputFormat( const QAudioFormat& format );
	void addPacket( int streamId, quint32 sequence, const QByteArray& opusPacket );
	void removeStream( int streamId );
	int streamCount() const
	{
		return int( m_streams.size() );
	}
	// Current jitter buffer target of a stream in frames (0 for an unknown stream).
	int targetFrames( int streamId ) const;
	// Output volume, 1.0 = unchanged (0..2).
	void setGain( float gain )
	{
		m_gain = qBound( 0.0f, gain, 2.0f );
	}

	bool isSequential() const override
	{
		return true;
	}
	qint64 bytesAvailable() const override;

protected:
	qint64 readData( char* data, qint64 maxSize ) override;
	qint64 writeData( const char* data, qint64 maxSize ) override;

private:
	struct Stream
	{
		OpusDecoder* decoder{nullptr};
		std::deque<qint16> samples;	// decoded 48 kHz mono
		quint32 nextSequence{0};
		bool primed{false};
		bool initialized{false};
		int targetFrames{MinTargetFrames};
		qint64 samplesSinceUnderflow{0};
	};

	qint16 mixedSample();

	// TCP never loses audio, so every gap is network jitter (e.g. Wi-Fi bursts). The buffer target
	// grows after each underflow and shrinks again slowly while playback stays smooth.
	static constexpr int MinTargetFrames = 3;			// 60 ms
	static constexpr int MaxTargetFrames = 15;			// 300 ms
	static constexpr int UnderflowGrowFrames = 2;
	static constexpr int ShrinkAfterSamples = 15 * 48000;	// 15 s without underflow
	static constexpr int BacklogFrames = 10;			// drop backlog beyond target + 200 ms

	QHash<int, Stream> m_streams;
	QAudioFormat m_format;
	double m_resamplePosition{0};
	qint16 m_currentSample{0};
	qint16 m_nextSample{0};
	std::atomic<float> m_gain{1.0f};
};

// Plays the mixer on the default output device.
class LabAudioPlayback : public QObject
{
	Q_OBJECT
public:
	explicit LabAudioPlayback( QObject* parent = nullptr );
	~LabAudioPlayback() override;

	bool start();
	void stop();
	LabAudioMixer& mixer()
	{
		return *m_mixer;
	}
	QString errorString() const
	{
		return m_error;
	}

private:
	QAudioSink* m_sink{nullptr};
	LabAudioMixer* m_mixer{nullptr};
	QString m_error;
};
