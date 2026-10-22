/*
 * LabAudioCalls.cpp - students calling the teacher: teacher window and student tray icon
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

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

#include "LabAudioCalls.h"



LabAudioCallsWindow::LabAudioCallsWindow( QWidget* parent ) :
	QWidget( parent, Qt::Tool | Qt::WindowStaysOnTopHint ),
	m_empty( new QLabel( tr( "No student is calling." ), this ) ),
	m_rows( new QVBoxLayout ),
	m_allowed( new QCheckBox( tr( "Students may call the teacher" ), this ) )
{
	setWindowTitle( tr( "Students calling" ) );
	setMinimumWidth( 380 );
	auto layout = new QVBoxLayout( this );
	m_empty->setEnabled( false );
	layout->addWidget( m_empty );
	layout->addLayout( m_rows );

	auto bottom = new QHBoxLayout;
	auto clearAll = new QPushButton( tr( "All done" ), this );
	clearAll->setToolTip( tr( "Answer all calls" ) );
	m_allowed->setToolTip( tr( "Turn off, for example during a test: students then cannot call." ) );
	m_allowed->setChecked( true );
	bottom->addWidget( m_allowed, 1 );
	bottom->addWidget( clearAll );
	layout->addLayout( bottom );

	connect( clearAll, &QPushButton::clicked, this, &LabAudioCallsWindow::answeredAll );
	connect( m_allowed, &QCheckBox::toggled, this, &LabAudioCallsWindow::callsAllowedChanged );
}



void LabAudioCallsWindow::setCallsAllowed( bool allowed )
{
	const QSignalBlocker blocker( m_allowed );
	m_allowed->setChecked( allowed );
}



void LabAudioCallsWindow::present()
{
	if( isVisible() == false )
	{
		adjustSize();
		if( const auto screen = QGuiApplication::primaryScreen() )
		{
			const auto area = screen->availableGeometry();
			move( area.right() - width() - 20, area.top() + 60 );
		}
		show();
	}
	raise();
}



void LabAudioCallsWindow::setCalls( const QList<LabAudioCall>& calls )
{
	while( auto item = m_rows->takeAt( 0 ) )
	{
		if( item->widget() )
		{
			item->widget()->deleteLater();
		}
		delete item;
	}

	for( const auto& call : calls )
	{
		auto row = new QWidget( this );
		auto rowLayout = new QHBoxLayout( row );
		rowLayout->setContentsMargins( 0, 0, 0, 0 );
		auto text = call.name.toHtmlEscaped();
		if( call.group > 0 )
		{
			text += tr( " · group %1" ).arg( call.group );
		}
		auto label = new QLabel( QStringLiteral( "✋ <b>%1</b> &nbsp; %2" )
									 .arg( text, call.time.toLocalTime().toString( QStringLiteral( "HH:mm" ) ) ), row );
		auto done = new QPushButton( tr( "Done" ), row );
		done->setToolTip( tr( "Answer the call: the student's button is reset." ) );
		const auto id = call.id;
		connect( done, &QPushButton::clicked, this, [this, id]() { Q_EMIT answered( id ); } );
		rowLayout->addWidget( label, 1 );
		rowLayout->addWidget( done );
		m_rows->addWidget( row );
	}
	m_empty->setVisible( calls.isEmpty() );
	adjustSize();

	if( calls.size() > m_shownCalls )
	{
		// A new call: show the window and draw attention without stealing keyboard focus.
		present();
		QApplication::beep();
		QApplication::alert( this );
	}
	m_shownCalls = int( calls.size() );
}



LabAudioCallTray::LabAudioCallTray( QObject* parent ) :
	QObject( parent ),
	m_tray( new QSystemTrayIcon( this ) ),
	m_action( new QAction( this ) )
{
	auto menu = new QMenu;
	menu->addAction( m_action );
	m_tray->setContextMenu( menu );
	connect( m_action, &QAction::triggered, this, &LabAudioCallTray::toggle );
	connect( m_tray, &QSystemTrayIcon::activated, this, [this]( QSystemTrayIcon::ActivationReason reason ) {
		if( reason == QSystemTrayIcon::Trigger )
		{
			toggle();
		}
	} );
	update();
	m_tray->show();
}



LabAudioCallTray::~LabAudioCallTray()
{
	delete m_tray->contextMenu();
}



void LabAudioCallTray::setState( bool calling, bool allowed )
{
	const bool answered = m_calling && calling == false && allowed;
	m_calling = calling;
	m_allowed = allowed;
	update();
	if( answered )
	{
		m_tray->showMessage( tr( "Call the teacher" ), tr( "The teacher has seen your call." ),
							 QSystemTrayIcon::Information, 4000 );
	}
}



void LabAudioCallTray::toggle()
{
	if( m_allowed == false )
	{
		m_tray->showMessage( tr( "Call the teacher" ), tr( "Calling is turned off by the teacher right now." ),
							 QSystemTrayIcon::Information, 4000 );
		return;
	}
	// The server confirms with the new state; until then show it right away.
	m_calling = !m_calling;
	update();
	Q_EMIT callToggled( m_calling );
}



void LabAudioCallTray::update()
{
	const QIcon idle( QStringLiteral( ":/labaudio/hand.png" ) );
	if( m_allowed == false )
	{
		m_tray->setIcon( QIcon( idle.pixmap( 64, QIcon::Disabled ) ) );
		m_tray->setToolTip( tr( "Calling the teacher is turned off" ) );
		m_action->setText( tr( "Calling the teacher is turned off" ) );
		m_action->setEnabled( false );
		return;
	}
	m_action->setEnabled( true );
	if( m_calling )
	{
		m_tray->setIcon( QIcon( QStringLiteral( ":/labaudio/hand-calling.png" ) ) );
		m_tray->setToolTip( tr( "Calling the teacher… (click to cancel)" ) );
		m_action->setText( tr( "Cancel the call" ) );
	}
	else
	{
		m_tray->setIcon( idle );
		m_tray->setToolTip( tr( "Call the teacher (click)" ) );
		m_action->setText( tr( "Call the teacher" ) );
	}
}
