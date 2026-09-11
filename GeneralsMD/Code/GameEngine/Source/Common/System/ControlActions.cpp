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

// FILE: ControlActions.cpp //////////////////////////////////////////////////////////////////////
//
// -control's orders, given as the local player without aiming the mouse.  Each one is the message
// the translators would have sent for the same click, appended to TheMessageStream: stamped with the
// local player, recorded in the replay, sent to the other machines of a network game, and carried out
// by GameLogicDispatch with CMD_FROM_PLAYER on the selection logic holds.  Selecting sends the
// selection message too, because logic knows nothing of what the client has highlighted.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ControlCommands.h"
#include "Common/GameCommon.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameWindow.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

struct NamedOrder
{
	const char *name;
	GameMessage::Type type;
};

/// orders aimed at a point on the ground
static const NamedOrder LOCATION_ORDERS[] =
{
	{ "move",					GameMessage::MSG_DO_MOVETO },
	{ "forcemove",		GameMessage::MSG_DO_FORCEMOVETO },
	{ "attackmove",		GameMessage::MSG_DO_ATTACKMOVETO },
	{ "waypoint",			GameMessage::MSG_ADD_WAYPOINT },
	{ "guard",				GameMessage::MSG_DO_GUARD_POSITION },
	{ "attackground",	GameMessage::MSG_DO_FORCE_ATTACK_GROUND },
};

/// orders aimed at one object
static const NamedOrder TARGET_ORDERS[] =
{
	{ "attack",				GameMessage::MSG_DO_ATTACK_OBJECT },
	{ "forceattack",	GameMessage::MSG_DO_FORCE_ATTACK_OBJECT },
	{ "enter",				GameMessage::MSG_ENTER },
	{ "dock",					GameMessage::MSG_DOCK },
	{ "repair",				GameMessage::MSG_DO_REPAIR },
	{ "getrepaired",	GameMessage::MSG_GET_REPAIRED },
	{ "gethealed",		GameMessage::MSG_GET_HEALED },
};

/// orders with nothing to aim at
static const NamedOrder PLAIN_ORDERS[] =
{
	{ "stop",					GameMessage::MSG_DO_STOP },
	{ "scatter",			GameMessage::MSG_DO_SCATTER },
	{ "sell",					GameMessage::MSG_SELL },
	{ "evacuate",			GameMessage::MSG_EVACUATE },
	{ "hold",					GameMessage::MSG_DO_HOLD_POSITION },
};

static const NamedOrder *findOrder( const NamedOrder *orders, Int count, const char *name )
{
	for( Int i = 0; i < count; ++i )
	{
		if (strcmp( orders[ i ].name, name ) == 0)
			return &orders[ i ];
	}
	return NULL;
}

#define ORDER_COUNT( table ) ( (Int)( sizeof( table ) / sizeof( table[ 0 ] ) ) )

static std::string describeId( const char *what, Int id )
{
	char text[ 128 ];
	sprintf( text, "%s %d", what, id );
	return text;
}

/** A live object the local player owns, or NULL with the reason in error. */
static Object *findOwnObject( Int id, std::string *error )
{
	Object *obj = TheGameLogic->findObjectByID( (ObjectID)id );
	if (obj == NULL || obj->isEffectivelyDead())
	{
		*error = describeId( "no live object with id", id );
		return NULL;
	}
	if (obj->getControllingPlayer() != ThePlayerList->getLocalPlayer())
	{
		*error = describeId( "the local player does not own object", id );
		return NULL;
	}
	return obj;
}

static Coord3D getGroundPoint( Real x, Real y )
{
	Coord3D point;
	point.x = x;
	point.y = y;
	point.z = TheTerrainLogic->getGroundHeight( x, y );
	return point;
}

/* select <id...> | select add <id...> | select none */
static ControlOutcome handleSelect( ControlCommand &command )
{
	if (!ControlServer_isMatchRunning())
		return ControlCommand_fail( command, "no match is running" );

	const std::string first = ControlCommand_word( command, 1 );
	if (first == "none")
	{
		TheInGameUI->deselectAllDrawables();
		command.reply.addInt( "selected", 0 );
		return CONTROL_DONE;
	}

	const Bool isAdding = first == "add";
	std::vector<Object *> chosen;
	for( size_t i = isAdding ? 2 : 1; i < command.words.size(); ++i )
	{
		Int id = 0;
		if (!ControlCommand_getInt( command, i, &id ))
			return ControlCommand_fail( command, "select wants object ids, 'add' and ids, or 'none'" );

		std::string error;
		Object *obj = findOwnObject( id, &error );
		if (obj == NULL)
			return ControlCommand_fail( command, error );
		if (obj->getDrawable() == NULL)
			return ControlCommand_fail( command, describeId( "nothing is drawn for object", id ) );
		chosen.push_back( obj );
	}
	if (chosen.empty())
		return ControlCommand_fail( command, "select wants at least one object id" );

	if (!isAdding)
		TheInGameUI->deselectAllDrawables();

	// the same message SelectionXlat sends for a box or a click, without the voice
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND );
	msg->appendBooleanArgument( !isAdding );
	for( size_t i = 0; i < chosen.size(); ++i )
	{
		TheInGameUI->selectDrawable( chosen[ i ]->getDrawable() );
		msg->appendObjectIDArgument( chosen[ i ]->getID() );
	}

	command.reply.addInt( "selected", TheInGameUI->getSelectCount() );
	return CONTROL_DONE;
}

