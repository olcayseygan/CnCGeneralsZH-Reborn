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

// FILE: ControlQuery.cpp ////////////////////////////////////////////////////////////////////////
//
// -control's eyes: what the game state is, who is playing, what is on the map, which windows are up
// and what they say, what the command bar offers, and a picture of the screen.  Nothing here changes
// anything, except that a picture has to wait for the next draw to exist.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ControlCommands.h"
#include "Common/Energy.h"
#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Shell.h"
#include "GameClient/WinInstanceData.h"
#include "GameClient/WindowLayout.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ProductionUpdate.h"

static const Int SCREENSHOT_WAIT_PASSES = 600;			///< a draw that has not come in this many passes is not coming
static const Int DEFAULT_OBJECT_LIMIT = 2000;
static const Int MAX_LISTBOX_ROWS = 200;

// ------------------------------------------------------------------------------------------------
// small writers
// ------------------------------------------------------------------------------------------------

static void addPosition( ControlJson &json, const char *name, const Coord3D &position )
{
	json.beginObject( name );
	json.addReal( "x", position.x );
	json.addReal( "y", position.y );
	json.addReal( "z", position.z );
	json.endObject();
}

static void addWindowRect( ControlJson &json, GameWindow *window )
{
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	json.addInt( "x", x );
	json.addInt( "y", y );
	json.addInt( "width", width );
	json.addInt( "height", height );
}

static Int getLocalPlayerIndex( void )
{
	return ThePlayerList->getLocalPlayer()->getPlayerIndex();
}

// ------------------------------------------------------------------------------------------------
// state
// ------------------------------------------------------------------------------------------------

static const char *getGameModeName( void )
{
	if (TheGameLogic->isInShellGame())
		return "shell";
	if (TheGameLogic->isInReplayGame())
		return "replay";
	if (TheGameLogic->isInMultiplayerGame())
		return "multiplayer";
	if (TheGameLogic->isInSkirmishGame())
		return "skirmish";
	if (TheGameLogic->isInGame())
		return "game";
	return "none";
}

static ControlOutcome replyState( ControlCommand &command )
{
	ControlJson &reply = command.reply;

	reply.addString( "mode", getGameModeName() );
	reply.addBool( "inMatch", ControlServer_isMatchRunning() );
	reply.addInt( "frame", (Int)TheGameLogic->getFrame() );
	reply.addBool( "paused", TheGameLogic->isGamePaused() );
	reply.addString( "map", TheGlobalData->m_mapName.str() );
	reply.addInt( "screenWidth", (Int)TheDisplay->getWidth() );
	reply.addInt( "screenHeight", (Int)TheDisplay->getHeight() );

	WindowLayout *screen = TheShell->top();
	reply.addBool( "shellActive", TheShell->isShellActive() );
	reply.addString( "shellScreen", screen ? screen->getFilename().str() : "" );
	// the socket opens while the EA logo is still playing; the menu is pushed after it and animates in
	reply.addBool( "shellReady", screen != NULL && TheShell->isAnimFinished() );

	reply.addInt( "localPlayer", getLocalPlayerIndex() );
	reply.addBool( "inputEnabled", TheInGameUI->getInputEnabled() );

	const CommandButton *armed = TheInGameUI->getGUICommand();
	reply.addString( "armedCommand", armed ? armed->getName().str() : "" );
	// a structure button arms no command: it puts the building on the cursor instead
	const ThingTemplate *placing = TheInGameUI->getPendingPlaceType();
	reply.addString( "placing", placing ? placing->getName().str() : "" );

	reply.beginArray( "selection" );
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( DrawableList::const_iterator it = selected->begin(); it != selected->end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if (obj)
			reply.addInt( NULL, (Int)obj->getID() );
	}
	reply.endArray();

	return CONTROL_DONE;
}

// ------------------------------------------------------------------------------------------------
// players
// ------------------------------------------------------------------------------------------------

