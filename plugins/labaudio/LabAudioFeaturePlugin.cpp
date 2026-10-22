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
#include <QMessageBox>
#include <QNetworkInterface>
#include <QSettings>
#include <QTcpSocket>
#include <QTimeZone>

#include "ComputerControlInterface.h"
#include "FeatureWorkerManager.h"
#include "LabAudioClient.h"
#include "LabAudioFeaturePlugin.h"
#include "LabAudioHub.h"
#include "LabAudioProtocol.h"
#include "Logger.h"
#include "PlatformPluginInterface.h"
#include "PlatformSessionFunctions.h"
#include "VeyonMasterInterface.h"
#include "VeyonServerInterface.h"
#include "VeyonWorkerInterface.h"

using namespace LabAudioProtocol;



LabAudioFeaturePlugin::LabAudioFeaturePlugin( QObject* parent ) :
	QObject( parent ),
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
	m_callsFeature( QStringLiteral( "LabAudioStudentCalls" ),
					Feature::Flag::Action | Feature::Flag::Master,
					Feature::Uid( "5e0b1f7a-3c2d-4b8e-9f61-7a4d2c8e1b05" ),
					m_labAudioFeature.uid(),
					tr( "Student calls" ), {},
					tr( "Students who pressed \"Call the teacher\". Here you can also turn calling off, "
						"for example during a test." ),
					QStringLiteral(":/labaudio/hand.png") ),
	m_callTrayFeature( QStringLiteral( "LabAudioCallTray" ),
					   Feature::Flag::Session | Feature::Flag::Service | Feature::Flag::Worker,
					   Feature::Uid( "a8d3c6e2-1f47-4c9b-b2e5-6d0f9a3e7c41" ),
					   m_labAudioFeature.uid(),
					   {}, {}, {} ),
	m_features( { m_labAudioFeature, m_broadcastFeature, m_talkFeature, m_listenFeature, m_groupWorkFeature,
				  m_callsFeature, m_hubFeature, m_clientFeature, m_callTrayFeature } ),
	m_token( CryptoCore::generateChallenge() )
{
	m_hubKeepAliveTimer.setInterval( HubKeepAliveIntervalMs );
	connect( &m_hubKeepAliveTimer, &QTimer::timeout, this, &LabAudioFeaturePlugin::sendHubCommand );
}



LabAudioFeaturePlugin::~LabAudioFeaturePlugin()
{
	delete m_groupPanel;
	delete m_teacherPanel;
	delete m_callsWindow;
	delete m_callTray;
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
					  { argToString(Argument::StudentName), studentLabel( student ) } },
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
	}
	const auto description = ( feature == m_talkFeature )
								 ? tr( "You are talking with %1." ).arg( studentName( master.selectedComputerControlInterfaces().value( 0 ) ) )
								 : tr( "You are speaking to the students." );
	m_teacherPanel->setMode( description, tr( "To stop, click \"%1\" in the toolbar again." ).arg( feature.displayName() ) );
	m_teacherPanel->show();
	m_teacherPanel->raise();
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
	return DefaultHubPort + VeyonCore::sessionId();
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
												  arguments.value( argToString(Argument::StudentName), QString() ).toString() ),
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
		for( int g = 0; g < m_groups.size(); ++g )
		{
			groupTokens[QString::fromLatin1( m_groups[g].token.toHex() )] = g + 1;
		}
		sendFeatureMessage( FeatureMessage{ m_hubFeature.uid(), FeatureCommand::StartHub }
								.addArgument( Argument::Token, m_token )
								.addArgument( Argument::HubPort, hubPort() )
								.addArgument( Argument::GroupTokens, groupTokens )
								.addArgument( Argument::FocusGroup, m_focusGroup )
								.addArgument( Argument::TeacherTalks, m_teacherTalks )
								.addArgument( Argument::TeacherMuted, m_teacherMuted ),
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
		m_master = &master;
		return startGroupWork( master, computerControlInterfaces );
	}
	if( feature == m_callsFeature )
	{
		m_master = &master;
		callsWindow()->present();
		return true;
	}
	return false;
}



// ---- calling the teacher ----
// Student: tray icon worker or the audio notice -> SetCalling -> Veyon Server (LabAudioCallBoard)
// -> CallState to every connected Veyon Master (sendAsyncFeatureMessages) and back to the workers.
// Master: "Done" -> AnswerCall; "Students may call" -> SetCallsAllowed.

FeatureMessage LabAudioFeaturePlugin::callStateMessage( Feature::Uid featureUid ) const
{
	const auto state = m_callBoard.state();
	FeatureMessage message{ featureUid, FeatureCommand::CallState };
	message.addArgument( Argument::Calling, state.calling )
		.addArgument( Argument::CallsAllowed, state.allowed )
		.addArgument( Argument::CallTime, state.calling ? state.since.toMSecsSinceEpoch() : qint64( 0 ) );
	// FeatureMessage has an explicit copy constructor: return a prvalue.
	return FeatureMessage( message );
}



