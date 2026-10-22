/*
 * LabAudioFeaturePlugin.cpp - live classroom audio (broadcast, talk, listen)
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

#include <QCoreApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QSettings>
#include <QTcpSocket>
#include <QTimeZone>
#include <QUrl>

#include "ComputerControlInterface.h"
#include "FeatureWorkerManager.h"
#include "LabAudioClient.h"
#include "LabAudioConfigurationPage.h"
#include "LabAudioFeaturePlugin.h"
#include "LabAudioHub.h"
#include "LabAudioProtocol.h"
#include "LabAudioRecorder.h"
#include "Logger.h"
#include "PlatformPluginInterface.h"
#include "PlatformSessionFunctions.h"
#include "TranslationLoader.h"
#include "VeyonMasterInterface.h"
#include "VeyonServerInterface.h"
#include "VeyonWorkerInterface.h"

using namespace LabAudioProtocol;



LabAudioFeaturePlugin::LabAudioFeaturePlugin( QObject* parent ) :
	QObject( parent ),
	// labaudio_<language>.qm (Russian, Turkmen) in Veyon's translations folder; language as configured in Veyon,
	// otherwise the Windows language.
	m_translationsLoaded( TranslationLoader::load( QStringLiteral( "labaudio" ) ) ),
	m_labAudioFeature( QStringLiteral( "LabAudio" ),
					   Feature::Flag::Mode | Feature::Flag::AllComponents,
					   Feature::Uid( "910da020-7bf3-4144-a476-7e18a2e0794c" ),
					   Feature::Uid(),
					   tr( "Lab audio" ), tr( "Stop lab audio" ),
					   tr( "Speak to students through their headsets, talk with one student, "
						   "or listen to a student's microphone." ),
					   QStringLiteral(":/labaudio/labaudio.png") ),
	m_broadcastFeature( QStringLiteral( "LabAudioBroadcast" ),
						Feature::Flag::Mode | Feature::Flag::AllComponents,
						Feature::Uid( "d6b01bc6-764e-4b3c-b705-6fd6b2f04ee2" ),
						m_labAudioFeature.uid(),
						tr( "Speak to students" ), tr( "Stop speaking" ),
						tr( "Your microphone is heard in the headsets of all selected students." ),
						QStringLiteral(":/labaudio/broadcast.png") ),
	m_talkFeature( QStringLiteral( "LabAudioTalk" ),
				   Feature::Flag::Mode | Feature::Flag::AllComponents,
				   Feature::Uid( "9056b1e7-e429-49bf-a20d-0184e6f18bd6" ),
				   m_labAudioFeature.uid(),
				   tr( "Talk with student" ), tr( "End conversation" ),
				   tr( "Two-way conversation with the selected student." ),
				   QStringLiteral(":/labaudio/talk.png") ),
	m_listenFeature( QStringLiteral( "LabAudioListen" ),
					 Feature::Flag::Mode | Feature::Flag::AllComponents,
					 Feature::Uid( "e5c9b998-40a9-4941-8497-7bab3258fedb" ),
					 m_labAudioFeature.uid(),
					 tr( "Listen to student" ), tr( "Stop listening" ),
					 tr( "Hear the selected student's microphone. The student sees a notice while you listen." ),
					 QStringLiteral(":/labaudio/listen.png") ),
	m_groupWorkFeature( QStringLiteral( "LabAudioGroupWork" ),
						Feature::Flag::Mode | Feature::Flag::AllComponents,
						Feature::Uid( "c3a7e2b4-5d61-4f0a-9b8e-2f4d6a1c7e93" ),
						m_labAudioFeature.uid(),
						tr( "Group work" ), tr( "End group work" ),
						tr( "Students talk with each other in pairs or small groups. You can listen to "
							"or join any group." ),
						QStringLiteral(":/labaudio/group.png") ),
	m_hubFeature( QStringLiteral( "LabAudioHub" ),
				  Feature::Flag::Session | Feature::Flag::Service | Feature::Flag::Worker,
				  Feature::Uid( "79305caa-c661-4126-9466-b5636aa3daa9" ),
				  m_labAudioFeature.uid(),
				  {}, {}, {} ),
	m_clientFeature( QStringLiteral( "LabAudioClient" ),
					 Feature::Flag::Meta | Feature::Flag::AllComponents,
					 Feature::Uid( "65215fe4-021f-4cfe-8e26-25be9599401b" ),
					 m_labAudioFeature.uid(),
					 tr( "Lab audio client" ), {}, {} ),
	m_features( { m_labAudioFeature, m_broadcastFeature, m_talkFeature, m_listenFeature, m_groupWorkFeature,
				  m_hubFeature, m_clientFeature } ),
	m_token( CryptoCore::generateChallenge() ),
	m_configuration( &VeyonCore::config() )
{
	Q_ASSERT( m_clientFeature.uid() == LabAudioCallProtocol::LabAudioClientFeatureUid );
	m_hubKeepAliveTimer.setInterval( HubKeepAliveIntervalMs );
	m_hubStatusTimer.setSingleShot( true );
	m_hubStatusTimer.setInterval( 150 );
	connect( &m_hubStatusTimer, &QTimer::timeout, this, &LabAudioFeaturePlugin::reportHubStatus );
	connect( &m_hubKeepAliveTimer, &QTimer::timeout, this, &LabAudioFeaturePlugin::sendHubCommand );
}



LabAudioFeaturePlugin::~LabAudioFeaturePlugin()
{
	delete m_groupPanel;
	delete m_teacherPanel;
	delete m_client;
	delete m_hub;
}



Feature::Uid LabAudioFeaturePlugin::metaFeature( Feature::Uid featureUid ) const
{
	if( featureUid == m_broadcastFeature.uid() || featureUid == m_talkFeature.uid() ||
		featureUid == m_listenFeature.uid() || featureUid == m_groupWorkFeature.uid() )
	{
		return m_clientFeature.uid();
	}
	return FeatureProviderInterface::metaFeature( featureUid );
}



QString LabAudioFeaturePlugin::studentName( const ComputerControlInterface::Pointer& computerControlInterface )
{
	if( computerControlInterface.isNull() )
	{
		return {};
	}
	const auto& computer = computerControlInterface->computer();
	const auto computerName = computer.displayName().isEmpty() ? computerControlInterface->computerName()
															   : computer.displayName();
	auto user = computerControlInterface->userFullName();
	if( user.isEmpty() )
	{
		user = computerControlInterface->userLoginName().section( QLatin1Char( '\\' ), -1 );
	}
	return user.isEmpty() ? computerName : user;
}



QString LabAudioFeaturePlugin::studentLabel( const ComputerControlInterface::Pointer& computerControlInterface )
{
	if( computerControlInterface.isNull() )
	{
		return {};
	}
	const auto name = studentName( computerControlInterface );
	const auto& computer = computerControlInterface->computer();
	const auto computerName = computer.displayName().isEmpty() ? computerControlInterface->computerName()
															   : computer.displayName();
	return ( computerName.isEmpty() || computerName == name ) ? name : QStringLiteral( "%1 (%2)" ).arg( name, computerName );
}



void LabAudioFeaturePlugin::startClient( const ComputerControlInterface::Pointer& student, const QByteArray& token,
										 int roles, const QStringList& groupMembers )
{
	controlFeature( m_clientFeature.uid(), Operation::Start,
					{ { argToString(Argument::Token), token },
					  { argToString(Argument::Roles), roles },
					  { argToString(Argument::GroupMembers), groupMembers },
					  { argToString(Argument::StudentName), studentLabel( student ) },
					  { argToString(Argument::ClientTag), student->computer().networkObjectUid().toString() } },
					{ student } );
}



void LabAudioFeaturePlugin::setTeacherMuted( bool muted )
{
	if( muted != m_teacherMuted )
	{
		m_teacherMuted = muted;
		// Sent at once (not only with the keep-alive) so push-to-talk reacts immediately.
		sendHubCommand();
	}
}



void LabAudioFeaturePlugin::showTeacherPanel( VeyonMasterInterface& master, const Feature& feature )
{
	if( m_teacherPanel.isNull() )
	{
		m_teacherPanel = new LabAudioTeacherPanel( master.mainWindow() );
		connect( m_teacherPanel->micControls(), &LabAudioMicControls::mutedChanged,
				 this, &LabAudioFeaturePlugin::setTeacherMuted );
		connect( m_teacherPanel, &LabAudioTeacherPanel::recordToggled, this, &LabAudioFeaturePlugin::setRecordConversation );
		connect( m_teacherPanel, &LabAudioTeacherPanel::openRecordingsRequested, this, &LabAudioFeaturePlugin::openRecordings );
	}
	const auto student = studentName( master.selectedComputerControlInterfaces().value( 0 ) );
	QString description;
	if( feature == m_talkFeature )
	{
		description = tr( "You are talking with %1." ).arg( student );
		m_conversationLabel = tr( "Talk with %1" ).arg( student );
	}
	else if( feature == m_listenFeature )
	{
		description = tr( "You are listening to %1." ).arg( student );
		m_conversationLabel = tr( "Listening to %1" ).arg( student );
	}
	else
	{
		description = tr( "You are speaking to the students." );
		m_conversationLabel = tr( "Lesson" );
	}
	m_teacherPanel->setMode( description, tr( "To stop, click \"%1\" in the toolbar again." ).arg( feature.displayName() ) );
	m_teacherPanel->setMicAvailable( feature != m_listenFeature );
	m_teacherPanel->setRecording( m_recordConversation );
	m_teacherPanel->show();
	m_teacherPanel->raise();
}



void LabAudioFeaturePlugin::setRecordConversation( bool record )
{
	m_recordConversation = record;
	sendHubCommand();
	if( m_teacherPanel )
	{
		m_teacherPanel->setRecording( record );
	}
}



void LabAudioFeaturePlugin::setRecordGroups( bool record )
{
	m_recordGroups = record;
	sendHubCommand();
	if( m_groupPanel )
	{
		m_groupPanel->setRecording( record );
	}
}



void LabAudioFeaturePlugin::openRecordings()
{
	LabAudioRecorder::setConfiguredFolder( m_configuration.recordingsFolder() );
	// Veyon Master runs as the same user as the teacher's hub, so this is the folder it records to.
	QDesktopServices::openUrl( QUrl::fromLocalFile( LabAudioRecorder::recordingsFolder() ) );
}



QStringList LabAudioFeaturePlugin::localAddresses()
{
	// All usable addresses of the teacher's computer, so students can fall back when the route
	// Veyon Master used is not reachable for the audio port (multi-homed teacher PCs).
	QStringList addresses;
	const auto interfaces = QNetworkInterface::allInterfaces();
	for( const auto& iface : interfaces )
	{
		const auto flags = iface.flags();
		if( ( flags & QNetworkInterface::IsUp ) == 0 || ( flags & QNetworkInterface::IsRunning ) == 0 ||
			( flags & QNetworkInterface::IsLoopBack ) )
		{
			continue;
		}
		for( const auto& entry : iface.addressEntries() )
		{
			const auto ip = entry.ip();
			if( ip.protocol() == QAbstractSocket::IPv4Protocol && ip.isLinkLocal() == false )
			{
				addresses.append( ip.toString() );
			}
		}
	}
	return addresses;
}



int LabAudioFeaturePlugin::hubPort() const
{
	return qBound( 1024, m_configuration.hubPort(), 65000 ) + VeyonCore::sessionId();
}



bool LabAudioFeaturePlugin::controlFeature( Feature::Uid featureUid, Operation operation, const QVariantMap& arguments,
											const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( featureUid == m_hubFeature.uid() )
	{
		if( operation == Operation::Start )
		{
			m_hubInterfaces = computerControlInterfaces;
			m_hubKeepAliveTimer.start();
		}
		else if( operation == Operation::Stop )
		{
			m_hubKeepAliveTimer.stop();
		}
		else
		{
			return false;
		}
		sendHubCommand();
		if( operation == Operation::Stop )
		{
			m_hubInterfaces.clear();
			// A new token for the next activation: old clients cannot reconnect.
			m_token = CryptoCore::generateChallenge();
		}
		return true;
	}

	if( featureUid == m_clientFeature.uid() )
	{
		if( operation == Operation::Start )
		{
			vWarning() << "LabAudio: starting clients on" << computerControlInterfaces.size() << "computers, hub port"
					   << hubPort() << "addresses" << localAddresses() << "token" << tokenFingerprint( m_token );
			sendFeatureMessage( FeatureMessage{ m_clientFeature.uid(), FeatureCommand::StartClient }
									// Every value needs a concrete type: the receiving Veyon Server
									// drops all arguments of a message containing an invalid QVariant.
									.addArgument( Argument::Token, arguments.value( argToString(Argument::Token), m_token ).toByteArray() )
									.addArgument( Argument::HubHost, arguments.value( argToString(Argument::HubHost), QString() ).toString() )
									.addArgument( Argument::HubAddresses, localAddresses() )
									.addArgument( Argument::HubPort, arguments.value( argToString(Argument::HubPort), hubPort() ).toInt() )
									.addArgument( Argument::Roles, arguments.value( argToString(Argument::Roles),
																					int( ReceiveTeacher ) ).toInt() )
									.addArgument( Argument::GroupMembers,
												  arguments.value( argToString(Argument::GroupMembers), QStringList() )
													  .toStringList() )
									.addArgument( Argument::StudentName,
												  arguments.value( argToString(Argument::StudentName), QString() ).toString() )
									.addArgument( Argument::BitRate, LabAudioQuality::bitRate( m_configuration.audioQuality() ) )
									.addArgument( Argument::ClientTag,
												  arguments.value( argToString(Argument::ClientTag), QString() ).toString() ),
								computerControlInterfaces );
			return true;
		}
		if( operation == Operation::Stop )
		{
			sendFeatureMessage( FeatureMessage{ m_clientFeature.uid(), FeatureCommand::StopClient },
								computerControlInterfaces );
			return true;
		}
	}

	return false;
}



void LabAudioFeaturePlugin::sendHubCommand()
{
	if( m_hubKeepAliveTimer.isActive() )
	{
		// Repeated periodically so a crashed hub worker is restarted with the same token.
		QVariantMap groupTokens;
		// File labels; a group's label contains its members, so new groups start new files.
		QVariantMap recordingLabels{ { QStringLiteral( "0" ), m_conversationLabel } };
		for( int g = 0; g < m_groups.size(); ++g )
		{
			groupTokens[QString::fromLatin1( m_groups[g].token.toHex() )] = g + 1;
			recordingLabels[QString::number( g + 1 )] =
				tr( "Group %1 - %2" ).arg( g + 1 ).arg( m_groups[g].names.join( QStringLiteral( ", " ) ) );
		}
		sendFeatureMessage( FeatureMessage{ m_hubFeature.uid(), FeatureCommand::StartHub }
								.addArgument( Argument::Token, m_token )
								.addArgument( Argument::HubPort, hubPort() )
								.addArgument( Argument::GroupTokens, groupTokens )
								.addArgument( Argument::FocusGroup, m_focusGroup )
								.addArgument( Argument::TeacherTalks, m_teacherTalks )
								.addArgument( Argument::TeacherMuted, m_teacherMuted )
								.addArgument( Argument::RecordConversation, m_recordConversation )
								.addArgument( Argument::RecordGroups, m_recordGroups )
								.addArgument( Argument::RecordingLabels, recordingLabels )
								.addArgument( Argument::BitRate, LabAudioQuality::bitRate( m_configuration.audioQuality() ) )
								.addArgument( Argument::RecordingsFolder, m_configuration.recordingsFolder() ),
							m_hubInterfaces );
	}
	else
	{
		sendFeatureMessage( FeatureMessage{ m_hubFeature.uid(), FeatureCommand::StopHub }, m_hubInterfaces );
	}
}



bool LabAudioFeaturePlugin::startFeature( VeyonMasterInterface& master, const Feature& feature,
										  const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( feature == m_broadcastFeature || feature == m_talkFeature || feature == m_listenFeature )
	{
		return startMode( master, feature, computerControlInterfaces );
	}
	if( feature == m_groupWorkFeature )
	{
		return startGroupWork( master, computerControlInterfaces );
	}
	return false;
}



void LabAudioFeaturePlugin::sendCalling( bool calling )
{
	if( m_worker )
	{
		// Handled by the LabAudioCalls plugin in the Veyon Server; a worker may only use its own feature uid.
		m_worker->sendFeatureMessageReply( FeatureMessage{ m_clientFeature.uid(), FeatureCommand::SetCalling }
											   .addArgument( Argument::Calling, calling ) );
	}
}



bool LabAudioFeaturePlugin::startGroupWork( VeyonMasterInterface& master,
											const ComputerControlInterfaceList& computerControlInterfaces )
{
	auto students = computerControlInterfaces;
	students.removeLocalHostInterfaces();
	if( students.size() < 2 )
	{
		QMessageBox::information( master.mainWindow(), m_groupWorkFeature.displayName(),
								  tr( "Group work needs at least two student computers." ) );
		return true;
	}

	QStringList names;
	for( const auto& student : std::as_const( students ) )
	{
		names.append( studentName( student ) );
	}
	LabAudioGroupDialog dialog( names, master.mainWindow() );
	if( dialog.exec() != QDialog::Accepted || dialog.groups().isEmpty() )
	{
		return true;
	}

	m_groupStudents = students;
	m_groupSize = dialog.groupSize();
	m_randomGroups = dialog.isRandom();
	m_focusGroup = -1;
	m_teacherTalks = false;
	assignGroups( dialog.groups() );

	// The hub learns the group tokens before the students connect with them.
	controlFeature( m_hubFeature.uid(), Operation::Start, {}, { master.localSessionControlInterface().weakPointer() } );
	startGroupClients();
	++m_activeModes;

	if( m_groupPanel.isNull() )
	{
		m_groupPanel = new LabAudioGroupPanel( master.mainWindow() );
		connect( m_groupPanel, &LabAudioGroupPanel::focusRequested, this, &LabAudioFeaturePlugin::setGroupFocus );
		connect( m_groupPanel, &LabAudioGroupPanel::regroupRequested, this, &LabAudioFeaturePlugin::regroup );
		connect( m_groupPanel->micControls(), &LabAudioMicControls::mutedChanged,
				 this, &LabAudioFeaturePlugin::setTeacherMuted );
		connect( m_groupPanel, &LabAudioGroupPanel::recordToggled, this, &LabAudioFeaturePlugin::setRecordGroups );
		connect( m_groupPanel, &LabAudioGroupPanel::openRecordingsRequested, this, &LabAudioFeaturePlugin::openRecordings );
	}
	m_groupPanel->setRecording( m_recordGroups );
	QList<QStringList> groupNames;
	for( const auto& group : std::as_const( m_groups ) )
	{
		groupNames.append( group.names );
	}
	m_groupPanel->setGroups( groupNames );
	m_groupPanel->setFocus( m_focusGroup, m_teacherTalks );
	m_modeStudents = m_groupStudents;
	m_connectedTags.clear();
	updateConnectionStatus();
	m_groupPanel->show();
	m_groupPanel->raise();
	return true;
}



void LabAudioFeaturePlugin::assignGroups( const QList<QList<int>>& groups )
{
	m_groups.clear();
	for( const auto& indices : groups )
	{
		Group group;
		group.token = CryptoCore::generateChallenge();
		for( const auto index : indices )
		{
			const auto student = m_groupStudents.value( index );
			if( student )
			{
				group.members.append( student );
				group.names.append( studentName( student ) );
			}
		}
		m_groups.append( group );
	}
	vWarning() << "LabAudio: group work with" << m_groups.size() << "groups of" << m_groupSize;
	publishGroups();
}



void LabAudioFeaturePlugin::publishGroups()
{
	// For the "Students calling" window of the LabAudioCalls plugin (same Veyon Master process).
	QVariantMap groupOfComputer;
	for( int g = 0; g < m_groups.size(); ++g )
	{
		for( const auto& member : std::as_const( m_groups[g].members ) )
		{
			groupOfComputer[member->computer().networkObjectUid().toString()] = g + 1;
		}
	}
	qApp->setProperty( LabAudioCallProtocol::GroupOfComputerProperty, groupOfComputer );
}



void LabAudioFeaturePlugin::startGroupClients()
{
	const int roles = SendMicrophone | ReceivePeers | ReceiveTeacher;
	for( const auto& group : std::as_const( m_groups ) )
	{
		for( int i = 0; i < group.members.size(); ++i )
		{
			auto others = group.names;
			others.removeAt( i );
			startClient( group.members[i], group.token, roles, others );
		}
	}
}



void LabAudioFeaturePlugin::regroup()
{
	if( m_groups.isEmpty() )
	{
		return;
	}
	m_focusGroup = -1;
	m_teacherTalks = false;
	assignGroups( LabAudioGroupDialog::makeGroups( int( m_groupStudents.size() ), m_groupSize, m_randomGroups ) );
	// New tokens first: the hub disconnects the old groups, then the students reconnect.
	sendHubCommand();
	startGroupClients();

	if( m_groupPanel )
	{
		QList<QStringList> groupNames;
		for( const auto& group : std::as_const( m_groups ) )
		{
			groupNames.append( group.names );
		}
		m_groupPanel->setGroups( groupNames );
		m_groupPanel->setFocus( m_focusGroup, m_teacherTalks );
	}
}



void LabAudioFeaturePlugin::setGroupFocus( int groupId, bool teacherTalks )
{
	m_focusGroup = ( groupId >= 1 && groupId <= m_groups.size() ) ? groupId : -1;
	m_teacherTalks = m_focusGroup > 0 && teacherTalks;
	sendHubCommand();
	if( m_groupPanel )
	{
		m_groupPanel->setFocus( m_focusGroup, m_teacherTalks );
	}
}



void LabAudioFeaturePlugin::endGroupWork()
{
	m_groups.clear();
	publishGroups();
	m_groupStudents.clear();
	m_focusGroup = -1;
	m_teacherTalks = false;
	if( m_groupPanel )
	{
		m_groupPanel->close();
		m_groupPanel->deleteLater();
	}
}



bool LabAudioFeaturePlugin::startMode( VeyonMasterInterface& master, const Feature& feature,
									   const ComputerControlInterfaceList& computerControlInterfaces )
{
	auto targets = computerControlInterfaces;
	// Never connect the teacher's own computer as a student.
	targets.removeLocalHostInterfaces();

	quint8 roles = ReceiveTeacher;
	if( feature != m_broadcastFeature )
	{
		const auto selected = master.selectedComputerControlInterfaces();
		if( selected.size() != 1 )
		{
			QMessageBox::information( master.mainWindow(), feature.displayName(),
									  tr( "Please select exactly one student computer." ) );
			return true;
		}
		targets = selected;
		roles = ( feature == m_talkFeature ) ? quint8( ReceiveTeacher | SendMicrophone ) : quint8( SendMicrophone );
	}
	if( targets.isEmpty() )
	{
		return true;
	}

	controlFeature( m_hubFeature.uid(), Operation::Start, {}, { master.localSessionControlInterface().weakPointer() } );
	for( const auto& target : std::as_const( targets ) )
	{
		// One message per student so each one carries the student's name (shown when calling).
		startClient( target, m_token, roles );
	}
	++m_activeModes;
	showTeacherPanel( master, feature );
	m_modeStudents = targets;
	m_connectedTags.clear();
	updateConnectionStatus();
	return true;
}



bool LabAudioFeaturePlugin::stopFeature( VeyonMasterInterface& master, const Feature& feature,
										 const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( feature == m_broadcastFeature || feature == m_talkFeature || feature == m_listenFeature ||
		feature == m_groupWorkFeature || feature == m_labAudioFeature )
	{
		if( feature == m_groupWorkFeature || feature == m_labAudioFeature )
		{
			endGroupWork();
		}
		if( m_teacherPanel && feature != m_groupWorkFeature )
		{
			m_teacherPanel->close();
			m_teacherPanel->deleteLater();
		}
		controlFeature( m_clientFeature.uid(), Operation::Stop, {}, computerControlInterfaces );
		m_activeModes = qMax( 0, m_activeModes - 1 );
		if( m_activeModes == 0 || feature == m_labAudioFeature )
		{
			m_activeModes = 0;
			m_teacherMuted = false;
			// Recording never carries over into the next mode without the teacher asking.
			m_recordConversation = false;
			m_recordGroups = false;
			m_modeStudents.clear();
			m_connectedTags.clear();
			controlFeature( m_hubFeature.uid(), Operation::Stop, {},
							{ master.localSessionControlInterface().weakPointer() } );
		}
		return true;
	}
	return false;
}



bool LabAudioFeaturePlugin::handleFeatureMessage( VeyonServerInterface& server, const MessageContext& messageContext,
												  const FeatureMessage& message )
{
	auto& workers = server.featureWorkerManager();

	if( message.featureUid() == m_hubFeature.uid() )
	{
		if( message.command<FeatureCommand>() == FeatureCommand::StopHub &&
			workers.isWorkerRunning( m_hubFeature.uid() ) == false )
		{
			return true;
		}
		if( message.command<FeatureCommand>() == FeatureCommand::StartHub )
		{
			// The hub's status reports go back to the Veyon Master that keeps it running.
			m_hubMasterContext = messageContext;
		}
		// Runs as the logged-in user so the teacher's own headset is used.
		workers.sendMessageToUnmanagedSessionWorker( message );
		return true;
	}

	if( message.featureUid() == m_clientFeature.uid() )
	{
		if( message.command<FeatureCommand>() == FeatureCommand::StopClient )
		{
			if( workers.isWorkerRunning( m_clientFeature.uid() ) )
			{
				workers.sendMessageToUnmanagedSessionWorker( message );
			}
			return true;
		}

		if( VeyonCore::platform().sessionFunctions().currentSessionHasUser() == false )
		{
			vDebug() << "not starting lab audio client: no user session";
			return true;
		}

		vWarning() << "LabAudio StartClient from master, argument keys" << message.arguments().keys()
				   << "token" << tokenFingerprint( message.argument( Argument::Token ).toByteArray() );

		if( message.argument( Argument::HubHost ).toString().isEmpty() )
		{
			// The hub runs on the computer of the Veyon Master that sent this command.
			auto socket = qobject_cast<QTcpSocket*>( messageContext.ioDevice() );
			if( socket == nullptr )
			{
				vCritical() << "invalid socket";
				return false;
			}
			// Use plain IPv4 for IPv4-mapped peers ("::ffff:10.0.0.1").
			auto peer = socket->peerAddress();
			bool isIPv4 = false;
			const auto ipv4 = peer.toIPv4Address( &isIPv4 );
			if( isIPv4 )
			{
				peer = QHostAddress( ipv4 );
			}
			workers.sendMessageToUnmanagedSessionWorker(
				FeatureMessage{ message }.addArgument( Argument::HubHost, peer.toString() ) );
		}
		else
		{
			workers.sendMessageToUnmanagedSessionWorker( message );
		}
		return true;
	}

	return false;
}



void LabAudioFeaturePlugin::reportHubStatus()
{
	if( m_worker && m_hub )
	{
		m_worker->sendFeatureMessageReply( FeatureMessage{ m_hubFeature.uid(), FeatureCommand::HubStatus }
											   .addArgument( Argument::ConnectedClients, m_hub->connectedTags() ) );
	}
}



bool LabAudioFeaturePlugin::handleFeatureMessageFromWorker( VeyonServerInterface& server, const FeatureMessage& message )
{
	if( message.featureUid() == m_hubFeature.uid() && message.command<FeatureCommand>() == FeatureCommand::HubStatus )
	{
		// Without a Master context (e.g. the server restarted) the reply is dropped; the next keep-alive sets it.
		server.sendFeatureMessageReply( m_hubMasterContext, message );
		return true;
	}
	return false;
}



bool LabAudioFeaturePlugin::handleFeatureMessage( ComputerControlInterface::Pointer computerControlInterface,
												  const FeatureMessage& message )
{
	Q_UNUSED(computerControlInterface)
	if( message.featureUid() != m_hubFeature.uid() || message.command<FeatureCommand>() != FeatureCommand::HubStatus )
	{
		return false;
	}
	const auto tags = message.argument( Argument::ConnectedClients ).toStringList();
	m_connectedTags = QSet<QString>( tags.begin(), tags.end() );
	updateConnectionStatus();
	return true;
}



void LabAudioFeaturePlugin::updateConnectionStatus()
{
	int connected = 0;
	QStringList waiting;
	for( const auto& student : std::as_const( m_modeStudents ) )
	{
		if( m_connectedTags.contains( student->computer().networkObjectUid().toString() ) )
		{
			++connected;
		}
		else
		{
			waiting.append( studentName( student ) );
		}
	}
	const auto total = int( m_modeStudents.size() );
	if( m_teacherPanel )
	{
		m_teacherPanel->setConnectionStatus( connected, total, waiting );
	}
	if( m_groupPanel )
	{
		m_groupPanel->setConnectionStatus( connected, total, waiting );
	}
}



bool LabAudioFeaturePlugin::handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message )
{
	m_worker = &worker;

	if( message.featureUid() == m_hubFeature.uid() )
	{
		switch( message.command<FeatureCommand>() )
		{
		case FeatureCommand::StartHub:
		{
			if( m_hub == nullptr )
			{
				vWarning() << "LabAudio StartHub: argument keys" << message.arguments().keys();
				m_hub = new LabAudioHub( message.argument( Argument::HubPort ).toInt(),
										 message.argument( Argument::Token ).toByteArray() );
				// Students connect within a short time: report the new state once they settle.
				connect( m_hub, &LabAudioHub::connectedClientsChanged, this, [this]() { m_hubStatusTimer.start(); } );
			}
			else
			{
				m_hub->setToken( message.argument( Argument::Token ).toByteArray() );
			}
			if( message.hasArgument( Argument::BitRate ) )
			{
				m_hub->setBitRate( message.argument( Argument::BitRate ).toInt() );
			}
			LabAudioRecorder::setConfiguredFolder( message.argument( Argument::RecordingsFolder ).toString() );
			QHash<QByteArray, int> groupTokens;
			const auto groups = message.argument( Argument::GroupTokens ).toMap();
			for( auto it = groups.cbegin(); it != groups.cend(); ++it )
			{
				groupTokens.insert( QByteArray::fromHex( it.key().toLatin1() ), it.value().toInt() );
			}
			m_hub->setGroups( groupTokens );
			m_hub->setFocus( message.hasArgument( Argument::FocusGroup ) ? message.argument( Argument::FocusGroup ).toInt()
																		 : LabAudioHub::NoGroup,
							 message.argument( Argument::TeacherTalks ).toBool() );
			m_hub->setTeacherMuted( message.argument( Argument::TeacherMuted ).toBool() );
			QHash<int, QString> labels;
			const auto labelMap = message.argument( Argument::RecordingLabels ).toMap();
			for( auto it = labelMap.cbegin(); it != labelMap.cend(); ++it )
			{
				labels.insert( it.key().toInt(), it.value().toString() );
			}
			m_hub->setRecording( message.argument( Argument::RecordConversation ).toBool(),
								 message.argument( Argument::RecordGroups ).toBool(), labels );
			// Also with every keep-alive, so a restarted Veyon Master gets the current state.
			reportHubStatus();
			return true;
		}
		case FeatureCommand::StopHub:
			delete m_hub;
			m_hub = nullptr;
			QCoreApplication::quit();
			return true;
		default:
			break;
		}
	}
	else if( message.featureUid() == m_clientFeature.uid() )
	{
		switch( message.command<FeatureCommand>() )
		{
		case FeatureCommand::StartClient:
		{
			auto port = message.argument( Argument::HubPort ).toInt();
			if( port <= 0 || port > 65535 )
			{
				vWarning() << "invalid lab audio hub port" << message.argument( Argument::HubPort )
						   << "- teacher runs a different plugin version? Using" << DefaultHubPort;
				port = DefaultHubPort;
			}
			auto roles = quint8( message.argument( Argument::Roles ).toInt() );
			if( ( roles & ( ReceiveTeacher | SendMicrophone ) ) == 0 )
			{
				roles = ReceiveTeacher;
			}
			vWarning() << "LabAudio StartClient: argument keys" << message.arguments().keys()
					   << "host" << message.argument( Argument::HubHost ).toString()
					   << "addresses" << message.argument( Argument::HubAddresses ).toStringList()
					   << "port" << port << "roles" << roles
					   << "token" << tokenFingerprint( message.argument( Argument::Token ).toByteArray() );
			// A new mode replaces the previous connection (e.g. broadcast -> talk).
			delete m_client;
			m_client = new LabAudioClient( QStringList{ message.argument( Argument::HubHost ).toString() } +
											   message.argument( Argument::HubAddresses ).toStringList(),
										   port,
										   message.argument( Argument::Token ).toByteArray(),
										   roles,
										   message.argument( Argument::GroupMembers ).toStringList() );
			if( message.argument( Argument::BitRate ).toInt() > 0 )
			{
				m_client->setBitRate( message.argument( Argument::BitRate ).toInt() );
			}
			m_client->setTag( message.argument( Argument::ClientTag ).toString() );
			connect( m_client->indicator(), &LabAudioIndicator::callToggled, this, [this]( bool calling ) {
				sendCalling( calling );
			} );
			return true;
		}
		case FeatureCommand::CallState:
			if( m_client )
			{
				m_client->indicator()->setCallState( message.argument( Argument::Calling ).toBool(),
													 message.argument( Argument::CallsAllowed ).toBool() );
			}
			return true;
		case FeatureCommand::StopClient:
			delete m_client;
			m_client = nullptr;
			QCoreApplication::quit();
			return true;
		default:
			break;
		}
	}

	return false;
}



bool LabAudioFeaturePlugin::isFeatureActive( VeyonServerInterface& server, Feature::Uid featureUid ) const
{
	if( featureUid == m_labAudioFeature.uid() || featureUid == m_clientFeature.uid() )
	{
		return server.featureWorkerManager().isWorkerRunning( m_clientFeature.uid() );
	}
	return false;
}



ConfigurationPage* LabAudioFeaturePlugin::createConfigurationPage()
{
	return new LabAudioConfigurationPage( m_configuration );
}



IMPLEMENT_CONFIG_PROXY(LabAudioConfiguration)
