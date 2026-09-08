/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// EA Pacific
// John McDonald, Jr
// Do not distribute

#pragma once

#ifndef _AUDIOREQUEST_H_
#define _AUDIOREQUEST_H_

#include "Common/GameAudio.h"
#include "Common/GameMemory.h"

class AudioEventRTS;

enum RequestType
{
	AR_Play,
	AR_Pause,
	AR_Stop
};

struct AudioRequest : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AudioRequest, "AudioRequest" )

public:
	RequestType m_request;

	//
	// These two used to share a union, told apart by m_usePendingEvent.  A play request fills in
	// the event pointer, so the kill-by-handle search compared a pointer against a handle, never
	// matched, and a sound killed before it started played anyway.  They are eight bytes; sharing
	// them bought nothing and cost that.
	//
	AudioEventRTS *m_pendingEvent;
	AudioHandle m_handleToInteractOn;

	Bool m_usePendingEvent;
	Bool m_requiresCheckForSample;

	//
	// The pool hands out raw memory, and allocateAudioRequest only ever set m_usePendingEvent.
	// Between that call and the caller assigning the event, m_pendingEvent held whatever the last
	// occupant of the block left there - which the destructor would then delete.
	//
	AudioRequest()
		: m_request(AR_Play),
			m_pendingEvent(NULL),
			m_handleToInteractOn(0),
			m_usePendingEvent(FALSE),
			m_requiresCheckForSample(FALSE)
	{ }

	/** A play request owns the event it is carrying until something takes it - the destructor frees
		* whatever is left, so a request dropped without being processed no longer leaks it. */
	AudioEventRTS *releasePendingEvent();
};

#endif // _AUDIOREQUEST_H_
