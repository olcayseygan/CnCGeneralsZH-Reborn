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

// OptionsCatalog.h
//
// One table describing every scalar setting the player can change.
//
// EA spread a setting over five files: a GlobalData field, a default in GlobalData's constructor,
// a hand-written OptionPreferences::getXxx that parses one Options.ini key, a copy line in
// GameLODManager::setOptionPreferences, a widget in OptionsMenu.wnd, and three more places in
// OptionsMenu.cpp - the NameKey, the code that fills the widget on the way in and the code that
// reads it back on the way out.  Ten edits for one checkbox, and every one of them a chance to
// forget the fourth.  The FPSLimit key is what forgetting looks like: OptionsMenu.cpp still
// carries the commented-out write, so the checkbox exists and nothing persists.
//
// So the description of a setting lives here instead, one row per setting, and the four passes
// that used to be hand-written per setting are loops over this table: read Options.ini, write
// Options.ini, fill the menu, read the menu back.  Adding a setting is a row, a GlobalData field
// and whatever code actually reads that field.
//
// What is deliberately NOT here: resolution, the detail preset, the LAN and online IP combo boxes
// and the firewall entries.  None of them is a scalar with a range - picking a resolution rebuilds
// the shell, the detail preset writes eleven other settings - and forcing them through a generic
// table would mean a table row carrying a function that does something entirely unlike the others.
// They keep their bespoke code in OptionsMenu.cpp.

#ifndef __OPTIONSCATALOG_H_
#define __OPTIONSCATALOG_H_

#include "Lib/BaseType.h"

// UserPreferences, not OptionPreferences: the catalog only ever does map lookups, and taking the
// base class keeps it clear of OptionsMenu.cpp - which is where OptionPreferences' constructor
// lives, and which drags in the shell, the audio manager and GameSpy behind it.
class UserPreferences;

//-----------------------------------------------------------------------------
/** How a setting is stored in Options.ini and what kind of control shows it. */
enum OptionKind
{
	OPTION_BOOL,		///< "yes" / "no" in Options.ini, a check box in the menu
	OPTION_INT,			///< a decimal clamped to [lo,hi], a slider in the menu
	OPTION_ENUM,		///< a decimal in [0,hi], a combo box whose entries are labelKey with 0..hi appended
};

//-----------------------------------------------------------------------------
/** When a changed value actually reaches the player.
	*
	* The menu shows this: a setting the device has to be rebuilt for cannot pretend it took effect
	* the moment Accept was pressed, and one that is only read while the process starts up has to say
	* so or the player changes it, sees nothing happen and changes it back. */
enum OptionApply
{
	APPLY_LIVE,					///< whoever reads the GlobalData field reads it every frame; nothing else to do
	APPLY_DEVICE_RESET,	///< the D3D device has to be recreated before it shows
	APPLY_RESTART,			///< only read once, before the engine exists - see EarlyOptions.h
};

//-----------------------------------------------------------------------------
/** One setting.
	*
	* `get` and `set` are the whole coupling to GlobalData.  A member pointer would be tidier but the
	* fields are a mix of Bool, Int and enum, and two one-line functions cost less than the cast that
	* would take. */
struct OptionDef
{
	const char*	iniKey;			///< the Options.ini key, and the name the tests match on
	const char*	widgetName;	///< "OptionsMenu.wnd:CheckXxx", or "" while the setting has no control
	const char*	labelKey;		///< CSF label; OPTION_ENUM appends "0".."N" to it for the combo entries
	OptionKind	kind;
	OptionApply	apply;
	Int					lo;					///< inclusive low bound; 0 for BOOL and ENUM
	Int					hi;					///< inclusive high bound; 1 for BOOL, the last index for ENUM
	Int					(*get)( void );				///< the live value out of TheGlobalData
	void				(*set)( Int value );	///< write it back into TheWritableGlobalData
};

//-----------------------------------------------------------------------------
// The table itself, and its length.  Terminated by a row whose iniKey is NULL as well, so a walk
// can use either.
extern const OptionDef TheOptionCatalog[];
extern const Int TheOptionCatalogCount;

/** The row with this Options.ini key, or NULL. */
extern const OptionDef *findOptionDef( const char *iniKey );

/** Clamp a raw value the way the row says. */
extern Int clampOptionValue( const OptionDef& def, Int value );

//-----------------------------------------------------------------------------
// Multisampling is stored as an index rather than a sample count, because that is what a combo box
// hands back and because the counts are not a range: 3x and 5x do not exist.
enum { OPTION_MSAA_LEVEL_COUNT = 5 };

/** Samples per pixel for a stored MSAA level.  0 is off. */
extern unsigned msaaSamplesForLevel( Int level );

/** The level that asks for this many samples, rounding a number in between downwards. */
extern Int msaaLevelForSamples( unsigned samples );

//-----------------------------------------------------------------------------
// Bloom is two percentages the shader wants and neither of them is a question a player can answer.
// "Bloom = 60" is a strength somebody has to find by experiment, and "BloomThreshold = 65" is worse:
// it is a brightness, it runs backwards - lower means more of the screen glows - and nothing on the
// screen tells you which way to push it.  So Options.ini stores a level, the menu offers those
// levels by name, and the percentages the levels stand for are in OptionsCatalog.cpp.
enum { BLOOM_LEVEL_COUNT = 4 };						///< off, subtle, normal, strong
enum { BLOOM_THRESHOLD_LEVEL_COUNT = 3 };	///< only the brightest, bright things, most of the picture

// Texture filtering is a mode too - bilinear, trilinear, anisotropic - and the combo box offers it
// by those names.  The number in Options.ini is the same 0..2 it was when the key had no control.
enum { TEXTURE_FILTER_MODE_COUNT = 3 };

/** Options.ini -> TheWritableGlobalData, for every row.  A key that is absent leaves the field at
	* whatever GlobalData's constructor put there, which is what makes an old Options.ini keep
	* working when a row is added. */
extern void loadOptionsFromPreferences( UserPreferences& pref );

/** TheGlobalData -> Options.ini, for every row.  Called from the options menu's Accept. */
extern void saveOptionsToPreferences( UserPreferences& pref );

#endif // __OPTIONSCATALOG_H_
