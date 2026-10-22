/*
 * LabAudioCallsPlugin.h - students call the teacher at any time
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

#include <QAtomicInteger>
#include <QHash>
#include <QPointer>

#include "ComputerControlInterface.h"
#include "Feature.h"
#include "FeatureProviderInterface.h"
#include "LabAudioCallBoard.h"
#include "LabAudioCalls.h"
#include "../labaudio/LabAudioConfiguration.h"

class VeyonWorkerInterface;

// Student: tray icon worker (or the ✋ button on the lab audio notice) -> SetCalling -> Veyon Server
// (LabAudioCallBoard) -> CallState to every connected Veyon Master (sendAsyncFeatureMessages) and back
// to the workers. Veyon Master: "Done" -> AnswerCall; "Students may call" -> SetCallsAllowed.
class LabAudioCallsPlugin : public QObject, FeatureProviderInterface, PluginInterface
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "io.veyon.Veyon.Plugins.LabAudioCalls")
	Q_INTERFACES(PluginInterface FeatureProviderInterface)
public:
	explicit LabAudioCallsPlugin( QObject* parent = nullptr );
	~LabAudioCallsPlugin() override;

	Plugin::Uid uid() const override
	{
		return Plugin::Uid{ QStringLiteral("b7c4f1e9-6a2d-4e83-9c5f-1d8e7a3b2c60") };
	}

	QVersionNumber version() const override
	{
		return QVersionNumber( 0, 1 );
	}

	QString name() const override
	{
		return QStringLiteral("LabAudioCalls");
	}

	QString description() const override
	{
		return tr( "Students call the teacher at any time" );
	}

	QString vendor() const override
	{
		return QStringLiteral("TDMUGI");
	}

	QString copyright() const override
	{
		return QStringLiteral("TDMUGI");
	}

	const FeatureList& featureList() const override
	{
		return m_features;
	}

	bool controlFeature( Feature::Uid featureUid, Operation operation, const QVariantMap& arguments,
						 const ComputerControlInterfaceList& computerControlInterfaces ) override;

	bool startFeature( VeyonMasterInterface& master, const Feature& feature,
					   const ComputerControlInterfaceList& computerControlInterfaces ) override;

	bool handleFeatureMessage( ComputerControlInterface::Pointer computerControlInterface,
							   const FeatureMessage& message ) override;

	bool handleFeatureMessage( VeyonServerInterface& server, const MessageContext& messageContext,
							   const FeatureMessage& message ) override;

	bool handleFeatureMessageFromWorker( VeyonServerInterface& server, const FeatureMessage& message ) override;

	void sendAsyncFeatureMessages( VeyonServerInterface& server, const MessageContext& messageContext ) override;

	bool handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message ) override;

	static constexpr int CallTrayRetryMs = 15000;
	static constexpr int CallTrayCheckMs = 3000;

private:
	FeatureMessage callStateMessage( Feature::Uid featureUid ) const;
	void pushCallState( VeyonServerInterface& server );
	void ensureCallTray( VeyonServerInterface& server );
	LabAudioCallsWindow* callsWindow();
	void updateCallsWindow();
	void answerCall( int id );
	void setCallsAllowed( bool allowed );
	static QString studentLabel( const ComputerControlInterface::Pointer& computerControlInterface );

	const Feature m_callsFeature;
	const Feature m_callTrayFeature;
	const FeatureList m_features;
	LabAudioConfiguration m_configuration;

	// Veyon Server
	LabAudioCallBoard m_callBoard;
	QAtomicInteger<qint64> m_lastCallTrayStart{0};
	QAtomicInteger<qint64> m_lastCallTrayCheck{0};

	// Veyon Master
	struct MasterCall
	{
		ComputerControlInterface::Pointer computer;
		QDateTime since;
	};
	QHash<int, MasterCall> m_calls;
	QHash<QString, int> m_callIds;	// computer uid -> call id
	int m_nextCallId{1};
	bool m_callsAllowed{true};
	QPointer<LabAudioCallsWindow> m_callsWindow;
	VeyonMasterInterface* m_master{nullptr};

	// tray worker
	VeyonWorkerInterface* m_worker{nullptr};
	LabAudioCallTray* m_callTray{nullptr};
};
