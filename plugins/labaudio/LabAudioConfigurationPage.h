/*
 * LabAudioConfigurationPage.h - "Lab audio" page in the Veyon Configurator
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

#include "ConfigurationPage.h"

namespace Ui {
class LabAudioConfigurationPage;
}

class LabAudioConfiguration;

class LabAudioConfigurationPage : public ConfigurationPage
{
	Q_OBJECT
public:
	explicit LabAudioConfigurationPage( LabAudioConfiguration& configuration, QWidget* parent = nullptr );
	~LabAudioConfigurationPage() override;

	void resetWidgets() override;
	void connectWidgetsToProperties() override;
	void applyConfiguration() override;

private:
	Ui::LabAudioConfigurationPage* ui;
	LabAudioConfiguration& m_configuration;
};
