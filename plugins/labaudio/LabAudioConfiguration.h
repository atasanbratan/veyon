/*
 * LabAudioConfiguration.h - settings of the LabAudio and LabAudioCalls plugins (Veyon Configurator)
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

#include "VeyonConfiguration.h"
#include "Configuration/Proxy.h"

// Stored in the Veyon configuration (section "LabAudio"), so it can be exported and imported to all
// computers with the Veyon Configurator. Teacher computer: port, quality, recordings folder (the
// quality is sent to the students with every mode). Student computers: call tray icon.
#define FOREACH_LAB_AUDIO_CONFIG_PROPERTY(OP) \
	OP( LabAudioConfiguration, m_configuration, int, hubPort, setHubPort, "HubPort", "LabAudio", 11500, Configuration::Property::Flag::Standard ) \
	OP( LabAudioConfiguration, m_configuration, int, audioQuality, setAudioQuality, "AudioQuality", "LabAudio", 1, Configuration::Property::Flag::Standard ) \
	OP( LabAudioConfiguration, m_configuration, QString, recordingsFolder, setRecordingsFolder, "RecordingsFolder", "LabAudio", QString(), Configuration::Property::Flag::Standard ) \
	OP( LabAudioConfiguration, m_configuration, bool, callTrayEnabled, setCallTrayEnabled, "CallTrayEnabled", "LabAudio", true, Configuration::Property::Flag::Standard )

DECLARE_CONFIG_PROXY(LabAudioConfiguration, FOREACH_LAB_AUDIO_CONFIG_PROPERTY)

namespace LabAudioQuality
{

// Index of the "Audio quality" choice -> Opus bit rate (bit/s).
inline int bitRate( int quality )
{
	switch( quality )
	{
	case 0: return 24000;	// speech, low bandwidth
	case 2: return 48000;	// high
	case 3: return 64000;	// music
	default: return 32000;	// standard
	}
}

}