/* order move|forcemove|attackmove|waypoint|guard|attackground <x> <y>
	 order attack|forceattack|enter|dock|repair|getrepaired|gethealed <id>
	 order stop|scatter|sell|evacuate|hold
	 order rally <producerId> <x> <y> */
static ControlOutcome handleOrder( ControlCommand &command )
{
	if (!ControlServer_isMatchRunning())
		return ControlCommand_fail( command, "no match is running" );

	const char *name = ControlCommand_word( command, 1 );

	if (strcmp( name, "rally" ) == 0)
	{
		Int producerId = 0;
		Real x = 0.0f;
		Real y = 0.0f;
		if (!ControlCommand_getInt( command, 2, &producerId )
				|| !ControlCommand_getReal( command, 3, &x ) || !ControlCommand_getReal( command, 4, &y ))
			return ControlCommand_fail( command, "order rally wants <producerId> <x> <y>" );

		std::string error;
		Object *producer = findOwnObject( producerId, &error );
		if (producer == NULL)
			return ControlCommand_fail( command, error );

		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_SET_RALLY_POINT );
		msg->appendObjectIDArgument( producer->getID() );
		msg->appendLocationArgument( getGroundPoint( x, y ) );
		command.reply.addString( "sent", name );
		return CONTROL_DONE;
	}

	if (TheInGameUI->getSelectCount() == 0)
		return ControlCommand_fail( command, "nothing is selected; an order goes to the selection" );

	const NamedOrder *order = findOrder( LOCATION_ORDERS, ORDER_COUNT( LOCATION_ORDERS ), name );
	if (order)
	{
		Real x = 0.0f;
		Real y = 0.0f;
		if (!ControlCommand_getReal( command, 2, &x ) || !ControlCommand_getReal( command, 3, &y ))
			return ControlCommand_fail( command, std::string( "order " ) + name + " wants <x> <y>" );

		GameMessage *msg = TheMessageStream->appendMessage( order->type );
		msg->appendLocationArgument( getGroundPoint( x, y ) );
		if (order->type == GameMessage::MSG_DO_ATTACKMOVETO)
			msg->appendBooleanArgument( FALSE );		// each unit at its own pace, as a click without ctrl
		if (order->type == GameMessage::MSG_DO_GUARD_POSITION)
			msg->appendIntegerArgument( GUARDMODE_GUARD_WITHOUT_PURSUIT );
		command.reply.addString( "sent", name );
		return CONTROL_DONE;
	}

	order = findOrder( TARGET_ORDERS, ORDER_COUNT( TARGET_ORDERS ), name );
	if (order)
	{
		Int targetId = 0;
		if (!ControlCommand_getInt( command, 2, &targetId ))
			return ControlCommand_fail( command, std::string( "order " ) + name + " wants <targetId>" );

		const Object *target = TheGameLogic->findObjectByID( (ObjectID)targetId );
		if (target == NULL)
			return ControlCommand_fail( command, describeId( "no object with id", targetId ) );

		GameMessage *msg = TheMessageStream->appendMessage( order->type );
		if (order->type == GameMessage::MSG_ENTER)
			msg->appendObjectIDArgument( INVALID_ID );	// the enterer is the selection, the second argument the thing entered
		msg->appendObjectIDArgument( target->getID() );
		command.reply.addString( "sent", name );
		return CONTROL_DONE;
	}

	order = findOrder( PLAIN_ORDERS, ORDER_COUNT( PLAIN_ORDERS ), name );
	if (order)
	{
		GameMessage *msg = TheMessageStream->appendMessage( order->type );
		if (order->type == GameMessage::MSG_DO_HOLD_POSITION)
			msg->appendIntegerArgument( GUARDMODE_GUARD_WITHOUT_PURSUIT );
		command.reply.addString( "sent", name );
		return CONTROL_DONE;
	}

	return ControlCommand_fail( command, std::string( "no order called '" ) + name + "'" );
}

/* button <index>
	 Clicks a command bar slot the way the mouse does.  A button that wants a target arms the command,
	 and a worldclick puts it down. */
static ControlOutcome handleButton( ControlCommand &command )
{
	Int index = 0;
	if (!ControlCommand_getInt( command, 1, &index ) || index < 0 || index >= MAX_COMMANDS_PER_SET)
		return ControlCommand_fail( command, "button wants a slot index from the buttons command" );

	GameWindow *window = TheControlBar->getVisibleCommandWindow( index );
	if (window == NULL)
		return ControlCommand_fail( command, describeId( "nothing is showing in command slot", index ) );
	if (!BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ))
		return ControlCommand_fail( command, describeId( "the button is disabled in command slot", index ) );

	TheControlBar->clickCommandButton( index );

	const CommandButton *armed = TheInGameUI->getGUICommand();
	command.reply.addString( "armedCommand", armed ? armed->getName().str() : "" );
	// a structure button arms no command: it puts the building on the cursor instead
	const ThingTemplate *placing = TheInGameUI->getPendingPlaceType();
	command.reply.addString( "placing", placing ? placing->getName().str() : "" );
	return CONTROL_DONE;
}

ControlOutcome ControlActions_handle( ControlCommand &command )
{
	const std::string &verb = command.words[ 0 ];

	if (verb == "select")
		return handleSelect( command );
	if (verb == "order")
		return handleOrder( command );
	if (verb == "button")
		return handleButton( command );

	return CONTROL_NOT_MINE;
}
