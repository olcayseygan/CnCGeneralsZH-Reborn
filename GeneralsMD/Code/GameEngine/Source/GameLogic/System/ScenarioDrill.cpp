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

// FILE: ScenarioDrill.cpp ///////////////////////////////////////////////////////////////////////
//
// -scenario <name>: play a match out of Run/Scenarios/<name>.txt.  See ScenarioDrill.h for why the
// orders go out through an AIGroup inside the logic frame rather than through the message stream.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScenarioDrill.h"
#include "GameLogic/TerrainLogic.h"

#include <algorithm>
#include <vector>

// ------------------------------------------------------------------------------------------------
// the shape of a scenario line
// ------------------------------------------------------------------------------------------------

static const Int SCENARIO_MAX_TOKENS = 10;
static const char SCENARIO_COMMENT_CHAR = '#';
static const char *SCENARIO_SELECTOR_ALL = "*";

/// how far apart spawned units stand, when the line does not say
static const Real SCENARIO_DEFAULT_SPACING = 30.0f;

/// "keep shooting until told otherwise" - the same count the mob's own self-tasking passes
static const Int SCENARIO_ATTACK_SHOTS = 999;

// token counts, including the frame and the action name
static const Int SCENARIO_TOKENS_STOP = 4;
static const Int SCENARIO_TOKENS_MOVE = 6;
static const Int SCENARIO_TOKENS_ATTACK = 6;
static const Int SCENARIO_TOKENS_SPAWN = 7;
static const Int SCENARIO_TOKENS_SPAWN_WITH_SPACING = 8;

static const char *SCENARIO_DIRECTORY = "Scenarios\\";
static const char *SCENARIO_EXTENSION = ".txt";

static const Int SCENARIO_REPORT_LENGTH = 192;

// ------------------------------------------------------------------------------------------------
// parsing
// ------------------------------------------------------------------------------------------------

static Bool isScenarioSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/** Split a line into whitespace-separated tokens, stopping at a comment.  Returns how many it
	  found, which may be more than maxTokens - the caller checks the count it wanted. */
static Int tokenizeScenarioLine( const char *line, AsciiString *tokens, Int maxTokens )
{
	Int found = 0;
	const char *at = line;

	while (*at != 0)
	{
		while (*at != 0 && isScenarioSpace( *at ))
			++at;

		if (*at == 0 || *at == SCENARIO_COMMENT_CHAR)
			break;

		const char *start = at;
		while (*at != 0 && !isScenarioSpace( *at ) && *at != SCENARIO_COMMENT_CHAR)
			++at;

		if (found < maxTokens)
		{
			const Int length = (Int)(at - start);
			char word[ 128 ];
			const Int copied = (length < (Int)sizeof( word ) - 1) ? length : (Int)sizeof( word ) - 1;
			memcpy( word, start, copied );
			word[ copied ] = 0;
			tokens[ found ].set( word );
		}
		++found;
	}

	return found;
}

/** atoi cannot tell "0" from "banana", and a scenario that quietly reads a typo as frame 0 fires
	  its whole file on the first frame.  So spell the digits out. */
static Bool parseWholeNumber( const AsciiString &token, Int *value )
{
	const char *at = token.str();
	if (at == NULL || *at == 0)
		return FALSE;

	Int accumulated = 0;
	while (*at != 0)
	{
		if (*at < '0' || *at > '9')
			return FALSE;
		accumulated = accumulated * 10 + (*at - '0');
		++at;
	}

	*value = accumulated;
	return TRUE;
}

static Bool parseActionType( const AsciiString &token, ScenarioActionType *action )
{
	if (token == "spawn")
		*action = SCENARIO_ACTION_SPAWN;
	else if (token == "move")
		*action = SCENARIO_ACTION_MOVE;
	else if (token == "attackmove")
		*action = SCENARIO_ACTION_ATTACKMOVE;
	else if (token == "attack")
		*action = SCENARIO_ACTION_ATTACK;
	else if (token == "stop")
		*action = SCENARIO_ACTION_STOP;
	else
		return FALSE;

	return TRUE;
}

