/*
 * LabAudioCallsPlugin.cpp - students call the teacher at any time
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
#include <QSettings>
#include <QTimeZone>

#include <algorithm>

#include "FeatureWorkerManager.h"
#include "LabAudioCallProtocol.h"
#include "LabAudioCallsPlugin.h"
#include "PlatformPluginInterface.h"
#include "PlatformSessionFunctions.h"
#include "VeyonMasterInterface.h"
#include "VeyonServerInterface.h"
#include "VeyonWorkerInterface.h"

using namespace LabAudioCallProtocol;



LabAudioCallsPlugin::LabAudioCallsPlugin( QObject* parent ) :
	QObject( parent ),
	m_callsFeature( QStringLiteral( "LabAudioStudentCalls" ),
					Feature::Flag::Action | Feature::Flag::Master,
					Feature::Uid( "5e0b1f7a-3c2d-4b8e-9f61-7a4d2c8e1b05" ),
					Feature::Uid(),
					tr( "Student calls" ), {},
					tr( "Students who pressed \"Call the teacher\". Here you can also turn calling off, "
						"for example during a test." ),
					QStringLiteral(":/labaudiocalls/hand.png") ),
	m_callTrayFeature( QStringLiteral( "LabAudioCallTray" ),
					   Feature::Flag::Session | Feature::Flag::Service | Feature::Flag::Worker,
					   Feature::Uid( "a8d3c6e2-1f47-4c9b-b2e5-6d0f9a3e7c41" ),
					   Feature::Uid(),
					   {}, {}, {} ),
	m_features( { m_callsFeature, m_callTrayFeature } ),
	m_configuration( &VeyonCore::config() )
{
}



LabAudioCallsPlugin::~LabAudioCallsPlugin()
{
	delete m_callsWindow;
	delete m_callTray;
}



bool LabAudioCallsPlugin::controlFeature( Feature::Uid featureUid, Operation operation, const QVariantMap& arguments,
										  const ComputerControlInterfaceList& computerControlInterfaces )
{
	Q_UNUSED(featureUid)
	Q_UNUSED(operation)
	Q_UNUSED(arguments)
	Q_UNUSED(computerControlInterfaces)
	return false;
}



bool LabAudioCallsPlugin::startFeature( VeyonMasterInterface& master, const Feature& feature,
										const ComputerControlInterfaceList& computerControlInterfaces )
{
	Q_UNUSED(computerControlInterfaces)
	if( feature == m_callsFeature )
	{
		m_master = &master;
		callsWindow()->present();
		return true;
	}
	return false;
}



QString LabAudioCallsPlugin::studentLabel( const ComputerControlInterface::Pointer& computerControlInterface )
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
	if( user.isEmpty() || user == computerName )
	{
		return computerName;
	}
	return computerName.isEmpty() ? user : QStringLiteral( "%1 (%2)" ).arg( user, computerName );
}



FeatureMessage LabAudioCallsPlugin::callStateMessage( Feature::Uid featureUid ) const
{
	const auto state = m_callBoard.state();
	FeatureMessage message{ featureUid, CallState };
	message.addArgument( Calling, state.calling )
		.addArgument( CallsAllowed, state.allowed )
		.addArgument( CallTime, state.calling ? state.since.toMSecsSinceEpoch() : qint64( 0 ) );
	// FeatureMessage has an explicit copy constructor: return a prvalue.
	return FeatureMessage( message );
}



void LabAudioCallsPlugin::pushCallState( VeyonServerInterface& server )
{
	auto& workers = server.featureWorkerManager();
	for( const auto& feature : { m_callTrayFeature.uid(), LabAudioClientFeatureUid } )
	{
		// Only inform running workers; this must not start an audio client.
		if( workers.isWorkerRunning( feature ) )
		{
			workers.sendMessageToUnmanagedSessionWorker( callStateMessage( feature ) );
		}
	}
}



void LabAudioCallsPlugin::ensureCallTray( VeyonServerInterface& server )
{
	auto& workers = server.featureWorkerManager();
	if( m_configuration.callTrayEnabled() == false )
	{
		// Turned off in the Veyon Configurator ("Lab audio" page); the notice button still works.
		if( workers.isWorkerRunning( m_callTrayFeature.uid() ) )
		{
			workers.stopWorker( m_callTrayFeature.uid() );
		}
		return;
	}
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



LabAudioCallsWindow* LabAudioCallsPlugin::callsWindow()
{
	if( m_callsWindow.isNull() )
	{
		m_callsAllowed = QSettings( QStringLiteral( "Veyon Solutions" ), QStringLiteral( "LabAudio" ) )
							 .value( QStringLiteral( "callsAllowed" ), true ).toBool();
		m_callsWindow = new LabAudioCallsWindow( m_master ? m_master->mainWindow() : nullptr );
		m_callsWindow->setCallsAllowed( m_callsAllowed );
		connect( m_callsWindow, &LabAudioCallsWindow::answered, this, &LabAudioCallsPlugin::answerCall );
		connect( m_callsWindow, &LabAudioCallsWindow::answeredAll, this, [this]() {
			const auto ids = m_calls.keys();
			for( const auto id : ids )
			{
				answerCall( id );
			}
		} );
		connect( m_callsWindow, &LabAudioCallsWindow::callsAllowedChanged, this, &LabAudioCallsPlugin::setCallsAllowed );
		updateCallsWindow();
	}
	return m_callsWindow;
}



void LabAudioCallsPlugin::updateCallsWindow()
{
	// Group numbers come from the LabAudio plugin while group work is running.
	const auto groups = qApp->property( GroupOfComputerProperty ).toMap();
	QList<LabAudioCall> calls;
	for( auto it = m_calls.cbegin(); it != m_calls.cend(); ++it )
	{
		const auto group = it->computer ? groups.value( it->computer->computer().networkObjectUid().toString() ).toInt() : 0;
		calls.append( { it.key(), studentLabel( it->computer ), group, it->since } );
	}
	std::sort( calls.begin(), calls.end(), []( const LabAudioCall& a, const LabAudioCall& b ) { return a.time < b.time; } );
	if( calls.isEmpty() == false || m_callsWindow )
	{
		callsWindow()->setCalls( calls );
	}
}



void LabAudioCallsPlugin::answerCall( int id )
{
	const auto call = m_calls.take( id );
	if( call.computer )
	{
		call.computer->sendFeatureMessage( FeatureMessage{ m_callTrayFeature.uid(), AnswerCall } );
	}
	updateCallsWindow();
}



void LabAudioCallsPlugin::setCallsAllowed( bool allowed )
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
	sendFeatureMessage( FeatureMessage{ m_callTrayFeature.uid(), SetCallsAllowed }.addArgument( CallsAllowed, allowed ),
						computers );
	if( allowed == false )
	{
		m_calls.clear();
	}
	updateCallsWindow();
}



bool LabAudioCallsPlugin::handleFeatureMessage( ComputerControlInterface::Pointer computerControlInterface,
												const FeatureMessage& message )
{
	if( message.featureUid() != m_callTrayFeature.uid() || message.command<Command>() != CallState ||
		computerControlInterface.isNull() )
	{
		return false;
	}

	const auto key = computerControlInterface->computer().networkObjectUid().toString();
	const bool calling = message.argument( Calling ).toBool();
	if( calling && m_callsAllowed == false )
	{
		// Calling was turned off while this computer was not connected.
		computerControlInterface->sendFeatureMessage( FeatureMessage{ m_callTrayFeature.uid(), SetCallsAllowed }
														  .addArgument( CallsAllowed, false ) );
		return true;
	}

	if( calling )
	{
		if( m_callIds.contains( key ) == false )
		{
			m_callIds[key] = m_nextCallId++;
		}
		m_calls[m_callIds[key]] = { computerControlInterface,
									QDateTime::fromMSecsSinceEpoch( message.argument( CallTime ).toLongLong(), QTimeZone::UTC ) };
	}
	else
	{
		m_calls.remove( m_callIds.value( key ) );
	}
	updateCallsWindow();
	return true;
}



bool LabAudioCallsPlugin::handleFeatureMessage( VeyonServerInterface& server, const MessageContext& messageContext,
												const FeatureMessage& message )
{
	Q_UNUSED(messageContext)

	if( message.featureUid() == LabAudioClientFeatureUid && message.command<Command>() == StartClient )
	{
		// A new lab audio notice shows whether this student is already calling. Queued, so the LabAudio
		// plugin has started the client worker by then.
		QMetaObject::invokeMethod( this, [this, &server]() { pushCallState( server ); }, Qt::QueuedConnection );
		return false;
	}

	if( message.featureUid() != m_callTrayFeature.uid() )
	{
		return false;
	}
	bool changed = false;
	if( message.command<Command>() == AnswerCall )
	{
		changed = m_callBoard.answer();
	}
	else if( message.command<Command>() == SetCallsAllowed )
	{
		changed = m_callBoard.setAllowed( message.argument( CallsAllowed ).toBool() );
	}
	if( changed )
	{
		pushCallState( server );
	}
	return true;
}



bool LabAudioCallsPlugin::handleFeatureMessageFromWorker( VeyonServerInterface& server, const FeatureMessage& message )
{
	if( ( message.featureUid() != m_callTrayFeature.uid() && message.featureUid() != LabAudioClientFeatureUid ) ||
		message.command<Command>() != SetCalling )
	{
		return false;
	}
	m_callBoard.setCalling( message.argument( Calling ).toBool() );
	// Always answer with the real state, e.g. to undo a click while calls are turned off.
	pushCallState( server );
	return true;
}



void LabAudioCallsPlugin::sendAsyncFeatureMessages( VeyonServerInterface& server, const MessageContext& messageContext )
{
	// Called from connection threads for every connected Veyon Master, on every screen update. The tray
	// icon is started from the main thread, which owns the worker processes, at most every few seconds.
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



bool LabAudioCallsPlugin::handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message )
{
	if( message.featureUid() != m_callTrayFeature.uid() || message.command<Command>() != CallState )
	{
		return false;
	}
	m_worker = &worker;
	if( m_callTray == nullptr )
	{
		m_callTray = new LabAudioCallTray;
		connect( m_callTray, &LabAudioCallTray::callToggled, this, [this]( bool calling ) {
			// A worker may only send messages with its own feature uid.
			m_worker->sendFeatureMessageReply( FeatureMessage{ m_callTrayFeature.uid(), SetCalling }
												   .addArgument( Calling, calling ) );
		} );
	}
	m_callTray->setState( message.argument( Calling ).toBool(), message.argument( CallsAllowed ).toBool() );
	return true;
}



// The same settings as the LabAudio plugin (section "LabAudio"); each plugin library has its own proxy.
IMPLEMENT_CONFIG_PROXY(LabAudioConfiguration)
