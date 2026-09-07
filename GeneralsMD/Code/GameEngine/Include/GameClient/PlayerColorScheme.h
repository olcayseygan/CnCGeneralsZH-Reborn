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

#pragma once

// PlayerColorScheme.h
//
// Who gets which colour on this screen.
//
// Retail hands every player the colour their lobby slot picked and draws it everywhere: the model's
// house tint, the radar blip, the health bar, the selection ring, the score screen.  Eight of them
// at once, chosen for being distinguishable from each other and not for saying whose side anything
// is on, so telling friend from foe in a four-player fight is a memory exercise done at speed.
//
// So the colour a player is drawn in becomes a client setting.  This is a translation layer and
// nothing more: Player::getPlayerColor() keeps returning the colour the lobby agreed on, the
// simulation keeps reading that one, and only the code about to put pixels on the screen asks here.
//
// That distinction is not cosmetic.  OCLUpdate asks whether a building changed hands by comparing
// the owner's colour against the one it stored last frame (OCLUpdate.cpp, m_currentPlayerColor,
// which is xfered), so a scheme that paints two enemies the same red would make a capture invisible
// to it and spawn units on different frames on different machines.  Nothing in here is ever
// reachable from GameLogic, which is why that cannot happen.  Two people in the same match may run
// different schemes and neither will know.
//
// The table is rebuilt at most once a frame and only when something it depends on moved - the
// setting, the local player, the roster, an alliance.  With PLAYER_COLORS_ORIGINAL every entry is
// the identity and every call here returns before touching it.

#ifndef __PLAYERCOLORSCHEME_H_
#define __PLAYERCOLORSCHEME_H_

#include "Lib/BaseType.h"
#include "GameClient/Color.h"

class Player;

//-----------------------------------------------------------------------------
/** What the player asked for in the options screen. */
enum PlayerColorSchemeType
{
	PLAYER_COLORS_ORIGINAL	= 0,	///< the lobby's own colours, which is what retail draws
	PLAYER_COLORS_RELATION	= 1,	///< you blue, your allies green, everyone shooting at you red
	PLAYER_COLORS_TEAM			= 2,	///< a hue per alliance, a shade per member of it

	PLAYER_COLOR_SCHEME_COUNT = 3,
};

//-----------------------------------------------------------------------------
// The colour families.  Each is four shades that read as the same colour across a battlefield and
// still tell four units apart at arm's length, which is the whole job: a flat red for every enemy
// answers "whose is that" and destroys "which of them is that".
enum
{
	PLAYER_COLOR_FAMILY_BLUE	= 0,
	PLAYER_COLOR_FAMILY_RED		= 1,
	PLAYER_COLOR_FAMILY_GREEN	= 2,
	PLAYER_COLOR_FAMILY_YELLOW	= 3,
	PLAYER_COLOR_FAMILY_PURPLE	= 4,
	PLAYER_COLOR_FAMILY_ORANGE	= 5,
	PLAYER_COLOR_FAMILY_CYAN	= 6,
	PLAYER_COLOR_FAMILY_GREY	= 7,

	PLAYER_COLOR_FAMILY_COUNT	= 8,
	PLAYER_COLOR_SHADE_COUNT	= 4,
};

/** One shade of one family, opaque.  A shade past the fourth is the same four darkened again, so a
	* seven-player alliance still gets seven telltale colours instead of running out at four. */
extern Color playerSchemeColor( Int family, Int shade );

/** The night version of a scheme colour.  The shipped palette carries a hand-tuned night colour per
	* slot because dark blue on a night map is a black unit on black ground; ours are generated, so
	* they get the same treatment by rule - a quarter of the way to white. */
extern Color playerSchemeNightColor( Color dayColor );

//-----------------------------------------------------------------------------
/** The colour to draw this player in, day and night. */
extern Color clientPlayerColor( const Player *player );
extern Color clientPlayerNightColor( const Player *player );

/** The colour to draw, for code that was handed a colour rather than a player.  A colour that is
	* not any player's passes through untouched, which is what makes it safe to put this in front of
	* the floating text and the indicator colour, where civilians and script colours also arrive. */
extern Color clientColor( Color logicColor );

/** Rebuild if anything moved.  TRUE when a colour actually changed, which is the client's cue that
	* every model is carrying a stale tint baked into its render object. */
extern Bool updatePlayerColorScheme( void );

/** Throw the table away; the next query rebuilds it.  Called when a game starts. */
extern void invalidatePlayerColorScheme( void );

#endif // __PLAYERCOLORSCHEME_H_
