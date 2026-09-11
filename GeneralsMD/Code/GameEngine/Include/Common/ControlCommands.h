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

// FILE: ControlCommands.h ///////////////////////////////////////////////////////////////////////
//
// What the -control command files share with the socket: the command's words, the reply being
// written, and the one way a command that finishes on a later pass sends that reply.  Only the four
// Control*.cpp files include this.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CONTROLCOMMANDS_H_
#define __CONTROLCOMMANDS_H_

#include "Lib/BaseType.h"

#include <string>
#include <vector>

class GameWindow;

// ------------------------------------------------------------------------------------------------
/** A JSON document written front to back.  Field names are the caller's own literals; every value is
	  escaped.  A NULL name writes an array element. */
// ------------------------------------------------------------------------------------------------
class ControlJson
{
public:
	ControlJson( void );

	void clear( void );
	void beginObject( const char *name = NULL );
	void endObject( void );
	void beginArray( const char *name = NULL );
	void endArray( void );

	void addInt( const char *name, Int value );
	void addReal( const char *name, Real value );
	void addBool( const char *name, Bool value );
	void addString( const char *name, const char *value );
	void addWide( const char *name, const WideChar *value );

	const std::string &getText( void ) const { return m_text; }

private:
	void writeName( const char *name );

	std::string m_text;
	std::vector<Bool> m_isFirstInContainer;
};

// ------------------------------------------------------------------------------------------------
/** What a command handler says happened. */
// ------------------------------------------------------------------------------------------------
enum ControlOutcome
{
	CONTROL_NOT_MINE = 0,		///< the verb belongs to another file
	CONTROL_DONE,						///< send the reply now
	CONTROL_FAILED,					///< send command.error now
	CONTROL_LATER						///< the handler calls ControlServer_finish when the command has finished
};

// ------------------------------------------------------------------------------------------------
/** The command being carried out.  The reply arrives opened, with "ok":true already in it. */
// ------------------------------------------------------------------------------------------------
struct ControlCommand
{
	std::vector<std::string> words;
	ControlJson reply;
	std::string error;
};

/** The command a CONTROL_LATER handler is still working on. */
extern ControlCommand &ControlServer_current( void );

/** Send the reply of the command that returned CONTROL_LATER. */
extern void ControlServer_finish( ControlOutcome outcome );

/** A word of the command, or "" past the end. */
extern const char *ControlCommand_word( const ControlCommand &command, size_t index );

/** A word as a number.  FALSE when the word is missing or is not a number. */
extern Bool ControlCommand_getInt( const ControlCommand &command, size_t index, Int *value );
extern Bool ControlCommand_getReal( const ControlCommand &command, size_t index, Real *value );

/** Record why a command failed, for a handler to return in one line. */
inline ControlOutcome ControlCommand_fail( ControlCommand &command, const std::string &why )
{
	command.error = why;
	return CONTROL_FAILED;
}

/** Whether a match is being played, as opposed to the shell or its background map. */
extern Bool ControlServer_isMatchRunning( void );

// ControlQuery.cpp
extern ControlOutcome ControlQuery_handle( ControlCommand &command );
extern void ControlQuery_tick( void );
extern GameWindow *ControlQuery_findWindow( const char *name );

// ControlInput.cpp
extern ControlOutcome ControlInput_handle( ControlCommand &command );
extern void ControlInput_tick( void );
extern void ControlInput_logicFrame( void );

// ControlActions.cpp
extern ControlOutcome ControlActions_handle( ControlCommand &command );

#endif // __CONTROLCOMMANDS_H_