static ControlOutcome replyPlayers( ControlCommand &command )
{
	ControlJson &reply = command.reply;
	const Bool inMatch = ControlServer_isMatchRunning();

	reply.beginArray( "players" );
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		Energy *energy = player->getEnergy();

		reply.beginObject();
		reply.addInt( "index", i );
		reply.addInt( "slot", ThePlayerList->getSlotIndex( i ) );
		reply.addWide( "name", player->getPlayerDisplayName().str() );
		reply.addString( "side", player->getSide().str() );
		reply.addBool( "local", player->isLocalPlayer() );
		reply.addBool( "playable", player->isPlayableSide() );
		reply.addBool( "observer", player->isPlayerObserver() );
		reply.addInt( "money", (Int)player->getMoney()->countMoney() );
		reply.addInt( "powerProduced", energy->getProduction() );
		reply.addInt( "powerUsed", energy->getConsumption() );
		reply.addInt( "rank", player->getRankLevel() );
		reply.addInt( "sciencePoints", player->getSciencePurchasePoints() );
		reply.addInt( "skillPoints", player->getSkillPoints() );
		reply.addBool( "dead", player->isPlayerDead() );
		if (inMatch)
		{
			reply.addBool( "won", TheVictoryConditions->hasAchievedVictory( player ) );
			reply.addBool( "defeated", TheVictoryConditions->hasSinglePlayerBeenDefeated( player ) );
		}
		reply.endObject();
	}
	reply.endArray();

	return CONTROL_DONE;
}

// ------------------------------------------------------------------------------------------------
// objects
// ------------------------------------------------------------------------------------------------

enum ObjectScope
{
	SCOPE_MINE = 0,
	SCOPE_VISIBLE,		///< what the local player can see, their own things included
	SCOPE_ALL					///< everything, shroud or not: for tests, not for playing
};

struct ObjectFilter
{
	ObjectScope scope;
	Bool isNearOnly;
	Real nearX;
	Real nearY;
	Real nearRadius;
	std::string templateName;
	std::string kind;
	Int onlyId;
	Int limit;
};

static const char *getKindName( const Object *obj )
{
	if (obj->isKindOf( KINDOF_STRUCTURE ))
		return "structure";
	if (obj->isKindOf( KINDOF_DOZER ))
		return "dozer";
	if (obj->isKindOf( KINDOF_HARVESTER ))
		return "harvester";
	if (obj->isKindOf( KINDOF_INFANTRY ))
		return "infantry";
	if (obj->isKindOf( KINDOF_AIRCRAFT ))
		return "aircraft";
	if (obj->isKindOf( KINDOF_VEHICLE ))
		return "vehicle";
	return "other";
}

/* objects [mine|visible|all] [near <x> <y> <radius>] [template <name>] [kind <kind>] [id <id>] [limit <n>] */
static Bool parseObjectFilter( const ControlCommand &command, ObjectFilter *filter, std::string *error )
{
	filter->scope = SCOPE_VISIBLE;
	filter->isNearOnly = FALSE;
	filter->onlyId = 0;
	filter->limit = DEFAULT_OBJECT_LIMIT;

	for( size_t i = 1; i < command.words.size(); ++i )
	{
		const std::string &word = command.words[ i ];
		if (word == "mine")
			filter->scope = SCOPE_MINE;
		else if (word == "visible")
			filter->scope = SCOPE_VISIBLE;
		else if (word == "all")
			filter->scope = SCOPE_ALL;
		else if (word == "near")
		{
			if (!ControlCommand_getReal( command, i + 1, &filter->nearX )
					|| !ControlCommand_getReal( command, i + 2, &filter->nearY )
					|| !ControlCommand_getReal( command, i + 3, &filter->nearRadius ))
			{
				*error = "near wants <x> <y> <radius>";
				return FALSE;
			}
			filter->isNearOnly = TRUE;
			i += 3;
		}
		else if (word == "template" && i + 1 < command.words.size())
			filter->templateName = command.words[ ++i ];
		else if (word == "kind" && i + 1 < command.words.size())
			filter->kind = command.words[ ++i ];
		else if (word == "id" && ControlCommand_getInt( command, i + 1, &filter->onlyId ))
			++i;
		else if (word == "limit" && ControlCommand_getInt( command, i + 1, &filter->limit ))
			++i;
		else
		{
			*error = "objects takes mine|visible|all, near <x> <y> <r>, template <name>, kind <structure|dozer|harvester|infantry|aircraft|vehicle|other>, id <id>, limit <n>; not '" + word + "'";
			return FALSE;
		}
	}
	return TRUE;
}

