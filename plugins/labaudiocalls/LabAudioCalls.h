/*
 * LabAudioCalls.h - students calling the teacher: teacher window and student tray icon
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

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QWidget>

class QAction;
class QCheckBox;
class QLabel;
class QSystemTrayIcon;
class QVBoxLayout;

// A student who pressed "Call the teacher".
struct LabAudioCall
{
	int id;
	QString name;
	int group;	// 0: not in group work
	QDateTime time;
};

// Veyon Master: lists students who are calling; "Done" answers a call. Also holds the
// "Students may call" switch.
class LabAudioCallsWindow : public QWidget
{
	Q_OBJECT
public:
	explicit LabAudioCallsWindow( QWidget* parent = nullptr );

	void setCalls( const QList<LabAudioCall>& calls );
	void setCallsAllowed( bool allowed );
	// Shows the window even without calls (opened from the toolbar).
	void present();

Q_SIGNALS:
	void answered( int id );
	void answeredAll();
	void callsAllowedChanged( bool allowed );

private:
	QLabel* m_empty;
	QVBoxLayout* m_rows;
	QCheckBox* m_allowed;
	int m_shownCalls{0};
};

// Student computer: tray icon to call the teacher at any time.
class LabAudioCallTray : public QObject
{
	Q_OBJECT
public:
	explicit LabAudioCallTray( QObject* parent = nullptr );
	~LabAudioCallTray() override;

	void setState( bool calling, bool allowed );

Q_SIGNALS:
	void callToggled( bool calling );

private:
	void toggle();
	void update();

	QSystemTrayIcon* m_tray;
	QAction* m_action;
	bool m_calling{false};
	bool m_allowed{true};
};
