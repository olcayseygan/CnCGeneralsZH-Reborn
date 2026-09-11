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

// FILE: ControlServer.h /////////////////////////////////////////////////////////////////////////
//
// -control [port]: a WebSocket on 127.0.0.1 that drives the game from outside it.
//
// The scenario file answers "play this match the same way twice".  This answers the other half:
// sit in the player's chair from another program.  Read the world and the menus, move the mouse,
// press keys, select units and give them orders, take a picture - from Python, from the MCP server
// in Tools/game_mcp, from anything that speaks WebSocket.
//
// It listens on the loopback address only.  There is no authentication and none is wanted: the
// socket can start a match and create units, so it has to be unreachable from anywhere but this
// machine, and binding to 127.0.0.1 is what makes that true rather than a promise.
//
// One text message is one command, and every command gets exactly one reply, in order: a JSON
// object carrying "ok", and "error" when ok is false.  A command that takes time - a click is
// several engine passes, a picture waits for a draw, a world command waits for a logic frame -
// holds its reply until it has finished, and the commands behind it wait their turn.
//
// The grammar lives in .claude/rules/commandline.md under -control.  The files:
//   ControlServer.cpp   the socket, framing, ping status quit skirmish, and the scenario commands
//                       (spawn move attackmove attack stop), which run inside a logic frame
//   ControlQuery.cpp    state players objects windows buttons messages screenshot
//   ControlInput.cpp    mouse key worldclick worlddrag window camera toscreen toworld wait
//   ControlActions.cpp  select order button - player messages, the way the translators send them
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CONTROLSERVER_H_
#define __CONTROLSERVER_H_

#include "Lib/BaseType.h"

#include <string>
#include <vector>

/// the largest command a client may send; a picture comes back as a path, so nothing needs more
enum { CONTROL_MAX_MESSAGE_BYTES = 1048576 };

/** Accept connections, read commands, send replies.  Called once per engine pass. */
extern void ControlServer_poll( void );

/** Carry out what has to happen inside a logic frame.  Called from GameLogic::update, which is the
	  only place it is safe to make an object. */
extern void ControlServer_runCommands( void );

/** Close the socket.  Called when the engine shuts down. */
extern void ControlServer_shutdown( void );

/** The WebSocket handshake reply for a client key: SHA-1 of the key and the protocol's own GUID,
	  base64'd.  Public because it can be tested without a socket, and RFC 6455 ships a worked
	  example to test it against. */
extern Bool ControlServer_computeAcceptKey( const char *clientKey, char *out, Int outSize );

/** One WebSocket frame as it came off the wire, with the client's mask already taken off. */
struct ControlFrame
{
	unsigned char opcode;
	Bool isFinal;
	std::string payload;
};

/** Read one frame off the front of a buffer.  Returns the bytes it used, 0 while the buffer does not
	  hold a whole frame yet, and -1 when it never will: a length past CONTROL_MAX_MESSAGE_BYTES is a
	  broken client, not a command still arriving. */
extern Int ControlServer_parseFrame( const char *data, Int length, ControlFrame *frame );

/** Split a command line on spaces and tabs. */
extern void ControlServer_splitWords( const char *line, std::vector<std::string> *words );

/** Append text to a JSON document as a quoted string.  Anything from 0x80 up is escaped too: the
	  game's 8-bit text is not UTF-8, and a text frame that is not UTF-8 is refused by the client. */
extern void ControlServer_appendJsonString( const char *text, std::string *json );

#endif // __CONTROLSERVER_H_
