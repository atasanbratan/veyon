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
#include <QTcpSocket>

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
	m_features( { m_labAudioFeature, m_broadcastFeature, m_talkFeature, m_listenFeature,
				  m_hubFeature, m_clientFeature } ),
	m_token( CryptoCore::generateChallenge() )
{
	m_hubKeepAliveTimer.setInterval( HubKeepAliveIntervalMs );
	connect( &m_hubKeepAliveTimer, &QTimer::timeout, this, &LabAudioFeaturePlugin::sendHubCommand );
}



LabAudioFeaturePlugin::~LabAudioFeaturePlugin()
{
	delete m_client;
	delete m_hub;
}



Feature::Uid LabAudioFeaturePlugin::metaFeature( Feature::Uid featureUid ) const
{
	if( featureUid == m_broadcastFeature.uid() || featureUid == m_talkFeature.uid() ||
		featureUid == m_listenFeature.uid() )
	{
		return m_clientFeature.uid();
	}
	return FeatureProviderInterface::metaFeature( featureUid );
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
									.addArgument( Argument::Token, arguments.value( argToString(Argument::Token), m_token ) )
									.addArgument( Argument::HubHost, arguments.value( argToString(Argument::HubHost) ) )
									.addArgument( Argument::HubAddresses, localAddresses() )
									.addArgument( Argument::HubPort, arguments.value( argToString(Argument::HubPort), hubPort() ) )
									.addArgument( Argument::Roles, arguments.value( argToString(Argument::Roles),
																					int( ReceiveTeacher ) ) ),
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
		sendFeatureMessage( FeatureMessage{ m_hubFeature.uid(), FeatureCommand::StartHub }
								.addArgument( Argument::Token, m_token )
								.addArgument( Argument::HubPort, hubPort() ),
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
	return false;
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
	controlFeature( m_clientFeature.uid(), Operation::Start,
					{ { argToString(Argument::Roles), int( roles ) } }, targets );
	++m_activeModes;
	return true;
}



bool LabAudioFeaturePlugin::stopFeature( VeyonMasterInterface& master, const Feature& feature,
										 const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( feature == m_broadcastFeature || feature == m_talkFeature || feature == m_listenFeature ||
		feature == m_labAudioFeature )
	{
		controlFeature( m_clientFeature.uid(), Operation::Stop, {}, computerControlInterfaces );
		m_activeModes = qMax( 0, m_activeModes - 1 );
		if( m_activeModes == 0 || feature == m_labAudioFeature )
		{
			m_activeModes = 0;
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



bool LabAudioFeaturePlugin::handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message )
{
	Q_UNUSED(worker)

	if( message.featureUid() == m_hubFeature.uid() )
	{
		switch( message.command<FeatureCommand>() )
		{
		case FeatureCommand::StartHub:
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
			return true;
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
										   roles );
			return true;
		}
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
