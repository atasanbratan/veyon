/*
 * LabAudioConfigurationPage.cpp - "Lab audio" page in the Veyon Configurator
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

#include "Configuration/UiMapping.h"
#include "FileSystemBrowser.h"
#include "LabAudioConfiguration.h"
#include "LabAudioConfigurationPage.h"

#include "ui_LabAudioConfigurationPage.h"



LabAudioConfigurationPage::LabAudioConfigurationPage( LabAudioConfiguration& configuration, QWidget* parent ) :
	ConfigurationPage( parent ),
	ui( new Ui::LabAudioConfigurationPage ),
	m_configuration( configuration )
{
	ui->setupUi( this );

	connect( ui->browseRecordingsFolder, &QAbstractButton::clicked, this, [this]() {
		FileSystemBrowser( FileSystemBrowser::ExistingDirectory, this ).exec( ui->recordingsFolder );
	} );
}



LabAudioConfigurationPage::~LabAudioConfigurationPage()
{
	delete ui;
}



void LabAudioConfigurationPage::resetWidgets()
{
	FOREACH_LAB_AUDIO_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
}



void LabAudioConfigurationPage::connectWidgetsToProperties()
{
	FOREACH_LAB_AUDIO_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY)
}



void LabAudioConfigurationPage::applyConfiguration()
{
}