static Bool isObjectWanted( Object *obj, const ObjectFilter &filter, Player *local )
{
	if (filter.onlyId != 0)
		return (Int)obj->getID() == filter.onlyId;

	const Bool isMine = obj->getControllingPlayer() == local;
	if (filter.scope == SCOPE_MINE && !isMine)
		return FALSE;
	if (filter.scope == SCOPE_VISIBLE && !isMine)
	{
		const ObjectShroudStatus shroud = obj->getShroudedStatus( local->getPlayerIndex() );
		if (shroud != OBJECTSHROUD_CLEAR && shroud != OBJECTSHROUD_PARTIAL_CLEAR)
			return FALSE;
	}

	if (filter.isNearOnly)
	{
		const Real dx = obj->getPosition()->x - filter.nearX;
		const Real dy = obj->getPosition()->y - filter.nearY;
		if (dx*dx + dy*dy > filter.nearRadius * filter.nearRadius)
			return FALSE;
	}
	if (!filter.templateName.empty() && filter.templateName != obj->getTemplate()->getName().str())
		return FALSE;
	if (!filter.kind.empty() && filter.kind != getKindName( obj ))
		return FALSE;

	return TRUE;
}

static void writeObject( ControlJson &reply, Object *obj, Int localIndex )
{
	reply.beginObject();
	reply.addInt( "id", (Int)obj->getID() );
	reply.addString( "template", obj->getTemplate()->getName().str() );
	reply.addString( "kind", getKindName( obj ) );
	reply.addInt( "owner", obj->getControllingPlayer()->getPlayerIndex() );
	addPosition( reply, "position", *obj->getPosition() );
	reply.addReal( "facing", obj->getOrientation() );

	BodyModuleInterface *body = obj->getBodyModule();
	if (body)
	{
		reply.addReal( "health", body->getHealth() );
		reply.addReal( "maxHealth", body->getMaxHealth() );
	}
	reply.addBool( "dead", obj->isEffectivelyDead() );
	if (obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ))
		reply.addReal( "constructionPercent", obj->getConstructionPercent() );

	const Drawable *draw = obj->getDrawable();
	reply.addBool( "selected", draw != NULL && draw->isSelected() );
	reply.addInt( "shroud", (Int)obj->getShroudedStatus( localIndex ) );

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai)
	{
		reply.beginObject( "ai" );
		reply.addInt( "state", (Int)ai->getCurrentStateID() );
		reply.addBool( "moving", ai->isMoving() );
		reply.addBool( "idle", ai->isIdle() );
		addPosition( reply, "goal", *ai->getGoalPosition() );
		const Object *goalObject = ai->getGoalObject();
		reply.addInt( "goalObject", goalObject ? (Int)goalObject->getID() : 0 );
		Path *path = ai->getPath();
		if (path)
			addPosition( reply, "pathEnd", *path->getLastNode()->getPosition() );
		reply.endObject();
	}

	ProductionUpdateInterface *production = obj->getProductionUpdateInterface();
	if (production)
	{
		reply.beginArray( "production" );
		for( const ProductionEntry *entry = production->firstProduction(); entry; entry = production->nextProduction( entry ) )
		{
			reply.beginObject();
			if (entry->getProductionObject())
				reply.addString( "name", entry->getProductionObject()->getName().str() );
			else if (entry->getProductionUpgrade())
				reply.addString( "name", entry->getProductionUpgrade()->getUpgradeName().str() );
			reply.addReal( "percent", entry->getPercentComplete() );
			reply.endObject();
		}
		reply.endArray();
	}

	reply.endObject();
}

static ControlOutcome replyObjects( ControlCommand &command )
{
	ObjectFilter filter;
	std::string error;
	if (!parseObjectFilter( command, &filter, &error ))
		return ControlCommand_fail( command, error );
	if (!ControlServer_isMatchRunning())
		return ControlCommand_fail( command, "no match is running" );

	Player *local = ThePlayerList->getLocalPlayer();
	ControlJson &reply = command.reply;
	Int written = 0;
	Bool isTruncated = FALSE;

	reply.beginArray( "objects" );
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (!isObjectWanted( obj, filter, local ))
			continue;
		if (written >= filter.limit)
		{
			isTruncated = TRUE;
			break;
		}
		writeObject( reply, obj, local->getPlayerIndex() );
		++written;
	}
	reply.endArray();
	reply.addInt( "count", written );
	reply.addBool( "truncated", isTruncated );

	return CONTROL_DONE;
}

