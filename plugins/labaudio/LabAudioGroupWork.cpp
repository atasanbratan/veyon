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
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

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



LabAudioGroupPanel::LabAudioGroupPanel( QWidget* parent ) :
	QWidget( parent, Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint ),
	m_state( new QLabel( this ) ),
	m_grid( new QGridLayout )
{
	setWindowTitle( tr( "Group work" ) );
	setMinimumWidth( 480 );

	auto layout = new QVBoxLayout( this );
	m_state->setWordWrap( true );
	layout->addWidget( m_state );
	layout->addLayout( m_grid );

	auto regroup = new QPushButton( tr( "New groups" ), this );
	regroup->setToolTip( tr( "Mix the students into new groups of the same size." ) );
	connect( regroup, &QPushButton::clicked, this, &LabAudioGroupPanel::regroupRequested );
	layout->addWidget( regroup );

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
		m_state->setText( tr( "%n group(s) are talking. Choose a group to listen to or to join.", nullptr,
							  int( m_rows.size() ) ) );
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
