/*
 * LabAudioCallBoard.h - "call the teacher" state of one student computer (Veyon Server side)
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

#include <QAtomicInt>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

// Used from the Veyon Server's main thread (workers, Master commands) and from its connection
// threads (pushing the state to every connected Veyon Master), hence the mutex. Every change
// bumps the version so each Master connection learns about it once.
class LabAudioCallBoard
{
public:
	struct State
	{
		bool calling{false};
		bool allowed{true};
		QDateTime since;	// valid while calling
	};

	State state() const
	{
		QMutexLocker locker( &m_mutex );
		return m_state;
	}

	int version() const
	{
		return m_version.loadAcquire();
	}

	// Returns false if nothing changed (e.g. a call while calls are turned off).
	bool setCalling( bool calling )
	{
		QMutexLocker locker( &m_mutex );
		if( calling && m_state.allowed == false )
		{
			return false;
		}
		if( calling == m_state.calling )
		{
			return false;
		}
		m_state.calling = calling;
		m_state.since = calling ? QDateTime::currentDateTimeUtc() : QDateTime();
		m_version.fetchAndAddOrdered( 1 );
		return true;
	}

	bool answer()
	{
		return setCalling( false );
	}

	bool setAllowed( bool allowed )
	{
		QMutexLocker locker( &m_mutex );
		if( allowed == m_state.allowed )
		{
			return false;
		}
		m_state.allowed = allowed;
		if( allowed == false )
		{
			m_state.calling = false;
			m_state.since = {};
		}
		m_version.fetchAndAddOrdered( 1 );
		return true;
	}

private:
	mutable QMutex m_mutex;
	State m_state;
	QAtomicInt m_version{1};
};
