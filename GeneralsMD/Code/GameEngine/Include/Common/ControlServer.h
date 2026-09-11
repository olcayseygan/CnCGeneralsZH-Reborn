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
// poke the running game and ask it what happened.  Start a skirmish, spawn a worker, send a
// selection somewhere, take a picture, read the frame number and everybody's money back - from
// Python, from a browser console, from anything that speaks WebSocket.
//
// It listens on the loopback address only.  There is no authentication and none is wanted: the
// socket can start a match and create units, so it has to be unreachable from anywhere but this
// machine, and binding to 127.0.0.1 is what makes that true rather than a promise.
//
// Commands are one line of text per frame, and world commands are the same grammar the scenario
// files use, minus the leading frame number:
//
//   spawn <slot> <template> <count> <x> <y> [spacing]
//   move|attackmove <slot> <selector> <x> <y>
//   attack <slot> <selector> <targetSlot> <targetSelector>
//   stop <slot> <selector>
//
// plus a handful the files have no use for: ping, status, screenshot, skirmish, quit,
//
//   influence <player index> threat|cash
//
// which reads one player's per-cell threat or cash map out of the partition manager where it
// arrives, since it writes nothing,
//
//   units <player index>
//
// which lists where that player's own and allied objects are, every enemy it can see right now and
// which cells it sees, read where it arrives for the same reason,
//
//   terrain
//
// which samples the ground once per pathfinder cell as passable, cliff or water, and
//
//   key <KEY_name> [ALT] [CTRL] [SHIFT]
//
// which presses and releases one key through the message stream, the way the keyboard does.  It
// runs where it arrives, on the render pass, because that is where a real key arrives too.
//
// Replies are one JSON object per frame.  Every reply carries "ok", and a failed one carries
// "error" saying what was wrong with the command rather than dropping it.
//
// World commands do not take effect where they arrive.  Reading a socket happens on a render pass
// and creating an object has to happen inside a logic frame, so a command is queued by
// ControlServer_poll and carried out by ControlServer_runCommands on the next logic frame.  That is
// also what keeps the game deterministic while something is driving it.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CONTROLSERVER_H_
#define __CONTROLSERVER_H_

#include "Lib/BaseType.h"

#include <vector>

/** Accept connections, read commands, send replies.  Called once per engine pass. */
extern void ControlServer_poll( void );

/** Carry out the world commands that arrived since the last logic frame.  Called from
	  GameLogic::update, which is the only place it is safe to make an object. */
extern void ControlServer_runCommands( void );

/** Close the socket.  Called when the engine shuts down. */
extern void ControlServer_shutdown( void );

/** The WebSocket handshake reply for a client key: SHA-1 of the key and the protocol's own GUID,
	  base64'd.  Public because it is the one piece of this file that can be tested without a socket,
	  and RFC 6455 ships a worked example to test it against. */
extern Bool ControlServer_computeAcceptKey( const char *clientKey, char *out, Int outSize );

/** The influence reply: one player's value for every partition cell, row by row from cell 0,0,
	  with the grid's size and the frame it was read on.  Public for the accept key's reason: the
	  formatting can be tested without a socket or a map.  out ends in a terminating 0. */
extern void ControlServer_formatInfluence( UnsignedInt frame, Int playerIndex, const char *kind,
																					 Int width, Int height, Real cellSize,
																					 const UnsignedInt *values, std::vector<char> *out );

/** How an object in the units reply stands to the player it was asked for.  The numbers go down the
	  socket as they are, and influence_viewer.py reads them by value. */
enum ControlUnitSide
{
	CONTROL_UNIT_OWN = 0,
	CONTROL_UNIT_ALLY = 1,
	CONTROL_UNIT_ENEMY = 2
};

struct ControlUnit
{
	UnsignedInt id;
	Real x;
	Real y;
	ControlUnitSide side;
	Bool isStructure;
	const char *templateName;
	Int cost;
};

/** What one sample of the ground is in the terrain reply, sent as the character itself. */
enum ControlTerrainKind
{
	CONTROL_TERRAIN_PASSABLE = '0',
	CONTROL_TERRAIN_CLIFF = '1',
	CONTROL_TERRAIN_WATER = '2'
};

/** The terrain reply: the sample grid's size and spacing, and "cells", one ControlTerrainKind
	  character per sample, row by row from 0,0.  out ends in a 0. */
extern void ControlServer_formatTerrain( Int width, Int height, Real cellSize, const std::vector<char> &cells,
																				 std::vector<char> *out );

/** The units reply: the partition grid's size, so a viewer can bin positions into the same cells the
	  influence reply uses, one [id, x, y, side, structure, template, cost] array per object, and "seen", one '1' or '0'
	  per cell row by row for whether the player sees that cell clear right now.  The id and the seen
	  cells are what let a viewer remember an enemy it lost sight of and forget it once the spot is
	  scouted again.  out ends in a 0. */
extern void ControlServer_formatUnits( UnsignedInt frame, Int playerIndex, Int width, Int height,
																			 Real cellSize, const std::vector<ControlUnit> &units,
																			 const std::vector<Bool> &seen, std::vector<char> *out );

#endif // __CONTROLSERVER_H_