static Int tokensNeededFor( ScenarioActionType action )
{
	switch (action)
	{
		case SCENARIO_ACTION_SPAWN:				return SCENARIO_TOKENS_SPAWN;
		case SCENARIO_ACTION_MOVE:				return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_ATTACKMOVE:	return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_ATTACK:			return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_STOP:				return SCENARIO_TOKENS_STOP;
	}
	return SCENARIO_TOKENS_STOP;
}

ScenarioParseResult ScenarioDrill_parseLine( const char *line, ScenarioAction *action )
{
	AsciiString tokens[ SCENARIO_MAX_TOKENS ];
	const Int count = tokenizeScenarioLine( line, tokens, SCENARIO_MAX_TOKENS );
	if (count == 0)
		return SCENARIO_PARSE_BLANK;

	// a bare frame number with nothing after it is a line somebody started and did not finish
	if (count < SCENARIO_TOKENS_STOP)
		return SCENARIO_PARSE_MISSING_ARGS;

	Int frame = 0;
	if (!parseWholeNumber( tokens[ 0 ], &frame ))
		return SCENARIO_PARSE_BAD_FRAME;

	ScenarioActionType actionType;
	if (!parseActionType( tokens[ 1 ], &actionType ))
		return SCENARIO_PARSE_BAD_ACTION;

	Int slot = 0;
	if (!parseWholeNumber( tokens[ 2 ], &slot ))
		return SCENARIO_PARSE_BAD_SLOT;

	if (count < tokensNeededFor( actionType ))
		return SCENARIO_PARSE_MISSING_ARGS;

	action->frame = (UnsignedInt)frame;
	action->action = actionType;
	action->slot = slot;
	action->selector = tokens[ 3 ];
	action->at.x = 0.0f;
	action->at.y = 0.0f;
	action->count = 1;
	action->spacing = SCENARIO_DEFAULT_SPACING;
	action->targetSlot = 0;
	action->targetSelector.clear();

	switch (actionType)
	{
		case SCENARIO_ACTION_SPAWN:
		{
			if (!parseWholeNumber( tokens[ 4 ], &action->count ) || action->count < 1)
				return SCENARIO_PARSE_BAD_COUNT;
			action->at.x = (Real)atof( tokens[ 5 ].str() );
			action->at.y = (Real)atof( tokens[ 6 ].str() );
			if (count >= SCENARIO_TOKENS_SPAWN_WITH_SPACING)
				action->spacing = (Real)atof( tokens[ 7 ].str() );
			break;
		}

		case SCENARIO_ACTION_MOVE:
		case SCENARIO_ACTION_ATTACKMOVE:
		{
			action->at.x = (Real)atof( tokens[ 4 ].str() );
			action->at.y = (Real)atof( tokens[ 5 ].str() );
			break;
		}

		case SCENARIO_ACTION_ATTACK:
		{
			if (!parseWholeNumber( tokens[ 4 ], &action->targetSlot ))
				return SCENARIO_PARSE_BAD_SLOT;
			action->targetSelector = tokens[ 5 ];
			break;
		}

		case SCENARIO_ACTION_STOP:
			break;
	}

	return SCENARIO_PARSE_OK;
}

const char *ScenarioDrill_parseResultName( ScenarioParseResult result )
{
	switch (result)
	{
		case SCENARIO_PARSE_OK:						return "ok";
		case SCENARIO_PARSE_BLANK:				return "blank";
		case SCENARIO_PARSE_BAD_FRAME:		return "frame is not a whole number";
		case SCENARIO_PARSE_BAD_ACTION:		return "no such action";
		case SCENARIO_PARSE_BAD_SLOT:		return "slot is not a whole number";
		case SCENARIO_PARSE_BAD_COUNT:		return "count is not a whole number above zero";
		case SCENARIO_PARSE_MISSING_ARGS:	return "not enough arguments for this action";
	}
	return "unknown";
}

