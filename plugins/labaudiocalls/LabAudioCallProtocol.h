/*
 * LabAudioCallProtocol.h - message numbers shared by the LabAudio and LabAudioCalls plugins
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

#include <QUuid>

// The ✋ button on the lab audio notice lives in the LabAudio client worker, the call state in this
// plugin: both plugins exchange messages of the LabAudio client feature, so these numbers must match
// LabAudioFeaturePlugin::FeatureCommand / Argument (checked there with static_assert).
namespace LabAudioCallProtocol
{

inline const QUuid LabAudioClientFeatureUid{ QStringLiteral( "65215fe4-021f-4cfe-8e26-25be9599401b" ) };

enum Command
{
	StartClient = 2,		// LabAudio: an audio notice is being shown
	SetCalling = 4,			// worker -> server
	CallState = 5,			// server -> worker, server -> Veyon Master
	AnswerCall = 6,			// Veyon Master -> server
	SetCallsAllowed = 7		// Veyon Master -> server
};

enum Argument
{
	Calling = 11,
	CallsAllowed = 12,
	CallTime = 13			// LongLong: milliseconds since epoch (UTC)
};

// Veyon Master: QVariantMap computer uid -> group number during group work, set by the LabAudio plugin
// on qApp so the calls window can show the group.
inline constexpr const char* GroupOfComputerProperty = "labAudioGroupOfComputer";

}