void LabAudioFeaturePlugin::pushCallState( VeyonServerInterface& server )
{
	auto& workers = server.featureWorkerManager();
	for( const auto& feature : { m_callTrayFeature.uid(), m_clientFeature.uid() } )
	{
		// Only inform running workers; this must not start an audio client.
		if( workers.isWorkerRunning( feature ) )
		{
			workers.sendMessageToUnmanagedSessionWorker( callStateMessage( feature ) );
		}
	}
}



void LabAudioFeaturePlugin::ensureCallTray( VeyonServerInterface& server )
{
	auto& workers = server.featureWorkerManager();
	if( workers.isWorkerRunning( m_callTrayFeature.uid() ) ||
		VeyonCore::platform().sessionFunctions().currentSessionHasUser() == false )
	{
		return;
	}
	// sendMessageToUnmanagedSessionWorker retries by itself, so do not ask again too often.
	const auto now = QDateTime::currentMSecsSinceEpoch();
	if( now - m_lastCallTrayStart.loadAcquire() < CallTrayRetryMs )
	{
		return;
	}
	m_lastCallTrayStart.storeRelease( now );
	workers.sendMessageToUnmanagedSessionWorker( callStateMessage( m_callTrayFeature.uid() ) );
}



void LabAudioFeaturePlugin::sendCalling( Feature::Uid workerFeature, bool calling )
{
	if( m_worker )
	{
		// A worker may only send messages with its own feature uid.
		m_worker->sendFeatureMessageReply( FeatureMessage{ workerFeature, FeatureCommand::SetCalling }
											   .addArgument( Argument::Calling, calling ) );
	}
}



LabAudioCallsWindow* LabAudioFeaturePlugin::callsWindow()
{
	if( m_callsWindow.isNull() )
	{
		m_callsAllowed = QSettings( QStringLiteral( "Veyon Solutions" ), QStringLiteral( "LabAudio" ) )
							 .value( QStringLiteral( "callsAllowed" ), true ).toBool();
		m_callsWindow = new LabAudioCallsWindow( m_master ? m_master->mainWindow() : nullptr );
		m_callsWindow->setCallsAllowed( m_callsAllowed );
		connect( m_callsWindow, &LabAudioCallsWindow::answered, this, &LabAudioFeaturePlugin::answerCall );
		connect( m_callsWindow, &LabAudioCallsWindow::answeredAll, this, [this]() {
			const auto ids = m_calls.keys();
			for( const auto id : ids )
			{
				answerCall( id );
			}
		} );
		connect( m_callsWindow, &LabAudioCallsWindow::callsAllowedChanged, this, &LabAudioFeaturePlugin::setCallsAllowed );
		updateCallsWindow();
	}
	return m_callsWindow;
}



void LabAudioFeaturePlugin::updateCallsWindow()
{
	QList<LabAudioCall> calls;
	for( auto it = m_calls.cbegin(); it != m_calls.cend(); ++it )
	{
		int group = 0;
		for( int g = 0; g < m_groups.size(); ++g )
		{
			if( m_groups[g].members.contains( it->computer ) )
			{
				group = g + 1;
			}
		}
		calls.append( { it.key(), studentLabel( it->computer ), group, it->since } );
	}
	std::sort( calls.begin(), calls.end(), []( const LabAudioCall& a, const LabAudioCall& b ) { return a.time < b.time; } );
	if( calls.isEmpty() == false || m_callsWindow )
	{
		callsWindow()->setCalls( calls );
	}
}



void LabAudioFeaturePlugin::answerCall( int id )
{
	const auto call = m_calls.take( id );
	if( call.computer )
	{
		call.computer->sendFeatureMessage( FeatureMessage{ m_callTrayFeature.uid(), FeatureCommand::AnswerCall } );
	}
	updateCallsWindow();
}



void LabAudioFeaturePlugin::setCallsAllowed( bool allowed )
{
	m_callsAllowed = allowed;
	QSettings( QStringLiteral( "Veyon Solutions" ), QStringLiteral( "LabAudio" ) )
		.setValue( QStringLiteral( "callsAllowed" ), allowed );

	ComputerControlInterfaceList computers;
	if( m_master )
	{
		computers = m_master->allComputerControlInterfaces();
	}
	for( const auto& call : std::as_const( m_calls ) )
	{
		if( call.computer && computers.contains( call.computer ) == false )
		{
			computers.append( call.computer );
		}
	}
	// Computers connected later learn it when a student tries to call (see handleFeatureMessage).
	sendFeatureMessage( FeatureMessage{ m_callTrayFeature.uid(), FeatureCommand::SetCallsAllowed }
							.addArgument( Argument::CallsAllowed, allowed ),
						computers );
	if( allowed == false )
	{
		m_calls.clear();
	}
	updateCallsWindow();
}