// ------------------------------------------------------------------------------------------------
// the loaded file, and what happened to it
// ------------------------------------------------------------------------------------------------

static std::vector<ScenarioAction> theScenarioActions;
static Int theScenarioCursor = 0;
static Bool theScenarioLoaded = FALSE;
static Int theScenarioActionsRun = 0;
static Int theScenarioActionsFailed = 0;
static Int theScenarioUnitsSpawned = 0;
static UnsignedInt theScenarioLastFrame = 0;
static char theScenarioReport[ SCENARIO_REPORT_LENGTH ];

static void resetScenario( void )
{
	theScenarioActions.clear();
	theScenarioCursor = 0;
	theScenarioLoaded = FALSE;
	theScenarioActionsRun = 0;
	theScenarioActionsFailed = 0;
	theScenarioUnitsSpawned = 0;
}

/** Sorted by frame, and stable so two actions on the same frame keep the order the file wrote
	  them in.  An unstable sort would make the run depend on the standard library's mood, which is
	  the one thing a repeatable measurement cannot have. */
static Bool scenarioActionIsEarlier( const ScenarioAction &left, const ScenarioAction &right )
{
	return left.frame < right.frame;
}

static void loadScenario( void )
{
	theScenarioLoaded = TRUE;

	AsciiString path;
	path.set( SCENARIO_DIRECTORY );
	path.concat( TheGlobalData->m_scenarioFile );
	path.concat( SCENARIO_EXTENSION );

	/* Read it as bytes, not as text.  readEntireAndClose sizes its buffer from size() and then reads
		 that many bytes; in text mode the CRLF translation hands back fewer than it asked for and the
		 tail of the buffer is whatever was in the heap.  So take the file as it lies and let the
		 tokenizer treat a carriage return as the whitespace it is. */
	File *file = TheFileSystem->openFile( path.str(), File::READ );
	if (file == NULL)
	{
		DEBUG_LOG(("SCENARIO: cannot open '%s'\n", path.str()));
		return;
	}

	const Int fileSize = file->size();
	char *contents = file->readEntireAndClose();		// this also closes and frees the File
	if (contents == NULL)
	{
		DEBUG_LOG(("SCENARIO: '%s' read as nothing\n", path.str()));
		return;
	}

	Int lineNumber = 0;
	Int at = 0;
	while (at < fileSize)
	{
		Int end = at;
		while (end < fileSize && contents[ end ] != '\n')
			++end;

		const Int length = end - at;
		char line[ 512 ];
		const Int copied = (length < (Int)sizeof( line ) - 1) ? length : (Int)sizeof( line ) - 1;
		memcpy( line, contents + at, copied );
		line[ copied ] = 0;
		++lineNumber;

		ScenarioAction action;
		const ScenarioParseResult result = ScenarioDrill_parseLine( line, &action );
		if (result == SCENARIO_PARSE_OK)
		{
			theScenarioActions.push_back( action );
		}
		else if (result != SCENARIO_PARSE_BLANK)
		{
			DEBUG_LOG(("SCENARIO: %s line %d: %s\n",
								 path.str(), lineNumber, ScenarioDrill_parseResultName( result )));
			++theScenarioActionsFailed;
		}

		at = end + 1;
	}

	delete [] contents;

	std::stable_sort( theScenarioActions.begin(), theScenarioActions.end(), scenarioActionIsEarlier );

	DEBUG_LOG(("SCENARIO: loaded '%s', %d actions, %d lines refused\n",
						 path.str(), (Int)theScenarioActions.size(), theScenarioActionsFailed));
}

// ------------------------------------------------------------------------------------------------
// doing what the file asked for
// ------------------------------------------------------------------------------------------------

/** The file names a seat, the same number -side takes.  ThePlayerList is not that list: it opens
	  with a neutral player and a civilian one, so slot 0 was player 2 in the first two-player run and
	  would be something else in another shape of game.  assignSlotIndices recorded the real mapping
	  when the game started, so ask it rather than carrying an offset around. */
