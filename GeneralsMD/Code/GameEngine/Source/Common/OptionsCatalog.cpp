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

// OptionsCatalog.cpp
//
// The table described in OptionsCatalog.h, and the four passes over it.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/OptionsCatalog.h"
#include "Common/GlobalData.h"
#include "Common/UserPreferences.h"

//-----------------------------------------------------------------------------
// The accessors.  Each is two lines and exists only because a member pointer cannot span Bool and
// Int fields without a cast, and a cast through a struct offset is the kind of thing that keeps
// working right up until somebody reorders GlobalData.
//-----------------------------------------------------------------------------

#define OPTION_BOOL_ACCESSORS( field )																												\
	static Int get_##field( void ) { return TheGlobalData->field ? 1 : 0; }											\
	static void set_##field( Int value ) { TheWritableGlobalData->field = (value != 0); }

#define OPTION_INT_ACCESSORS( field )																													\
	static Int get_##field( void ) { return TheGlobalData->field; }															\
	static void set_##field( Int value ) { TheWritableGlobalData->field = value; }

OPTION_BOOL_ACCESSORS( m_edgeScrollInWindowedMode )
OPTION_BOOL_ACCESSORS( m_snapCameraRotateTo45 )
OPTION_BOOL_ACCESSORS( m_zoomToCursor )
OPTION_BOOL_ACCESSORS( m_formationDrag )
OPTION_INT_ACCESSORS( m_bloomIntensity )
OPTION_INT_ACCESSORS( m_bloomThreshold )
OPTION_INT_ACCESSORS( m_windowMode )
OPTION_INT_ACCESSORS( m_msaaLevel )
OPTION_INT_ACCESSORS( m_healthBarMode )

//-----------------------------------------------------------------------------
static const unsigned TheMsaaSamples[ OPTION_MSAA_LEVEL_COUNT ] = { 0, 2, 4, 8, 16 };

unsigned msaaSamplesForLevel( Int level )
{
	if( level <= 0 || level >= OPTION_MSAA_LEVEL_COUNT )
		return 0;
	return TheMsaaSamples[ level ];
}

//-----------------------------------------------------------------------------
Int msaaLevelForSamples( unsigned samples )
{
	Int level = 0;
	for( Int i = 1; i < OPTION_MSAA_LEVEL_COUNT; ++i )
	{
		if( TheMsaaSamples[ i ] <= samples )
			level = i;
	}
	return level;
}

//-----------------------------------------------------------------------------
// The catalog.
//
// widgetName and labelKey are empty for every row that has no control in OptionsMenu.wnd yet.
// The menu passes skip those rows, so a setting can live here - loaded, saved, clamped - before it
// has anywhere to be clicked.
//
// A widget name is the layout file plus the control name, which is what nameToKey wants; OPT_WND
// spells the layout once instead of seventeen times.
//-----------------------------------------------------------------------------
#define OPT_WND( control )	"OptionsMenu.wnd:" control

