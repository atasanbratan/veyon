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

// Stays next to Veyon Master during group work: lets the teacher listen to or join one group.
class LabAudioGroupPanel : public QWidget
{
	Q_OBJECT
public:
	explicit LabAudioGroupPanel( QWidget* parent = nullptr );

	// One entry per group (group id = index + 1) with the member names.
	void setGroups( const QList<QStringList>& groups );
	void setFocus( int groupId, bool talking );

Q_SIGNALS:
	// groupId < 0: stop listening/talking
	void focusRequested( int groupId, bool talk );
	void regroupRequested();

protected:
	void closeEvent( QCloseEvent* event ) override;

private:
	struct Row
	{
		QLabel* label;
		QPushButton* listen;
		QPushButton* talk;
	};

	QLabel* m_state;
	QGridLayout* m_grid;
	QList<Row> m_rows;
	int m_focusGroup{-1};
	bool m_talking{false};
};