static Player *findPlayerForSlot( Int slot )
{
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		if (ThePlayerList->getSlotIndex( i ) == slot)
			return ThePlayerList->getNthPlayer( i );
	}
	return NULL;
}

static Bool selectorMatches( const AsciiString &selector, const Object *obj )
{
	if (selector == SCENARIO_SELECTOR_ALL)
		return TRUE;

	const ThingTemplate *tmpl = obj->getTemplate();
	if (tmpl == NULL)
		return FALSE;

	return tmpl->getName() == selector;
}

/** Everything this player owns that matches the selector and can be given an order at all. */
static Int gatherIntoGroup( Player *player, const AsciiString &selector, AIGroup *group )
{
	Int taken = 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (obj->getControllingPlayer() != player || obj->isEffectivelyDead())
			continue;
		if (obj->getAIUpdateInterface() == NULL)
			continue;
		if (!selectorMatches( selector, obj ))
			continue;

		group->add( obj );
		++taken;
	}
	return taken;
}

static Object *findFirstMatching( Player *player, const AsciiString &selector )
{
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (obj->getControllingPlayer() != player || obj->isEffectivelyDead())
			continue;
		if (selectorMatches( selector, obj ))
			return obj;
	}
	return NULL;
}

/** The recipe the debug spawn path uses (GameLogic.cpp's unitTimings): a template's onCreate ran in
	  the constructor, but a unit that was never built still has to be told it is finished, or half
	  its modules never start.  The team has to be activated too, or it counts as an empty team. */
static Bool spawnOne( const ThingTemplate *tmpl, Team *team, const Coord3D *pos )
{
	Object *obj = TheThingFactory->newObject( tmpl, team );
	if (obj == NULL)
		return FALSE;

	obj->setOrientation( 0 );
	obj->setPosition( pos );

	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		CreateModuleInterface *create = (*m)->getCreate();
		if (create == NULL)
			continue;
		create->onBuildComplete();
	}

	team->setActive();
	TheAI->pathfinder()->addObjectToPathfindMap( obj );
	return TRUE;
}

static Bool executeSpawn( const ScenarioAction &action, Player *player )
{
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( action.selector, FALSE );
	if (tmpl == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d spawn: no template named '%s'\n",
							 action.frame, action.selector.str()));
		return FALSE;
	}

	Team *team = player->getDefaultTeam();

	// a square-ish block centred on the point the file named, so the author names where the army is
	// rather than where its top left corner is
	Int columns = 1;
	while (columns * columns < action.count)
		++columns;
	const Int rows = (action.count + columns - 1) / columns;
	const Real halfWidth = (columns - 1) * action.spacing * 0.5f;
	const Real halfHeight = (rows - 1) * action.spacing * 0.5f;

	Int made = 0;
	for( Int i = 0; i < action.count; ++i )
	{
		Coord3D pos;
		pos.x = action.at.x + (i % columns) * action.spacing - halfWidth;
		pos.y = action.at.y + (i / columns) * action.spacing - halfHeight;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

		if (spawnOne( tmpl, team, &pos ))
			++made;
	}

	theScenarioUnitsSpawned += made;
	DEBUG_LOG(("SCENARIO: frame %d spawn slot %d '%s' %d of %d at (%.0f,%.0f)\n",
						 action.frame, action.slot, action.selector.str(),
						 made, action.count, action.at.x, action.at.y));

	return made > 0;
}