const OptionDef TheOptionCatalog[] =
{
	// iniKey, widgetName, labelKey, kind, apply, lo, hi, get, set

	// The four camera and mouse habits below have no control in OptionsMenu.wnd any more.  They are
	// not gone: an empty widgetName only makes the menu passes skip the row, so Options.ini still
	// loads, clamps and saves each one and a player who wants the old behaviour can put the key
	// back by hand.  The defaults in GlobalData are what everybody else gets.

	// Retail refuses to edge-scroll in a window because the cursor can legitimately sit on the
	// border while you reach for something else; on a second monitor, or borderless, that is
	// exactly the behaviour you want back.
	{ "EdgeScrollInWindowedMode",	"", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_edgeScrollInWindowedMode, set_m_edgeScrollInWindowedMode },

	{ "SnapCameraRotateTo45",			"", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_snapCameraRotateTo45, set_m_snapCameraRotateTo45 },

	// MiddleMousePans used to sit here.  The middle button is the only camera drag there is now, so
	// there is nothing left to choose: it pans, and Ctrl turns the same drag into a rotate.
	{ "ZoomToCursor",							"", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_zoomToCursor, set_m_zoomToCursor },

	// A right drag over the ground spreads the selection along the line drawn instead of sending
	// everyone to one point.  On by default - the right button stopped scrolling, so the drag was
	// free - and here for anyone who would rather a slipped click did nothing at all.
	{ "FormationDrag",						"", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_formationDrag, set_m_formationDrag },

	// Eight rows used to sit here: grid and nudge build placement, snap-to-45 building rotation, the
	// placement range ring, workers returning to supply, detailed build tooltips, the HUD overlay
	// and replay archiving.  Every one of them is now on for everybody, decided in GlobalData's
	// constructor, so there is nothing left to load or save.  Gameplay is health bars and nothing
	// else.

	// Percent, 0 = off, which is what the GameData.ini default is: the game's artwork has no HDR
	// range in it, so how much glow looks right is a matter of taste rather than something to pick
	// on the player's behalf.  The key is "Bloom", not "BloomIntensity" - it predates the catalog
	// and an Options.ini in the wild already spells it this way.
	{ "Bloom",										OPT_WND( "SliderBloom" ), "GUI:Bloom",
		OPTION_INT, APPLY_LIVE, 0, 100,
		get_m_bloomIntensity, set_m_bloomIntensity },

	// The brightness below which nothing blooms at all.  Lower it and more of the picture joins in;
	// raise it and only the genuinely blinding things glow.
	{ "BloomThreshold",						OPT_WND( "SliderBloomThreshold" ), "GUI:BloomThreshold",
		OPTION_INT, APPLY_LIVE, 0, 100,
		get_m_bloomThreshold, set_m_bloomThreshold },

	// Fullscreen, borderless or windowed.  The old Windowed flag in GameData.ini seeds this and is
	// then derived back from it, so the device layer keeps reading the boolean it always read.
	// APPLY_RESTART is not laziness: the window style is settled by CreateWindow in WinMain, which
	// runs before the engine exists and reads this key itself through EarlyOptions.h.
	{ "WindowMode",								OPT_WND( "ComboBoxWindowMode" ), "GUI:WindowMode",
		OPTION_ENUM, APPLY_RESTART, 0, WINDOW_MODE_COUNT - 1,
		get_m_windowMode, set_m_windowMode },

	// Multisampling, as an index into 0/2/4/8/16 rather than a sample count - the device offers
	// those and nothing between them, so a slider would spend most of its travel on values that
	// silently round down.
	{ "MSAA",											OPT_WND( "ComboBoxMSAA" ), "GUI:MSAA",
		OPTION_ENUM, APPLY_DEVICE_RESET, 0, OPTION_MSAA_LEVEL_COUNT - 1,
		get_m_msaaLevel, set_m_msaaLevel },

	// Who wears a health bar: everyone, everyone hurt, only the selection, or nobody.  Read every
	// frame by the drawable that is about to draw one, so changing it shows immediately.
	{ "HealthBars",								OPT_WND( "ComboBoxHealthBars" ), "GUI:HealthBars",
		OPTION_ENUM, APPLY_LIVE, 0, HEALTH_BAR_MODE_COUNT - 1,
		get_m_healthBarMode, set_m_healthBarMode },

	{ NULL, NULL, NULL, OPTION_BOOL, APPLY_LIVE, 0, 0, NULL, NULL }
};

const Int TheOptionCatalogCount = (sizeof( TheOptionCatalog ) / sizeof( TheOptionCatalog[ 0 ] )) - 1;

//-----------------------------------------------------------------------------
const OptionDef *findOptionDef( const char *iniKey )
{
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
		if( stricmp( TheOptionCatalog[ i ].iniKey, iniKey ) == 0 )
			return &TheOptionCatalog[ i ];

	return NULL;
}

//-----------------------------------------------------------------------------
Int clampOptionValue( const OptionDef& def, Int value )
{
	if( value < def.lo )
		return def.lo;
	if( value > def.hi )
		return def.hi;
	return value;
}

//-----------------------------------------------------------------------------
/** Read one stored string.
	*
	* The option getters this replaces tested `stricmp(s, "yes") == 0` and called everything else
	* false, so a hand-edited `ZoomToCursor = true` silently did nothing.  UserPreferences::getBool
	* has always been the lenient one; the catalog follows it.  Writing still produces "yes"/"no". */
static Int parseOptionValue( const OptionDef& def, const AsciiString& stored )
{
	if( def.kind == OPTION_BOOL )
	{
		const char *s = stored.str();
		const Bool on = stricmp( s, "yes" ) == 0
									|| stricmp( s, "true" ) == 0
									|| stricmp( s, "on" ) == 0
									|| stricmp( s, "y" ) == 0
									|| stricmp( s, "t" ) == 0
									|| stricmp( s, "1" ) == 0;
		return on ? 1 : 0;
	}

	return clampOptionValue( def, atoi( stored.str() ) );
}

//-----------------------------------------------------------------------------
static AsciiString formatOptionValue( const OptionDef& def, Int value )
{
	if( def.kind == OPTION_BOOL )
		return AsciiString( value ? "yes" : "no" );

	AsciiString out;
	out.format( "%d", clampOptionValue( def, value ) );
	return out;
}

//-----------------------------------------------------------------------------
void loadOptionsFromPreferences( UserPreferences& pref )
{
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		const OptionDef& def = TheOptionCatalog[ i ];

		UserPreferences::const_iterator it = pref.find( AsciiString( def.iniKey ) );
		if( it == pref.end() )
			continue;	// no key, so keep whatever GameData.ini's default put in GlobalData

		def.set( parseOptionValue( def, it->second ) );
	}
}

//-----------------------------------------------------------------------------
void saveOptionsToPreferences( UserPreferences& pref )
{
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		const OptionDef& def = TheOptionCatalog[ i ];
		pref[ AsciiString( def.iniKey ) ] = formatOptionValue( def, def.get() );
	}
}