bool LabAudioFeaturePlugin::handleFeatureMessage( ComputerControlInterface::Pointer computerControlInterface,
												  const FeatureMessage& message )
{
	if( message.featureUid() != m_callTrayFeature.uid() || message.command<FeatureCommand>() != FeatureCommand::CallState ||
		computerControlInterface.isNull() )
	{
		return false;
	}

	const auto key = computerControlInterface->computer().networkObjectUid().toString();
	const bool calling = message.argument( Argument::Calling ).toBool();
	if( calling && m_callsAllowed == false )
	{
		// Calling was turned off while this computer was not connected.
		computerControlInterface->sendFeatureMessage( FeatureMessage{ m_callTrayFeature.uid(), FeatureCommand::SetCallsAllowed }
														  .addArgument( Argument::CallsAllowed, false ) );
		return true;
	}

	if( calling )
	{
		if( m_callIds.contains( key ) == false )
		{
			m_callIds[key] = m_nextCallId++;
		}
		m_calls[m_callIds[key]] = { computerControlInterface,
									QDateTime::fromMSecsSinceEpoch( message.argument( Argument::CallTime ).toLongLong(), QTimeZone::UTC ) };
	}
	else
	{
		m_calls.remove( m_callIds.value( key ) );
	}
	updateCallsWindow();
	return true;
}



bool LabAudioFeaturePlugin::handleFeatureMessageFromWorker( VeyonServerInterface& server, const FeatureMessage& message )
{
	if( ( message.featureUid() != m_callTrayFeature.uid() && message.featureUid() != m_clientFeature.uid() ) ||
		message.command<FeatureCommand>() != FeatureCommand::SetCalling )
	{
		return false;
	}
	m_callBoard.setCalling( message.argument( Argument::Calling ).toBool() );
	// Always answer with the real state, e.g. to undo a click while calls are turned off.
	pushCallState( server );
	return true;
}



void LabAudioFeaturePlugin::sendAsyncFeatureMessages( VeyonServerInterface& server, const MessageContext& messageContext )
{
	// Called from connection threads for every connected Veyon Master. The tray icon is started from
	// the main thread, which owns the worker processes.
	// This runs on every screen update, so check at most every few seconds.
	const auto now = QDateTime::currentMSecsSinceEpoch();
	const auto lastCheck = m_lastCallTrayCheck.loadAcquire();
	if( now - lastCheck > CallTrayCheckMs && m_lastCallTrayCheck.testAndSetOrdered( lastCheck, now ) )
	{
		QMetaObject::invokeMethod( this, [this, &server]() { ensureCallTray( server ); }, Qt::QueuedConnection );
	}

	static const char* versionProperty = "labAudioCallVersion";
	const auto version = m_callBoard.version();
	if( messageContext.ioDevice() && messageContext.ioDevice()->property( versionProperty ).toInt() != version )
	{
		server.sendFeatureMessageReply( messageContext, callStateMessage( m_callTrayFeature.uid() ) );
		messageContext.ioDevice()->setProperty( versionProperty, version );
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
	}
	QList<QStringList> groupNames;
	for( const auto& group : std::as_const( m_groups ) )
	{
		groupNames.append( group.names );
	}
	m_groupPanel->setGroups( groupNames );
	m_groupPanel->setFocus( m_focusGroup, m_teacherTalks );
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
	m_master = &master;
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
	if( roles & ReceiveTeacher )
	{
		showTeacherPanel( master, feature );
	}
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

	if( message.featureUid() == m_callTrayFeature.uid() )
	{
		bool changed = false;
		if( message.command<FeatureCommand>() == FeatureCommand::AnswerCall )
		{
			changed = m_callBoard.answer();
		}
		else if( message.command<FeatureCommand>() == FeatureCommand::SetCallsAllowed )
		{
			changed = m_callBoard.setAllowed( message.argument( Argument::CallsAllowed ).toBool() );
		}
		if( changed )
		{
			pushCallState( server );
		}
		return true;
	}

	if( message.featureUid() == m_hubFeature.uid() )
	{
		if( message.command<FeatureCommand>() == FeatureCommand::StopHub &&
			workers.isWorkerRunning( m_hubFeature.uid() ) == false )
		{
			return true;
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
		// The new notice shows whether this student is already calling.
		pushCallState( server );
		return true;
	}

	return false;
}



bool LabAudioFeaturePlugin::handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message )
{
	m_worker = &worker;

	if( message.featureUid() == m_callTrayFeature.uid() && message.command<FeatureCommand>() == FeatureCommand::CallState )
	{
		if( m_callTray == nullptr )
		{
			m_callTray = new LabAudioCallTray;
			connect( m_callTray, &LabAudioCallTray::callToggled, this, [this]( bool calling ) {
				sendCalling( m_callTrayFeature.uid(), calling );
			} );
		}
		m_callTray->setState( message.argument( Argument::Calling ).toBool(),
							  message.argument( Argument::CallsAllowed ).toBool() );
		return true;
	}

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
			}
			else
			{
				m_hub->setToken( message.argument( Argument::Token ).toByteArray() );
			}
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
			connect( m_client->indicator(), &LabAudioIndicator::callToggled, this, [this]( bool calling ) {
				sendCalling( m_clientFeature.uid(), calling );
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