// ------------------------------------------------------------------------------------------------
// windows
// ------------------------------------------------------------------------------------------------

static const char *getWindowTypeName( GameWindow *window )
{
	const UnsignedInt style = window->winGetStyle();
	if (BitTest( style, GWS_PUSH_BUTTON ))
		return "button";
	if (BitTest( style, GWS_RADIO_BUTTON ))
		return "radio";
	if (BitTest( style, GWS_CHECK_BOX ))
		return "checkbox";
	if (BitTest( style, GWS_ALL_SLIDER ))
		return "slider";
	if (BitTest( style, GWS_SCROLL_LISTBOX ))
		return "listbox";
	if (BitTest( style, GWS_COMBO_BOX ))
		return "combobox";
	if (BitTest( style, GWS_ENTRY_FIELD ))
		return "entry";
	if (BitTest( style, GWS_STATIC_TEXT ))
		return "text";
	if (BitTest( style, GWS_PROGRESS_BAR ))
		return "progress";
	if (BitTest( style, GWS_TAB_CONTROL ))
		return "tabs";
	return "window";
}

static void writeWindow( ControlJson &json, GameWindow *window, Bool isHiddenIncluded )
{
	const UnsignedInt style = window->winGetStyle();

	json.beginObject();
	json.addString( "name", window->winGetInstanceData()->m_decoratedNameString.str() );
	json.addString( "type", getWindowTypeName( window ) );
	addWindowRect( json, window );
	json.addBool( "hidden", window->winIsHidden() );
	json.addBool( "enabled", window->winGetEnabled() );
	json.addWide( "text", window->winGetText().str() );

	if (BitTest( style, GWS_CHECK_BOX ))
		json.addBool( "checked", GadgetCheckBoxIsChecked( window ) );
	else if (BitTest( style, GWS_ALL_SLIDER ))
		json.addInt( "position", GadgetSliderGetPosition( window ) );
	else if (BitTest( style, GWS_COMBO_BOX ))
		json.addWide( "value", GadgetComboBoxGetText( window ).str() );
	else if (BitTest( style, GWS_ENTRY_FIELD ))
		json.addWide( "value", GadgetTextEntryGetText( window ).str() );
	else if (BitTest( style, GWS_SCROLL_LISTBOX ))
	{
		const Int rows = GadgetListBoxGetNumEntries( window );
		json.addInt( "rowCount", rows );
		json.beginArray( "rows" );
		for( Int row = 0; row < rows && row < MAX_LISTBOX_ROWS; ++row )
			json.addWide( NULL, GadgetListBoxGetText( window, row ).str() );
		json.endArray();
	}

	json.beginArray( "children" );
	for( GameWindow *child = window->winGetChild(); child; child = child->winGetNext() )
	{
		if (isHiddenIncluded || !child->winIsHidden())
			writeWindow( json, child, isHiddenIncluded );
	}
	json.endArray();

	json.endObject();
}

static ControlOutcome replyWindows( ControlCommand &command )
{
	const Bool isHiddenIncluded = strcmp( ControlCommand_word( command, 1 ), "all" ) == 0;

	command.reply.beginArray( "windows" );
	for( GameWindow *window = TheWindowManager->winGetWindowList(); window; window = window->winGetNext() )
	{
		if (isHiddenIncluded || !window->winIsHidden())
			writeWindow( command.reply, window, isHiddenIncluded );
	}
	command.reply.endArray();

	return CONTROL_DONE;
}

static GameWindow *findWindowIn( GameWindow *first, const char *name, Bool isHiddenAccepted )
{
	for( GameWindow *window = first; window; window = window->winGetNext() )
	{
		if (!isHiddenAccepted && window->winIsHidden())
			continue;
		if (strcmp( window->winGetInstanceData()->m_decoratedNameString.str(), name ) == 0)
			return window;
		GameWindow *found = findWindowIn( window->winGetChild(), name, isHiddenAccepted );
		if (found)
			return found;
	}
	return NULL;
}

