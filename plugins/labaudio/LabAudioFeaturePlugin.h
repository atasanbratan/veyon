/*
 * LabAudioFeaturePlugin.h - live classroom audio (broadcast, talk, listen)
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

#include <QPointer>
#include <QTimer>

#include "ComputerControlInterface.h"
#include "CryptoCore.h"
#include "Feature.h"
#include "FeatureProviderInterface.h"
#include "LabAudioGroupWork.h"

class LabAudioClient;
class LabAudioHub;

class LabAudioFeaturePlugin : public QObject, FeatureProviderInterface, PluginInterface
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "io.veyon.Veyon.Plugins.LabAudio")
	Q_INTERFACES(PluginInterface FeatureProviderInterface)
public:
	// Arguments travel keyed by their numeric value: only append, never reorder,
	// so teacher and student computers with different plugin builds stay compatible.
	enum class Argument {
		Token,
		HubHost,
		HubPort,
		Roles,
		HubAddresses,
		GroupTokens,	// QVariantMap: hex token -> group id
		FocusGroup,
		TeacherTalks,
		GroupMembers,
		StudentName,
		TeacherMuted
	};
	Q_ENUM(Argument)

	explicit LabAudioFeaturePlugin( QObject* parent = nullptr );
	~LabAudioFeaturePlugin() override;

	Plugin::Uid uid() const override
	{
		return Plugin::Uid{ QStringLiteral("294158c2-5f67-4696-9c36-259292bd0f2d") };
	}

	QVersionNumber version() const override
	{
		return QVersionNumber( 0, 2 );
	}

	QString name() const override
	{
		return QStringLiteral("LabAudio");
	}

	QString description() const override
	{
		return tr( "Live classroom audio: speak to students, talk with or listen to a student, pair and group work" );
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

	Feature::Uid metaFeature( Feature::Uid featureUid ) const override;

	bool controlFeature( Feature::Uid featureUid, Operation operation, const QVariantMap& arguments,
						 const ComputerControlInterfaceList& computerControlInterfaces ) override;

	bool startFeature( VeyonMasterInterface& master, const Feature& feature,
					   const ComputerControlInterfaceList& computerControlInterfaces ) override;

	bool stopFeature( VeyonMasterInterface& master, const Feature& feature,
					  const ComputerControlInterfaceList& computerControlInterfaces ) override;

	bool handleFeatureMessage( VeyonServerInterface& server,
							   const MessageContext& messageContext,
							   const FeatureMessage& message ) override;

	bool handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message ) override;

	bool isFeatureActive( VeyonServerInterface& server, Feature::Uid featureUid ) const override;

	// Default TCP port of the teacher's audio hub (plus the Veyon session id on multi-session hosts).
	static constexpr int DefaultHubPort = 11500;
	static constexpr int HubKeepAliveIntervalMs = 5000;

private:
	enum class FeatureCommand {
		StartHub,
		StopHub,
		StartClient,
		StopClient
	};

	struct Group
	{
		QByteArray token;
		ComputerControlInterfaceList members;
		QStringList names;
	};

	bool startMode( VeyonMasterInterface& master, const Feature& feature,
					const ComputerControlInterfaceList& computerControlInterfaces );
	bool startGroupWork( VeyonMasterInterface& master, const ComputerControlInterfaceList& computerControlInterfaces );
	void assignGroups( const QList<QList<int>>& groups );
	void startGroupClients();
	void regroup();
	void setGroupFocus( int groupId, bool teacherTalks );
	void endGroupWork();
	void startClient( const ComputerControlInterface::Pointer& student, const QByteArray& token, int roles,
					  const QStringList& groupMembers = {} );
	void showTeacherPanel( VeyonMasterInterface& master, const Feature& feature );
	void setTeacherMuted( bool muted );
	void sendHubCommand();
	int hubPort() const;
	static QStringList localAddresses();
	static QString studentName( const ComputerControlInterface::Pointer& computerControlInterface );
	// Name plus computer, e.g. "Anna Petrova (PC-07)", for the teacher's list of calls.
	static QString studentLabel( const ComputerControlInterface::Pointer& computerControlInterface );

	const Feature m_labAudioFeature;
	const Feature m_broadcastFeature;
	const Feature m_talkFeature;
	const Feature m_listenFeature;
	const Feature m_groupWorkFeature;
	const Feature m_hubFeature;
	const Feature m_clientFeature;
	const FeatureList m_features;

	QByteArray m_token;
	QTimer m_hubKeepAliveTimer;
	ComputerControlInterfaceList m_hubInterfaces;
	int m_activeModes{0};

	// group work (Veyon Master)
	ComputerControlInterfaceList m_groupStudents;
	int m_groupSize{2};
	bool m_randomGroups{true};
	QList<Group> m_groups;
	int m_focusGroup{-1};
	bool m_teacherTalks{false};
	QPointer<LabAudioGroupPanel> m_groupPanel;
	QPointer<LabAudioTeacherPanel> m_teacherPanel;
	bool m_teacherMuted{false};

	LabAudioHub* m_hub{nullptr};
	LabAudioClient* m_client{nullptr};
};
