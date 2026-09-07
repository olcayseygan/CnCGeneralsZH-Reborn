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

// FILE: ScenarioDrill.h /////////////////////////////////////////////////////////////////////////
//
// -scenario <name>: play a match out of a text file instead of out of a person or an AI.
//
// A measurement needs the same units doing the same thing twice, and -autoskirmish cannot give
// that: it hands every slot PLAYERTEMPLATE_RANDOM and the faction falls out of the seed, so
// "enough angry mobs to see them cost anything" is a thing you wait for rather than a thing you
// ask for.  A scenario file names the unit, the place, the count and the frame, so the same
// command line puts the same army on the same ground in two different builds.
//
// Orders go out the way -groupdrill's do - an AIGroup built inside the logic frame, ordered with
// CMD_FROM_SCRIPT - and not through TheMessageStream.  Two reasons.  A GameMessage is stamped with
// the local player and the fake index a harness would write does not survive a network game
// (NetCommandMsg re-stamps it with the sender's).  Worse for a measurement, messages propagate on
// the render pass, so which logic frame an order lands on depends on how fast the machine drew -
// and two builds that draw at different speeds would then be playing different scenarios.  A tick
// keyed to the logic frame has neither problem.
//
// The bill for that is the same one -groupdrill pays: these orders exist nowhere in the command
// stream, so a run driven by a scenario cannot be replayed.  Repeatability comes from the file
// plus -seed instead, which is what an A/B needs anyway.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SCENARIODRILL_H_
#define __SCENARIODRILL_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

// ------------------------------------------------------------------------------------------------
/** What one scenario line asks for. */
// ------------------------------------------------------------------------------------------------
enum ScenarioActionType
{
	SCENARIO_ACTION_SPAWN = 0,		///< spawn <slot> <template> <count> <x> <y> [spacing]
	SCENARIO_ACTION_MOVE,					///< move <slot> <selector> <x> <y>
	SCENARIO_ACTION_ATTACKMOVE,		///< attackmove <slot> <selector> <x> <y>
	SCENARIO_ACTION_ATTACK,				///< attack <slot> <selector> <targetSlot> <targetSelector>
	SCENARIO_ACTION_STOP					///< stop <slot> <selector>
};

// ------------------------------------------------------------------------------------------------
/** Why a line was not turned into an action.  A scenario that silently drops half its orders is
	  worse than one that refuses to load, because the run still produces numbers. */
// ------------------------------------------------------------------------------------------------
enum ScenarioParseResult
{
	SCENARIO_PARSE_OK = 0,
	SCENARIO_PARSE_BLANK,					///< a comment or an empty line: nothing to do, not an error
	SCENARIO_PARSE_BAD_FRAME,
	SCENARIO_PARSE_BAD_ACTION,
	SCENARIO_PARSE_BAD_SLOT,
	SCENARIO_PARSE_BAD_COUNT,
	SCENARIO_PARSE_MISSING_ARGS
};

// ------------------------------------------------------------------------------------------------
/** One parsed line.  Which fields carry anything depends on the action - see the enum above. */
// ------------------------------------------------------------------------------------------------
struct ScenarioAction
{
	UnsignedInt frame;						///< the logic frame this fires on
	ScenarioActionType action;
	Int slot;											///< the seat, the same number -side takes; not an index into ThePlayerList
	AsciiString selector;					///< a template name, or "*" for everything that seat owns
	Coord2D at;										///< spawn, move and attackmove target
	Int count;										///< how many to spawn
	Real spacing;									///< how far apart to spawn them, in world units
	Int targetSlot;								///< whose units to attack
	AsciiString targetSelector;		///< which of them
};

/** Turn one line of a scenario file into an action.  Pure: no engine state is read, which is what
	  lets the parser be tested without booting a game. */
extern ScenarioParseResult ScenarioDrill_parseLine( const char *line, ScenarioAction *action );

/** The name of a parse result, for the log. */
extern const char *ScenarioDrill_parseResultName( ScenarioParseResult result );

/** Run whatever the file asks for on this logic frame.  Called from GameLogic::update. */
extern void ScenarioDrill_tick( void );

/** Carry out one action now.  The scenario file is one caller; the control server is the other, so
	  that a command typed down a socket means exactly what the same line means in a file.  Only safe
	  from inside a logic frame, and only while a match is running. */
extern Bool ScenarioDrill_execute( const ScenarioAction &action );

/** One line for the end-of-run summary: how much of the file actually happened. */
extern const char *ScenarioDrill_report( void );

#endif // __SCENARIODRILL_H_
