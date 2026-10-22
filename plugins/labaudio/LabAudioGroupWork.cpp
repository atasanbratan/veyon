/*
 * LabAudioGroupWork.cpp - teacher dialogs for pair and group work
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

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "LabAudioGroupWork.h"
#include "LabAudioProtocol.h"



LabAudioGroupDialog::LabAudioGroupDialog( const QStringList& students, QWidget* parent ) :
	QDialog( parent ),
	m_students( students ),
	m_size( new QSpinBox( this ) ),
	m_random( new QCheckBox( tr( "Mix students randomly" ), this ) ),
	m_preview( new QListWidget( this ) )
{
	setWindowTitle( tr( "Group work" ) );

	auto layout = new QVBoxLayout( this );
	auto form = new QFormLayout;
	m_size->setRange( 2, qMax( 2, qMin( 6, int( students.size() ) ) ) );
	m_size->setValue( 2 );
	m_size->setSuffix( tr( " students per group" ) );
	form->addRow( tr( "Group size:" ), m_size );
	form->addRow( QString(), m_random );
	m_random->setChecked( true );
	layout->addLayout( form );

	auto shuffle = new QPushButton( tr( "Mix again" ), this );
	layout->addWidget( new QLabel( tr( "Students who talk with each other:" ), this ) );
	layout->addWidget( m_preview );
	layout->addWidget( shuffle );

	auto buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
	buttons->button( QDialogButtonBox::Ok )->setText( tr( "Start group work" ) );
	layout->addWidget( buttons );

	connect( m_size, &QSpinBox::valueChanged, this, [this]() { updatePreview( true ); } );
	connect( m_random, &QCheckBox::toggled, this, [this, shuffle]( bool random ) {
		shuffle->setEnabled( random );
		updatePreview( true );
	} );
	connect( shuffle, &QPushButton::clicked, this, [this]() { updatePreview( true ); } );
	connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );

	resize( 420, 420 );
	updatePreview( true );
}



int LabAudioGroupDialog::groupSize() const
{
	return m_size->value();
}



bool LabAudioGroupDialog::isRandom() const
{
	return m_random->isChecked();
}



QList<QList<int>> LabAudioGroupDialog::makeGroups( int studentCount, int groupSize, bool random )
{
	QList<int> order;
	for( int i = 0; i < studentCount; ++i )
	{
		order.append( i );
	}
	if( random )
	{
		std::shuffle( order.begin(), order.end(), *QRandomGenerator::global() );
	}
	return LabAudioProtocol::makeGroups( order, groupSize );
}



void LabAudioGroupDialog::updatePreview( bool reshuffle )
{
	if( reshuffle )
	{
		m_groups = makeGroups( int( m_students.size() ), groupSize(), isRandom() );
	}
	m_preview->clear();
	for( int g = 0; g < m_groups.size(); ++g )
	{
		QStringList names;
		for( const auto index : m_groups[g] )
		{
			names.append( m_students.value( index ) );
		}
		m_preview->addItem( tr( "Group %1: %2" ).arg( g + 1 ).arg( names.join( QStringLiteral( ", " ) ) ) );
	}
}



namespace
{

// Space = push-to-talk while a teacher panel is the active window.
bool handleSpace( LabAudioMicControls* mic, QKeyEvent* event, bool pressed )
{
	if( event->key() != Qt::Key_Space )
	{
		return false;
	}
	if( event->isAutoRepeat() == false )
	{
		mic->setHolding( pressed );
	}
	event->accept();
	return true;
}

// "● Record" toggle and "Recordings…" (opens the folder) side by side.
// "● 28 of 30 students connected – waiting for: Anna, Ben" (orange) or "✔ All 30 students connected" (green).
void showConnectionStatus( QLabel* label, int connected, int total, const QStringList& waiting )
{
	if( total <= 0 )
	{
		label->hide();
		return;
	}
	if( connected >= total )
	{
		label->setText( total == 1 ? QObject::tr( "✔ The student is connected" )
								   : QObject::tr( "✔ All %1 students connected" ).arg( total ) );
		label->setStyleSheet( QStringLiteral( "color: #16a34a; font-weight: bold;" ) );
	}
	else
	{
		constexpr int MaxNames = 5;
		auto names = QStringList( waiting.mid( 0, MaxNames ) ).join( QStringLiteral( ", " ) );
		if( waiting.size() > MaxNames )
		{
			names += QObject::tr( " and %n more", nullptr, int( waiting.size() ) - MaxNames );
		}
		label->setText( QObject::tr( "⏳ %1 of %2 students connected – waiting for: %3" )
							.arg( connected ).arg( total ).arg( names ) );
		label->setStyleSheet( QStringLiteral( "color: #d97706; font-weight: bold;" ) );
	}
	label->show();
}

QHBoxLayout* recordRow( QWidget* parent, QPushButton*& record, const QString& text, const QString& toolTip,
					   const std::function<void( bool )>& toggled, const std::function<void()>& openFolder )
{
	auto row = new QHBoxLayout;
	record = new QPushButton( text, parent );
	record->setCheckable( true );
	record->setFocusPolicy( Qt::NoFocus );
	record->setToolTip( toolTip );
	record->setStyleSheet( QStringLiteral( "QPushButton:checked { color: white; background: #b91c1c; }" ) );
	QObject::connect( record, &QPushButton::toggled, parent, toggled );
	auto folder = new QPushButton( QObject::tr( "Recordings…" ), parent );
	folder->setFocusPolicy( Qt::NoFocus );
	folder->setToolTip( QObject::tr( "Open the folder with the recordings (Documents, \"Lab audio recordings\")" ) );
	QObject::connect( folder, &QPushButton::clicked, parent, openFolder );
	row->addWidget( record, 1 );
	row->addWidget( folder );
	return row;
}

}



LabAudioMicControls::LabAudioMicControls( QWidget* parent ) :
	QWidget( parent ),
	m_mute( new QPushButton( this ) ),
	m_pushToTalk( new QCheckBox( tr( "Push to talk" ), this ) ),
	m_hold( new QPushButton( tr( "Hold to talk (or Space)" ), this ) )
{
	auto layout = new QHBoxLayout( this );
	layout->setContentsMargins( 0, 0, 0, 0 );
	layout->addWidget( m_mute );
	layout->addWidget( m_pushToTalk );
	layout->addWidget( m_hold, 1 );

	// Without keyboard focus on the buttons, Space reaches the panel for push-to-talk.
	m_mute->setFocusPolicy( Qt::NoFocus );
	m_pushToTalk->setFocusPolicy( Qt::NoFocus );
	m_hold->setFocusPolicy( Qt::NoFocus );
	m_mute->setCheckable( true );
	m_pushToTalk->setToolTip( tr( "Students only hear you while you hold the button or the Space key." ) );

	connect( m_mute, &QPushButton::toggled, this, &LabAudioMicControls::refresh );
	connect( m_pushToTalk, &QCheckBox::toggled, this, &LabAudioMicControls::refresh );
	connect( m_hold, &QPushButton::pressed, this, [this]() { setHolding( true ); } );
	connect( m_hold, &QPushButton::released, this, [this]() { setHolding( false ); } );
	refresh();
}



bool LabAudioMicControls::isMuted() const
{
	return m_mute->isChecked() || ( m_pushToTalk->isChecked() && m_holding == false );
}



void LabAudioMicControls::setHolding( bool holding )
{
	if( m_pushToTalk->isChecked() && holding != m_holding )
	{
		m_holding = holding;
		refresh();
	}
}



void LabAudioMicControls::refresh()
{
	if( m_pushToTalk->isChecked() == false )
	{
		m_holding = false;
	}
	m_hold->setVisible( m_pushToTalk->isChecked() );
	m_mute->setText( m_mute->isChecked() ? tr( "Microphone off" ) : tr( "Microphone on" ) );
	m_mute->setStyleSheet( m_mute->isChecked() ? QStringLiteral( "QPushButton { color: white; background: #b91c1c; }" )
											   : QString() );
	m_hold->setText( m_holding ? tr( "Talking…" ) : tr( "Hold to talk (or Space)" ) );

	const auto muted = isMuted();
	if( muted != m_lastMuted )
	{
		m_lastMuted = muted;
		Q_EMIT mutedChanged( muted );
	}
}



LabAudioTeacherPanel::LabAudioTeacherPanel( QWidget* parent ) :
	QWidget( parent, Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint ),
	m_mode( new QLabel( this ) ),
	m_hint( new QLabel( this ) ),
	m_connection( new QLabel( this ) ),
	m_mic( new LabAudioMicControls( this ) )
{
	setWindowTitle( tr( "Lab audio" ) );
	setMinimumWidth( 420 );
	auto layout = new QVBoxLayout( this );
	m_mode->setWordWrap( true );
	m_hint->setWordWrap( true );
	m_hint->setEnabled( false );
	m_connection->setWordWrap( true );
	m_connection->hide();
	layout->addWidget( m_mode );
	layout->addWidget( m_connection );
	layout->addWidget( m_mic );
	layout->addLayout( recordRow( this, m_record, tr( "● Record" ),
								  tr( "Record this conversation to a file on this computer. The students see that "
									  "they are being recorded." ),
								  [this]( bool record ) { Q_EMIT recordToggled( record ); },
								  [this]() { Q_EMIT openRecordingsRequested(); } ) );
	layout->addWidget( m_hint );
}



void LabAudioTeacherPanel::setConnectionStatus( int connected, int total, const QStringList& waiting )
{
	showConnectionStatus( m_connection, connected, total, waiting );
	adjustSize();
}



void LabAudioTeacherPanel::setMicAvailable( bool available )
{
	m_mic->setVisible( available );
	adjustSize();
}



void LabAudioTeacherPanel::setRecording( bool recording )
{
	const QSignalBlocker blocker( m_record );
	m_record->setChecked( recording );
	m_record->setText( recording ? tr( "● Recording… (click to stop)" ) : tr( "● Record" ) );
}



void LabAudioTeacherPanel::setMode( const QString& description, const QString& stopHint )
{
	m_mode->setText( QStringLiteral( "<b>%1</b>" ).arg( description.toHtmlEscaped() ) );
	m_hint->setText( stopHint );
	adjustSize();
}



void LabAudioTeacherPanel::closeEvent( QCloseEvent* event )
{
	// The mode ends from the Veyon Master toolbar; programmatic closes are allowed.
	if( event->spontaneous() )
	{
		event->ignore();
	}
	else
	{
		event->accept();
	}
}



void LabAudioTeacherPanel::keyPressEvent( QKeyEvent* event )
{
	if( handleSpace( m_mic, event, true ) == false )
	{
		QWidget::keyPressEvent( event );
	}
}



void LabAudioTeacherPanel::keyReleaseEvent( QKeyEvent* event )
{
	if( handleSpace( m_mic, event, false ) == false )
	{
		QWidget::keyReleaseEvent( event );
	}
}



LabAudioGroupPanel::LabAudioGroupPanel( QWidget* parent ) :
	QWidget( parent, Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint ),
	m_state( new QLabel( this ) ),
	m_grid( new QGridLayout ),
	m_connection( new QLabel( this ) ),
	m_mic( new LabAudioMicControls( this ) )
{
	setWindowTitle( tr( "Group work" ) );
	setMinimumWidth( 480 );

	auto layout = new QVBoxLayout( this );
	m_state->setWordWrap( true );
	m_connection->setWordWrap( true );
	m_connection->hide();
	layout->addWidget( m_state );
	layout->addWidget( m_connection );
	layout->addLayout( m_grid );
	m_mic->setToolTip( tr( "Your microphone while you talk with a group" ) );
	layout->addWidget( m_mic );

	auto regroup = new QPushButton( tr( "New groups" ), this );
	regroup->setToolTip( tr( "Mix the students into new groups of the same size." ) );
	connect( regroup, &QPushButton::clicked, this, &LabAudioGroupPanel::regroupRequested );
	layout->addWidget( regroup );
	layout->addLayout( recordRow( this, m_record, tr( "● Record each group" ),
								  tr( "Record every group's conversation to its own file on this computer (e.g. oral exams). "
									  "The students see that they are being recorded." ),
								  [this]( bool record ) { Q_EMIT recordToggled( record ); },
								  [this]() { Q_EMIT openRecordingsRequested(); } ) );

	auto hint = new QLabel( tr( "To end group work, click \"Group work\" in the toolbar again." ), this );
	hint->setWordWrap( true );
	hint->setEnabled( false );
	layout->addWidget( hint );

	setFocus( -1, false );
}



void LabAudioGroupPanel::setGroups( const QList<QStringList>& groups )
{
	for( const auto& row : std::as_const( m_rows ) )
	{
		delete row.label;
		delete row.listen;
		delete row.talk;
	}
	m_rows.clear();

	for( int g = 0; g < groups.size(); ++g )
	{
		const int groupId = g + 1;
		Row row{ new QLabel( tr( "<b>Group %1</b>: %2" ).arg( groupId ).arg( groups[g].join( QStringLiteral( ", " ) ).toHtmlEscaped() ), this ),
				 new QPushButton( tr( "Listen" ), this ),
				 new QPushButton( tr( "Talk" ), this ) };
		row.label->setWordWrap( true );
		row.listen->setCheckable( true );
		row.talk->setCheckable( true );
		row.listen->setToolTip( tr( "Hear this group. The students see that you are listening." ) );
		row.talk->setToolTip( tr( "Join this group's conversation with your microphone." ) );
		connect( row.listen, &QPushButton::clicked, this, [this, groupId]() {
			Q_EMIT focusRequested( ( m_focusGroup == groupId && m_talking == false ) ? -1 : groupId, false );
		} );
		connect( row.talk, &QPushButton::clicked, this, [this, groupId]() {
			Q_EMIT focusRequested( ( m_focusGroup == groupId && m_talking ) ? -1 : groupId, true );
		} );
		m_grid->addWidget( row.label, g, 0 );
		m_grid->addWidget( row.listen, g, 1 );
		m_grid->addWidget( row.talk, g, 2 );
		m_rows.append( row );
	}
	m_grid->setColumnStretch( 0, 1 );
	setFocus( m_focusGroup, m_talking );
	adjustSize();
}



void LabAudioGroupPanel::setFocus( int groupId, bool talking )
{
	m_focusGroup = groupId;
	m_talking = groupId > 0 && talking;
	for( int g = 0; g < m_rows.size(); ++g )
	{
		const bool focused = ( g + 1 ) == m_focusGroup;
		m_rows[g].listen->setChecked( focused && m_talking == false );
		m_rows[g].talk->setChecked( focused && m_talking );
	}
	if( m_focusGroup <= 0 )
	{
		m_state->setText( m_rows.size() == 1
							  ? tr( "1 group is talking. Choose a group to listen to or to join." )
							  : tr( "%1 groups are talking. Choose a group to listen to or to join." ).arg( m_rows.size() ) );
	}
	else if( m_talking )
	{
		m_state->setText( tr( "You are talking with group %1." ).arg( m_focusGroup ) );
	}
	else
	{
		m_state->setText( tr( "You are listening to group %1." ).arg( m_focusGroup ) );
	}
}



void LabAudioGroupPanel::setConnectionStatus( int connected, int total, const QStringList& waiting )
{
	showConnectionStatus( m_connection, connected, total, waiting );
	adjustSize();
}



void LabAudioGroupPanel::setRecording( bool recording )
{
	const QSignalBlocker blocker( m_record );
	m_record->setChecked( recording );
	m_record->setText( recording ? tr( "● Recording each group… (click to stop)" ) : tr( "● Record each group" ) );
}



void LabAudioGroupPanel::keyPressEvent( QKeyEvent* event )
{
	if( handleSpace( m_mic, event, true ) == false )
	{
		QWidget::keyPressEvent( event );
	}
}



void LabAudioGroupPanel::keyReleaseEvent( QKeyEvent* event )
{
	if( handleSpace( m_mic, event, false ) == false )
	{
		QWidget::keyReleaseEvent( event );
	}
}



void LabAudioGroupPanel::closeEvent( QCloseEvent* event )
{
	// Group work ends from the Veyon Master toolbar; programmatic closes are allowed.
	if( event->spontaneous() )
	{
		event->ignore();
	}
	else
	{
		event->accept();
	}
}