static Bool executeOrder( const ScenarioAction &action, Player *player )
{
	AIGroup *group = TheAI->createGroup();
	const Int taken = gatherIntoGroup( player, action.selector, group );
	if (taken == 0)
	{
		TheAI->destroyGroup( group );
		DEBUG_LOG(("SCENARIO: frame %d: slot %d owns nothing matching '%s'\n",
							 action.frame, action.slot, action.selector.str()));
		return FALSE;
	}

	Bool ordered = TRUE;
	switch (action.action)
	{
		case SCENARIO_ACTION_MOVE:
		case SCENARIO_ACTION_ATTACKMOVE:
		{
			Coord3D dest;
			dest.x = action.at.x;
			dest.y = action.at.y;
			dest.z = TheTerrainLogic->getGroundHeight( dest.x, dest.y );

			if (action.action == SCENARIO_ACTION_MOVE)
				group->groupMoveToPosition( &dest, FALSE, CMD_FROM_SCRIPT );
			else
				group->groupAttackMoveToPosition( &dest, SCENARIO_ATTACK_SHOTS, CMD_FROM_SCRIPT );

			DEBUG_LOG(("SCENARIO: frame %d %s slot %d '%s' x%d to (%.0f,%.0f)\n",
								 action.frame,
								 (action.action == SCENARIO_ACTION_MOVE) ? "move" : "attackmove",
								 action.slot, action.selector.str(), taken, dest.x, dest.y));
			break;
		}

		case SCENARIO_ACTION_ATTACK:
		{
			Player *targetPlayer = findPlayerForSlot( action.targetSlot );
			Object *victim = (targetPlayer != NULL)
											 ? findFirstMatching( targetPlayer, action.targetSelector )
											 : NULL;
			if (victim == NULL)
			{
				DEBUG_LOG(("SCENARIO: frame %d attack: slot %d owns nothing matching '%s'\n",
									 action.frame, action.targetSlot, action.targetSelector.str()));
				ordered = FALSE;
				break;
			}

			group->groupAttackObject( victim, SCENARIO_ATTACK_SHOTS, CMD_FROM_SCRIPT );
			DEBUG_LOG(("SCENARIO: frame %d attack slot %d '%s' x%d -> slot %d '%s'\n",
								 action.frame, action.slot, action.selector.str(), taken,
								 action.targetSlot, action.targetSelector.str()));
			break;
		}

		case SCENARIO_ACTION_STOP:
		{
			group->groupIdle( CMD_FROM_SCRIPT );
			DEBUG_LOG(("SCENARIO: frame %d stop slot %d '%s' x%d\n",
								 action.frame, action.slot, action.selector.str(), taken));
			break;
		}

		case SCENARIO_ACTION_SPAWN:
			ordered = FALSE;		// handled before the group is built
			break;
	}

	TheAI->destroyGroup( group );
	return ordered;
}

Bool ScenarioDrill_execute( const ScenarioAction &action )
{
	Player *player = findPlayerForSlot( action.slot );
	if (player == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d: nobody is sitting in slot %d\n", action.frame, action.slot));
		return FALSE;
	}

	if (action.action == SCENARIO_ACTION_SPAWN)
		return executeSpawn( action, player );

	return executeOrder( action, player );
}

// ------------------------------------------------------------------------------------------------
// the tick
// ------------------------------------------------------------------------------------------------

void ScenarioDrill_tick( void )
{
	if (TheGlobalData->m_scenarioFile.isEmpty())
		return;

	// the shell map is a running game too, and an army spawned onto the main menu is not the match
	// anybody asked to measure
	if (TheGameLogic->isInShellGame())
		return;

	const UnsignedInt now = TheGameLogic->getFrame();

	// a restart runs the clock back, and the file should play again rather than sit finished
	if (now < theScenarioLastFrame)
		resetScenario();
	theScenarioLastFrame = now;

	if (!theScenarioLoaded)
		loadScenario();

	while (theScenarioCursor < (Int)theScenarioActions.size()
				 && theScenarioActions[ theScenarioCursor ].frame <= now)
	{
		if (ScenarioDrill_execute( theScenarioActions[ theScenarioCursor ] ))
			++theScenarioActionsRun;
		else
			++theScenarioActionsFailed;
		++theScenarioCursor;
	}
}

const char *ScenarioDrill_report( void )
{
	sprintf( theScenarioReport, "%d actions, %d ran, %d refused, %d units spawned",
					 (Int)theScenarioActions.size(), theScenarioActionsRun,
					 theScenarioActionsFailed, theScenarioUnitsSpawned );
	return theScenarioReport;
}