GameWindow *ControlQuery_findWindow( const char *name )
{
	// a layout loaded twice leaves a hidden copy behind, so the one on screen wins
	GameWindow *visible = findWindowIn( TheWindowManager->winGetWindowList(), name, FALSE );
	return visible ? visible : findWindowIn( TheWindowManager->winGetWindowList(), name, TRUE );
}

// ------------------------------------------------------------------------------------------------
// the command bar and the message log
// ------------------------------------------------------------------------------------------------

static ControlOutcome replyButtons( ControlCommand &command )
{
	ControlJson &reply = command.reply;

	reply.beginArray( "buttons" );
	for( Int index = 0; index < MAX_COMMANDS_PER_SET; ++index )
	{
		GameWindow *window = TheControlBar->getVisibleCommandWindow( index );
		if (window == NULL)
			continue;
		const CommandButton *button = (const CommandButton *)GadgetButtonGetData( window );
		if (button == NULL)
			continue;

		reply.beginObject();
		reply.addInt( "index", index );
		reply.addString( "name", button->getName().str() );
		reply.addWide( "label", TheGameText->fetch( button->getTextLabel() ).str() );
		reply.addInt( "commandType", (Int)button->getCommandType() );
		if (button->getThingTemplate())
			reply.addString( "builds", button->getThingTemplate()->getName().str() );
		reply.addBool( "enabled", BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) );
		addWindowRect( reply, window );
		reply.endObject();
	}
	reply.endArray();

	return CONTROL_DONE;
}

static ControlOutcome replyMessages( ControlCommand &command )
{
	command.reply.beginArray( "messages" );
	for( Int i = 0; i < TheInGameUI->getUIMessageCount(); ++i )
	{
		const UnicodeString &text = TheInGameUI->getUIMessageText( i );
		if (!text.isEmpty())
			command.reply.addWide( NULL, text.str() );
	}
	command.reply.endArray();

	return CONTROL_DONE;
}

// ------------------------------------------------------------------------------------------------
// screenshot: the file is written at the end of the next draw, so the reply waits for the path
// ------------------------------------------------------------------------------------------------

static Bool theScreenShotIsPending = FALSE;
static AsciiString thePathBeforeScreenShot;
static Int theScreenShotPasses = 0;

static ControlOutcome requestScreenShot( ControlCommand &command )
{
	if (TheGlobalData->m_headless)
		return ControlCommand_fail( command, "a -headless run draws nothing, so there is nothing to photograph" );

	thePathBeforeScreenShot = TheDisplay->getLastScreenShotPath();
	theScreenShotPasses = 0;
	theScreenShotIsPending = TRUE;
	TheDisplay->takeScreenShot();
	return CONTROL_LATER;
}

void ControlQuery_tick( void )
{
	if (!theScreenShotIsPending)
		return;

	const AsciiString path = TheDisplay->getLastScreenShotPath();
	if (strcmp( path.str(), thePathBeforeScreenShot.str() ) != 0)
	{
		theScreenShotIsPending = FALSE;
		ControlServer_current().reply.addString( "path", path.str() );
		ControlServer_finish( CONTROL_DONE );
		return;
	}

	if (++theScreenShotPasses > SCREENSHOT_WAIT_PASSES)
	{
		theScreenShotIsPending = FALSE;
		ControlServer_current().error = "no draw wrote the picture; a minimised window or a lost device draws nothing";
		ControlServer_finish( CONTROL_FAILED );
	}
}

ControlOutcome ControlQuery_handle( ControlCommand &command )
{
	const std::string &verb = command.words[ 0 ];

	if (verb == "state")
		return replyState( command );
	if (verb == "players")
		return replyPlayers( command );
	if (verb == "objects")
		return replyObjects( command );
	if (verb == "windows")
		return replyWindows( command );
	if (verb == "buttons")
		return replyButtons( command );
	if (verb == "messages")
		return replyMessages( command );
	if (verb == "screenshot")
		return requestScreenShot( command );

	return CONTROL_NOT_MINE;
}
