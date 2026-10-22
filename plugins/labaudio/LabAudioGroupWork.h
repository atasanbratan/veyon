/*
 * LabAudioGroupWork.h - teacher dialogs for pair and group work
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

#include <QDialog>
#include <QList>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QGridLayout;
class QKeyEvent;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

// Asks the teacher for the group size and shows which students end up together.
class LabAudioGroupDialog : public QDialog
{
	Q_OBJECT
public:
	LabAudioGroupDialog( const QStringList& students, QWidget* parent = nullptr );

	int groupSize() const;
	bool isRandom() const;
	// Groups as indices into the student list.
	const QList<QList<int>>& groups() const
	{
		return m_groups;
	}

	static QList<QList<int>> makeGroups( int studentCount, int groupSize, bool random );

private:
	void updatePreview( bool reshuffle );

	QStringList m_students;
	QSpinBox* m_size;
	QCheckBox* m_random;
	QListWidget* m_preview;
	QList<QList<int>> m_groups;
};

// Teacher microphone: mute, or push-to-talk (hold the button or the Space key).
class LabAudioMicControls : public QWidget
{
	Q_OBJECT
public:
	explicit LabAudioMicControls( QWidget* parent = nullptr );

	bool isMuted() const;
	// Space key while the panel is active (auto-repeat is ignored by the caller).
	void setHolding( bool holding );

Q_SIGNALS:
	void mutedChanged( bool muted );

private:
	void refresh();

	QPushButton* m_mute;
	QCheckBox* m_pushToTalk;
	QPushButton* m_hold;
	bool m_holding{false};
	bool m_lastMuted{false};
};

// Small window next to Veyon Master while the teacher speaks to students or talks with one.
class LabAudioTeacherPanel : public QWidget
{
	Q_OBJECT
public:
	explicit LabAudioTeacherPanel( QWidget* parent = nullptr );

	void setMode( const QString& description, const QString& stopHint );
	// Listening only: the teacher's microphone is not used.
	void setMicAvailable( bool available );
	void setRecording( bool recording );
	LabAudioMicControls* micControls() const
	{
		return m_mic;
	}

Q_SIGNALS:
	void recordToggled( bool record );
	void openRecordingsRequested();

protected:
	void closeEvent( QCloseEvent* event ) override;
	void keyPressEvent( QKeyEvent* event ) override;
	void keyReleaseEvent( QKeyEvent* event ) override;

private:
	QLabel* m_mode;
	QLabel* m_hint;
	LabAudioMicControls* m_mic;
	QPushButton* m_record;
};

// Stays next to Veyon Master during group work: lets the teacher listen to or join one group.
class LabAudioGroupPanel : public QWidget
{
	Q_OBJECT
public:
	explicit LabAudioGroupPanel( QWidget* parent = nullptr );

	LabAudioMicControls* micControls() const
	{
		return m_mic;
	}

	// One entry per group (group id = index + 1) with the member names.
	void setGroups( const QList<QStringList>& groups );
	void setFocus( int groupId, bool talking );
	void setRecording( bool recording );

Q_SIGNALS:
	// groupId < 0: stop listening/talking
	void focusRequested( int groupId, bool talk );
	void regroupRequested();
	void recordToggled( bool record );
	void openRecordingsRequested();

protected:
	void closeEvent( QCloseEvent* event ) override;
	void keyPressEvent( QKeyEvent* event ) override;
	void keyReleaseEvent( QKeyEvent* event ) override;

private:
	struct Row
	{
		QLabel* label;
		QPushButton* listen;
		QPushButton* talk;
	};

	QLabel* m_state;
	QGridLayout* m_grid;
	LabAudioMicControls* m_mic;
	QPushButton* m_record;
	QList<Row> m_rows;
	int m_focusGroup{-1};
	bool m_talking{false};
};
