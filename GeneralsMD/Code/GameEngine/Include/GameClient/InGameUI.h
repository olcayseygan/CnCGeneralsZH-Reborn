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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: InGameUI.h ///////////////////////////////////////////////////////////////////////////////
// Defines the in-game user interface singleton
// Author: Michael S. Booth, March 2001
//				 Colin Day August 2001, or so
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _IN_GAME_UI_H_
#define _IN_GAME_UI_H_

#include "Common/GameCommon.h"
#include "Common/GameType.h"
#include "Common/MessageStream.h"		// for GameMessageTranslator
#include "Common/KindOf.h"
#include "Common/SpecialPowerType.h"
#include "Common/Snapshot.h"
#include "Common/STLTypedefs.h"
#include "Common/SubsystemInterface.h"
#include "Common/UnicodeString.h"
#include "GameClient/DisplayString.h"
#include "GameClient/Mouse.h"
#include "GameClient/RadiusDecal.h"
#include "GameClient/View.h"

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
class Drawable;
class Object;
class ThingTemplate;
class GameWindow;
class VideoBuffer;
class VideoStreamInterface;
class CommandButton;
class SpecialPowerTemplate;
class WindowLayout;
class Anim2DTemplate;
class Anim2D;
class Shadow;
class Image;
enum LegalBuildCode;
enum KindOfType;
enum ShadowType;
enum CanAttackResult;

// ------------------------------------------------------------------------------------------------
enum RadiusCursorType
{
	RADIUSCURSOR_NONE = 0,
	RADIUSCURSOR_ATTACK_DAMAGE_AREA,
	RADIUSCURSOR_ATTACK_SCATTER_AREA,
	RADIUSCURSOR_ATTACK_CONTINUE_AREA,
	RADIUSCURSOR_GUARD_AREA,
	RADIUSCURSOR_EMERGENCY_REPAIR,
	RADIUSCURSOR_FRIENDLY_SPECIALPOWER,
	RADIUSCURSOR_OFFENSIVE_SPECIALPOWER,
	RADIUSCURSOR_SUPERWEAPON_SCATTER_AREA,
	
	RADIUSCURSOR_PARTICLECANNON, 
	RADIUSCURSOR_A10STRIKE,
	RADIUSCURSOR_CARPETBOMB,
	RADIUSCURSOR_DAISYCUTTER,
	RADIUSCURSOR_PARADROP,
	RADIUSCURSOR_SPYSATELLITE, 
	RADIUSCURSOR_SPECTREGUNSHIP,
	RADIUSCURSOR_HELIX_NAPALM_BOMB,

	RADIUSCURSOR_NUCLEARMISSILE, 
	RADIUSCURSOR_EMPPULSE,
	RADIUSCURSOR_ARTILLERYBARRAGE,
	RADIUSCURSOR_NAPALMSTRIKE,
	RADIUSCURSOR_CLUSTERMINES,

	RADIUSCURSOR_SCUDSTORM, 
	RADIUSCURSOR_ANTHRAXBOMB,
	RADIUSCURSOR_AMBUSH, 
	RADIUSCURSOR_RADAR,
	RADIUSCURSOR_SPYDRONE,
	RADIUSCURSOR_FRENZY,
	
	RADIUSCURSOR_CLEARMINES,
	RADIUSCURSOR_AMBULANCE,


	RADIUSCURSOR_COUNT	// keep last
};

#ifdef DEFINE_RADIUSCURSOR_NAMES
static const char *TheRadiusCursorNames[] = 
{
	"NONE",
	"ATTACK_DAMAGE_AREA",
	"ATTACK_SCATTER_AREA",
	"ATTACK_CONTINUE_AREA",
	"GUARD_AREA",
	"EMERGENCY_REPAIR",
	"FRIENDLY_SPECIALPOWER",	//green
	"OFFENSIVE_SPECIALPOWER", //red
	"SUPERWEAPON_SCATTER_AREA",//red

	"PARTICLECANNON", 
	"A10STRIKE",
	"CARPETBOMB",
	"DAISYCUTTER",
	"PARADROP",
	"SPYSATELLITE",  
  "SPECTREGUNSHIP",
  "HELIX_NAPALM_BOMB",

	"NUCLEARMISSILE", 
	"EMPPULSE",
	"ARTILLERYBARRAGE",
	"NAPALMSTRIKE",
	"CLUSTERMINES",

	"SCUDSTORM", 
	"ANTHRAXBOMB",
	"AMBUSH", 
	"RADAR",
	"SPYDRONE",
	"FRENZY",
	
	"CLEARMINES",
	"AMBULANCE",

	NULL
};
#endif

// ------------------------------------------------------------------------------------------------
/** For keeping track in the UI of how much build progress has been done */
// ------------------------------------------------------------------------------------------------
enum { MAX_BUILD_PROGRESS = 64 };  ///< interface can support building this many different units
struct BuildProgress
{
	const ThingTemplate *m_thingTemplate;
	Real m_percentComplete;
	GameWindow *m_control;
};

// TYPE DEFINES ///////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
typedef std::list<Drawable *> DrawableList;
typedef std::list<Drawable *>::iterator DrawableListIt;
typedef std::list<Drawable *>::const_iterator DrawableListCIt;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class SuperweaponInfo : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(SuperweaponInfo, "SuperweaponInfo")		

private:
// not saved
	DisplayString *             m_nameDisplayString;						///< display string used to render the message
	DisplayString *             m_timeDisplayString;						///< display string used to render the message
	Color												m_color;
	const SpecialPowerTemplate*	m_powerTemplate;

public:

	SuperweaponInfo(
		ObjectID id,
		UnsignedInt timestamp,
		Bool hiddenByScript,
		Bool hiddenByScience,
		Bool ready,
    Bool evaReadyPlayed,
		const AsciiString& superweaponNormalFont, 
		Int superweaponNormalPointSize, 
		Bool superweaponNormalBold,
		Color c, 
		const SpecialPowerTemplate* spt
	);

	const SpecialPowerTemplate*	getSpecialPowerTemplate() const { return m_powerTemplate; }
	Color												getColor() const { return m_color; }
	void setFont(const AsciiString& superweaponNormalFont, Int superweaponNormalPointSize, Bool superweaponNormalBold);
	void setText(const UnicodeString& name, const UnicodeString& time);
	void drawBackdrop(Int x, Int y);				///< plate behind the name and time, so they stay readable over terrain
	void drawName(Int x, Int y, Color color, Color dropColor);
	void drawTime(Int x, Int y, Color color, Color dropColor);
	Real getHeight() const;

// saved & public
	AsciiString									m_powerName;
	ObjectID										m_id;
	UnsignedInt									m_timestamp;									  ///< seconds shown in display string
	Bool												m_hiddenByScript;
	Bool												m_hiddenByScience;
 	Bool												m_ready;											///< Stores if we were ready last draw, since readyness can change without time changing
  Bool                        m_evaReadyPlayed;             ///< Stores if Eva announced superweapon is ready
// not saved, but public
 	Bool												m_forceUpdateText;

};

// ------------------------------------------------------------------------------------------------
typedef std::list<SuperweaponInfo *> SuperweaponList;
typedef std::map<AsciiString, SuperweaponList> SuperweaponMap;

// ------------------------------------------------------------------------------------------------
// Popup message box
class PopupMessageData : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(PopupMessageData, "PopupMessageData")		
public:
	UnicodeString		message;
	Int							x;
	Int							y;
	Int							width;
	Color						textColor;
	Bool						pause;
	Bool						pauseMusic;
	WindowLayout*	layout;
};
EMPTY_DTOR(PopupMessageData)

// ------------------------------------------------------------------------------------------------
class NamedTimerInfo : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(NamedTimerInfo, "NamedTimerInfo")		
public:
	AsciiString			m_timerName;							///< Timer name, needed on Load to reconstruct Map.
	UnicodeString		timerText;								///< timer text
	DisplayString*	displayString;						///< display string used to render the message
	UnsignedInt			timestamp;									///< seconds shown in display string
	Color						color;
	Bool						isCountdown;
};
EMPTY_DTOR(NamedTimerInfo)

// ------------------------------------------------------------------------------------------------
typedef std::map<AsciiString, NamedTimerInfo *> NamedTimerMap;
typedef NamedTimerMap::iterator NamedTimerMapIt;

// ------------------------------------------------------------------------------------------------
enum {MAX_SUBTITLE_LINES = 4};							///< The maximum number of lines a subtitle can have

// ------------------------------------------------------------------------------------------------
// Floating Text Data
class FloatingTextData : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(FloatingTextData, "FloatingTextData")		
public:
	FloatingTextData(void);
	//~FloatingTextData(void);

	Color						m_color;														///< It's current color
	UnicodeString		m_text;											///< the text we're displaying
	DisplayString*	m_dString;									///< The display string
	Coord3D					m_pos3D;													///< the 3d position in game coords
	Int							m_frameTimeOut;												///< when we want this thing to disappear
	Int							m_frameCount;													///< how many frames have we been displaying text?
};

typedef std::list<FloatingTextData *> FloatingTextList;
typedef FloatingTextList::iterator	FloatingTextListIt;

enum 
{
	DEFAULT_FLOATING_TEXT_TIMEOUT = LOGICFRAMES_PER_SECOND/3,
};

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
enum WorldAnimationOptions
{
	WORLD_ANIM_NO_OPTIONS								= 0x00000000,
	WORLD_ANIM_FADE_ON_EXPIRE						= 0x00000001,
	WORLD_ANIM_PLAY_ONCE_AND_DESTROY		= 0x00000002,
};

// ------------------------------------------------------------------------------------------------
class WorldAnimationData
{

public:

	WorldAnimationData( void );
	~WorldAnimationData( void ) { }

	Anim2D *m_anim;												///< the animation instance
	Coord3D m_worldPos;										///< position in the world
	UnsignedInt m_expireFrame;						///< frame we expire on
	WorldAnimationOptions m_options;			///< options
	Real m_zRisePerSecond;								///< Z units to rise per second

};
typedef std::list< WorldAnimationData *> WorldAnimationList;
typedef WorldAnimationList::iterator WorldAnimationListIterator;



// ------------------------------------------------------------------------------------------------
/** Basic functionality common to all in-game user interfaces */
// ------------------------------------------------------------------------------------------------ 
class InGameUI : public SubsystemInterface, public Snapshot
{
	
friend class Drawable;	// for selection/deselection transactions
		
public:  // ***************************************************************************************

	enum SelectionRules
	{
		SELECTION_ANY, //Only one of the selected units has to qualify
		SELECTION_ALL, //All selected units have to qualify
	};
	enum ActionType
	{
		ACTIONTYPE_NONE,
		ACTIONTYPE_ATTACK_OBJECT,
		ACTIONTYPE_GET_REPAIRED_AT,
		ACTIONTYPE_DOCK_AT,
		ACTIONTYPE_GET_HEALED_AT,
		ACTIONTYPE_REPAIR_OBJECT,
		ACTIONTYPE_RESUME_CONSTRUCTION,
		ACTIONTYPE_ENTER_OBJECT,
		ACTIONTYPE_HIJACK_VEHICLE,
		ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB,
		ACTIONTYPE_CAPTURE_BUILDING,
		ACTIONTYPE_DISABLE_VEHICLE_VIA_HACKING,
#ifdef ALLOW_SURRENDER
		ACTIONTYPE_PICK_UP_PRISONER,
#endif
		ACTIONTYPE_STEAL_CASH_VIA_HACKING,
		ACTIONTYPE_DISABLE_BUILDING_VIA_HACKING,
		ACTIONTYPE_MAKE_DEFECTOR,
		ACTIONTYPE_SET_RALLY_POINT,
		ACTIONTYPE_COMBATDROP_INTO,
		ACTIONTYPE_SABOTAGE_BUILDING,

		//Keep last.
		NUM_ACTIONTYPES
	};

	InGameUI( void );
	virtual ~InGameUI( void );
	
	// Inherited from subsystem interface -----------------------------------------------------------
	virtual	void init( void );															///< Initialize the in-game user interface
	virtual void update( void );														///< Update the UI by calling preDraw(), draw(), and postDraw()
	virtual void reset( void );															///< Reset
	//-----------------------------------------------------------------------------------------------

	// interface for the popup messages
	virtual void popupMessage( const AsciiString& message, Int x, Int y, Int width, Bool pause, Bool pauseMusic);
	virtual void popupMessage( const AsciiString& message, Int x, Int y, Int width, Color textColor, Bool pause, Bool pauseMusic);
	PopupMessageData *getPopupMessageData( void ) { return m_popupMessageData; }
	void clearPopupMessageData( void );

	// interface for messages to the user
	// srj sez: passing as const-ref screws up varargs for some reason. dunno why. just pass by value.
	virtual void messageColor( const RGBColor *rgbColor, UnicodeString format, ... );	///< display a colored message to the user
	virtual void message( UnicodeString format, ... );				  ///< display a message to the user
	virtual void message( AsciiString stringManagerLabel, ... );///< display a message to the user
	virtual void toggleMessages( void ) { m_messagesOn = 1 - m_messagesOn; }	///< toggle messages on/off
	virtual Bool isMessagesOn( void ) { return m_messagesOn; }	///< are the display messages on
	void freeMessageResources( void );				///< free resources for the ui messages
	Color getMessageColor(Bool altColor) { return (altColor)?m_messageColor2:m_messageColor1; }
	
	// interface for military style messages
	virtual void militarySubtitle( const AsciiString& label, Int duration );			// time in milliseconds
	virtual void removeMilitarySubtitle( void );
	
	// for can't build messages
	virtual void displayCantBuildMessage( LegalBuildCode lbc ); ///< display message to use as to why they can't build here

	// interface for graphical "hints" which provide visual feedback for user-interface commands
	virtual void beginAreaSelectHint( const GameMessage *msg );	///< Used by HintSpy. An area selection is occurring, start graphical "hint"
	virtual void endAreaSelectHint( const GameMessage *msg );		///< Used by HintSpy. An area selection had occurred, finish graphical "hint"
	virtual void createMoveHint( const GameMessage *msg );			///< A move command has occurred, start graphical "hint"
	virtual void createAttackHint( const GameMessage *msg );		///< An attack command has occurred, start graphical "hint"
	virtual void createForceAttackHint( const GameMessage *msg );		///< A force attack command has occurred, start graphical "hint"

	virtual void createMouseoverHint( const GameMessage *msg );	///< An object is mouse hovered over, start hint if any
	virtual void createCommandHint( const GameMessage *msg );		///< Used by HintSpy. Someone is selected so generate the right Cursor for the potential action
	virtual void createGarrisonHint( const GameMessage *msg );  ///< A garrison command has occurred, start graphical "hint"
	
	virtual void addSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate);
	virtual Bool removeSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate);
	virtual void objectChangedTeam(const Object *obj, Int oldPlayerIndex, Int newPlayerIndex);	// notification for superweapons, etc

	virtual void setSuperweaponDisplayEnabledByScript( Bool enable );	///< Set the superweapon display enabled or disabled
	virtual Bool getSuperweaponDisplayEnabledByScript( void ) const;				///< Get the current superweapon display status

	virtual void hideObjectSuperweaponDisplayByScript(const Object *obj);
	virtual void showObjectSuperweaponDisplayByScript(const Object *obj);

	void addNamedTimer( const AsciiString& timerName, const UnicodeString& text, Bool isCountdown );
	void removeNamedTimer( const AsciiString& timerName );
	void showNamedTimerDisplay( Bool show );

	// mouse mode interface
	virtual void setScrolling( Bool isScrolling );							///< set right-click scroll mode
	virtual Bool isScrolling( void );														///< are we scrolling?
	virtual void setSelecting( Bool isSelecting );							///< set drag select mode
	virtual Bool isSelecting( void );														///< are we selecting?
	virtual void setScrollAmount( Coord2D amt );								///< set scroll amount
	virtual Coord2D getScrollAmount( void );										///< get scroll amount

	// gui command interface
	virtual void setGUICommand( const CommandButton *command );				///< the command has been clicked in the UI and needs additional data
	virtual const CommandButton *getGUICommand( void ) const;								///< get the pending gui command

	// build interface
	virtual void placeBuildAvailable( const ThingTemplate *build, Drawable *buildDrawable );				///< built thing being placed
	virtual const ThingTemplate *getPendingPlaceType( void );					///< get item we're trying to place
	virtual const Drawable *getPendingPlaceDrawable( void ) const;		///< the ghost riding the cursor, NULL when not placing
	virtual const ObjectID getPendingPlaceSourceObjectID( void );			///< get producing object
	virtual Bool getPreventLeftClickDeselectionInAlternateMouseModeForOneClick() const { return m_preventLeftClickDeselectionInAlternateMouseModeForOneClick; }
	virtual void setPreventLeftClickDeselectionInAlternateMouseModeForOneClick( Bool set ) { m_preventLeftClickDeselectionInAlternateMouseModeForOneClick = set; }
	virtual void setPlacementStart( const ICoord2D *start );					///< placement anchor point (for choosing angles)
	virtual void setPlacementEnd( const ICoord2D *end );							///< set target placement point (for choosing angles)
	virtual Bool isPlacementAnchored( void );													///< is placement arrow anchor set
	virtual void getPlacementPoints( ICoord2D *start, ICoord2D *end );///< get the placemnt arrow points
	virtual Real getPlacementAngle( void );														///< placement angle of drawable at cursor when placing down structures
	virtual Real computePlacementAngle( const ICoord2D *start, const ICoord2D *end );	///< heading a drag from start to end aims a structure at

	/** Round a heading to the nearest eighth of a turn.  Nearest, not toward or away from zero:
		* toAngle returns -PI..PI and the drag has to feel the same on both halves of the circle, so
		* every heading is at most 22.5 degrees of mouse travel from the one it snaps to.  Inline so a
		* test can reach it without linking the whole in-game UI. */
	static Real snapAngleTo45( Real angle )
	{
		const Real step = PI / 4.0f;

		return ((Real)REAL_TO_INT_FLOOR( angle / step + 0.5f )) * step;
	}
	/** Experimental GridBuildPlacement: quantize a structure's position to the pathfinder's own
		* build grid, so that structures put down by eye line up with each other and with the cells
		* the pathfinder actually reasons about. */
	virtual void snapPlacementToGrid( Coord3D *world, const ThingTemplate *what, Real angle ) const;

	/** Where the pathfinder's cell boundaries actually are.  It files a world position under
		* floor( (v + 0.5) / PATHFIND_CELL_SIZE ) (see Pathfinder::internal_classifyObjectFootprint),
		* so cell k spans [10k - 0.5, 10k + 9.5) - the lines are at k*10 - 0.5, half a unit off the
		* round numbers.  Snapping to the round numbers left every footprint edge half a unit inside
		* the neighbouring cell, so two buildings that looked flush were both claiming it. */
	enum { PLACEMENT_CELL = 10 };										///< PATHFIND_CELL_SIZE, without the pathfinder header
	static Real placementGridLine( Int k ) { return k * (Real)PLACEMENT_CELL - 0.5f; }

	/** One axis of that snap.  'extent' is the structure's half-size along this axis, so a footprint
		* that is an odd number of cells wide is centred on a cell and an even one on the line between
		* two - either way its edges land on grid lines and two neighbours can share one.  Inline and
		* static so a test can reach it without linking the whole in-game UI. */
	static Real snapPlacementAxis( Real v, Real extent )
	{
		const Real cell = (Real)PLACEMENT_CELL;		// one pathfinder cell, one grid square
		const Real line = placementGridLine( 0 );	// where cell 0 starts

		Int cells = REAL_TO_INT_FLOOR( 2.0f * extent / cell + 0.5f );
		if( cells < 1 )
			cells = 1;

		const Real offset = ((cells & 1) ? cell * 0.5f : 0.0f) + line;

		return ((Real)REAL_TO_INT_FLOOR( (v - offset) / cell + 0.5f )) * cell + offset;
	}
	/** NudgeBuildPlacement: when the spot under the cursor is blocked, slide the structure to the
		* nearest one it does fit and show it there, so the ghost answers "here, then" instead of just
		* going red.  Moves 'world' and returns TRUE if it found somewhere; leaves it alone otherwise. */
	virtual Bool nudgePlacementToLegal( Coord3D *world, const ThingTemplate *what, Real angle,
																			Object *builderObject ) const;

	/** What the ghost asks of a spot before it goes green.  One place, because the nudge search has
		* to ask exactly the same question or it would offer a spot the ghost then paints red. */
	static UnsignedInt placementCheckOptions( void );

	//
	// Structures ordered but not standing yet.  A build order is a message: it crosses the network
	// and only becomes an object on the ground several frames later, and until it does the spot it
	// was ordered on is empty ground to every check that decides whether the next click is legal.
	// Holding shift on a laggy link put two structures on the same square that way, and the ghost
	// stayed green over a building that was already paid for.  So the client remembers its own
	// orders for a couple of seconds and treats them as standing.
	//
	// It is a client-side courtesy, not the rule: the rule is re-asked on the logic side, where the
	// first structure really does exist by the time the second order arrives.
	//
	enum { PENDING_PLACEMENTS = 8 };						///< orders in flight at once; the oldest is overwritten
	enum { PENDING_PLACEMENT_FRAMES = 60 };			///< logic frames one is remembered for - two seconds,
																							///  comfortably longer than any network delay

	/// the ground a structure of this template put down here would stand on
	static void placementFootprint( const ThingTemplate *what, const Coord3D *world, Real angle,
																	Region2D *footprint );
	/// do two of those share any ground?  edge to edge is not sharing - structures are built flush
	static Bool footprintsOverlap( const Region2D *a, const Region2D *b );

	/// remember a structure just ordered here, so the next click can see it
	void recordPendingPlacement( const ThingTemplate *what, const Coord3D *world, Real angle );
	/// drop the lot - a new game is not the old one's orders
	void forgetPendingPlacements( void );
	/// would a structure here land on one of those?
	Bool overlapsPendingPlacement( const Coord3D *world, const ThingTemplate *what, Real angle ) const;
	enum { PLACEMENT_NUDGE_PATHFINDS = 12 };						///< unreachable candidates tolerated before the search gives up

	/** The offsets that search tries: every cell of a square this many rings across, ordered by
		* true distance so that "the first one that fits" is "the nearest one that fits".  Ring by
		* ring would not be that ordering - from three rings out, a ring's own diagonal (1.41 cells)
		* is farther than the next ring's straight neighbour - and sampling only the eight compass
		* directions, as this first did, walks straight past the gap one cell to the side of them.
		* The table is built and sorted once, on the first placement of the session.  Inline and
		* static so a test can check the ordering without linking the whole in-game UI. */
	enum { PLACEMENT_NUDGE_RINGS = 10 };								///< how far out to look, in grid cells
	enum { PLACEMENT_NUDGE_SPAN = 2 * PLACEMENT_NUDGE_RINGS + 1 };
	enum { PLACEMENT_NUDGE_TRIES = PLACEMENT_NUDGE_SPAN * PLACEMENT_NUDGE_SPAN - 1 };
	static void placementNudgeOffset( Int index, Real step, Real *dx, Real *dy )
	{
		static Int cellX[ PLACEMENT_NUDGE_TRIES ];
		static Int cellY[ PLACEMENT_NUDGE_TRIES ];
		static Bool ordered = FALSE;

		if( ordered == FALSE )
		{
			Int n = 0;
			for( Int y = -PLACEMENT_NUDGE_RINGS; y <= PLACEMENT_NUDGE_RINGS; y++ )
				for( Int x = -PLACEMENT_NUDGE_RINGS; x <= PLACEMENT_NUDGE_RINGS; x++ )
					if( x != 0 || y != 0 )		// the spot the player is pointing at is the one that failed
					{
						cellX[ n ] = x;
						cellY[ n ] = y;
						n++;
					}

			// insertion sort by distance; it runs once for the whole session, and being in distance
			// order is the entire contract of this list
			for( Int i = 1; i < PLACEMENT_NUDGE_TRIES; i++ )
			{
				const Int keyX = cellX[ i ];
				const Int keyY = cellY[ i ];
				const Int keyD = keyX * keyX + keyY * keyY;
				Int j = i - 1;

				while( j >= 0 && cellX[ j ] * cellX[ j ] + cellY[ j ] * cellY[ j ] > keyD )
				{
					cellX[ j + 1 ] = cellX[ j ];
					cellY[ j + 1 ] = cellY[ j ];
					j--;
				}
				cellX[ j + 1 ] = keyX;
				cellY[ j + 1 ] = keyY;
			}

			ordered = TRUE;
		}

		*dx = cellX[ index ] * step;
		*dy = cellY[ index ] * step;
	}
	virtual void setPlacementAngle( Real angle );											///< aim the structure on the cursor, and keep that heading for the next one
	virtual Bool rotatePendingPlacement( Int steps );									///< turn the structure on the cursor by 45 degrees a step

	// Drawable selection mechanisms
	virtual void selectDrawable( Drawable *draw );					///< Mark given Drawable as "selected"
	virtual void deselectDrawable( Drawable *draw );				///< Clear "selected" status from Drawable
	virtual void deselectAllDrawables( Bool postMsg = true );							///< Clear the "select" flag from all drawables
	virtual Int getSelectCount( void ) { return m_selectCount; }		///< Get count of currently selected drawables
	virtual Int getMaxSelectCount( void ) { return m_maxSelectCount; }	///< Get the max number of selected drawables
	virtual UnsignedInt getFrameSelectionChanged( void ) { return m_frameSelectionChanged; }	///< Get the max number of selected drawables
	virtual const DrawableList *getAllSelectedDrawables( void ) const;	///< Return the list of all the currently selected Drawable IDs.
	virtual const DrawableList *getAllSelectedLocalDrawables( void );		///< Return the list of all the currently selected Drawable IDs owned by the current player.
	virtual Drawable *getFirstSelectedDrawable( void );							///< get the first selected drawable (if any)
	virtual DrawableID getSoloNexusSelectedDrawableID( void ) { return m_soloNexusSelectedDrawableID; }  ///< Return the one drawable of the nexus if only 1 angry mob is selected 
	virtual Bool isDrawableSelected( DrawableID idToCheck ) const;	///< Return true if the selected ID is in the drawable list
	virtual Bool isAnySelectedKindOf( KindOfType kindOf ) const;		///< is any selected object a kind of 
	virtual Bool isAllSelectedKindOf( KindOfType kindOf ) const;		///< are all selected objects a kind of

	virtual void setRadiusCursor(RadiusCursorType r, const SpecialPowerTemplate* sp, WeaponSlotType wslot);
	virtual void setRadiusCursorNone() { setRadiusCursor(RADIUSCURSOR_NONE, NULL, PRIMARY_WEAPON); }
	/// ring an explicit radius with the guard-area decal; used while placing a defensive structure,
	/// where there is no Object yet to read a weapon range off.
	virtual void setRadiusCursorForRadius(Real radius);

	virtual void setInputEnabled( Bool enable );										///< Set the input enabled or disabled
	virtual void clearModifierModes( void );												///< forget every mode a held key puts us in (see the definition)
	virtual Bool getInputEnabled( void ) { return m_inputEnabled; }	///< Get the current input status

	virtual void disregardDrawable( Drawable *draw );				///< Drawable is being destroyed, clean up any UI elements associated with it

	virtual void preDraw( void );														///< Logic which needs to occur before the UI renders
	virtual void draw( void ) = 0;													///< Render the in-game user interface
	virtual void postDraw( void );													///< Logic which needs to occur after the UI renders

	//
	// One cameo of the global production strip: which producer it belongs to, which entry of that
	// producer's queue it is, and where it was drawn this frame.
	//
	enum { PRODUCTION_STRIP_ROW_MAX = 5 };	///< cameos one column will draw, stacked upward; whatever
																					///  is left over closes it as a sixth cell wearing a "+N"
	enum { PRODUCTION_STRIP_WATCH_MAX = 3 };	///< and while watching, where eight columns share the
																					///  screen and a column is a whole player: three cameos and
																					///  a "+N", since a run of the same unit is one of them now
	enum { PRODUCTION_STRIP_ROWS = 8 };			///< playing: one column, the whole base's.
																					///  watching: one column per player, so eight of them

	//
	// Playing, everything the base has coming is one column: what a factory is turning out and what
	// a dozer is raising stand in the same run of cells, soonest first, because the question is when
	// the next thing lands and not which of the two kinds it is.  The building you have selected does
	// not get a column of its own either - its items lead this one.
	//
	enum
	{
		PRODUCTION_ROW_QUEUE		= 0		///< queues and building sites together
	};

	//
	// A queue slot is one slot of the general's power bar down in the corner, measurement for
	// measurement: that bar is where a tray of cameos already lives in this game, so the strip is
	// built out of the same tray instead of inventing a second look for the same job.  Every number
	// here is read off GenPowersShortcutBar*.wnd, in the 800x600 the rest of the strip is written in.
	//
	enum
	{
		PRODUCTION_STRIP_TRAY_W		= 48,	///< the tray, at the size that bar draws it - never stretched
		PRODUCTION_STRIP_TRAY_H		= 41,
		PRODUCTION_STRIP_TRAY_X		= 1,	///< where the cameo sits inside it
		PRODUCTION_STRIP_TRAY_Y		= 7,
		PRODUCTION_STRIP_QUEUE_W	= 39,	///< and how big it is there
		PRODUCTION_STRIP_QUEUE_H	= 27,
		PRODUCTION_STRIP_TRAY_OVER = 6,	///< that bar steps 35 between 41-tall slots: its trays overlap by
																		///  six sideways, and so do ours.  Stacked, they do not: the
																		///  trays sit one on top of the next at their full height, so no
																		///  cameo has another tray's rail lying over its top edge
		PRODUCTION_STRIP_BAR_STEP	= 35	///< the step itself, kept so the overlap can be checked against it
	};

	///< does an item go in front of one already in the row?  the selected building's items are a
	///< block of their own at the head of it; inside a block, soonest finished first, ties keeping
	///< the order they were met in so a base of identical barracks does not shuffle frame to frame
	static Bool stripSlotGoesBefore( Bool leads, Int remaining,
																	 Bool otherLeads, Int otherRemaining );
	struct ProductionStripSlot
	{
		ObjectID			producer;						///< the building this item is queued on
		Int						id;									///< a unit's ProductionID, or an upgrade's name key
																			///  (kept as an Int so this header stays clear of GameLogic)
		Bool					isUpgrade;					///< which of those two the id means
		Int						typeKey;						///< what the item is rather than which order it is - a unit's template
																			///  id, an upgrade's name key - so a run of the same thing queued
																			///  back to back becomes one cameo
		Bool					isStructure;				///< the slot is a building going up on the map, not an item in
																			///  a queue: 'producer' is that building and there is no id
		Bool					leads;							///< it belongs to the building you have selected, so it stands at the
																			///  head of the row whatever the rest of the base is finishing
		Int						remaining;					///< logic frames until this item pops, waiting time ahead of it included
		Int						quantity;						///< how many of the same thing this cameo stands for: a queue of five
																			///  Red Guards is one cameo wearing an "x5", not five cameos of
																			///  the same picture pushing everything else off the row
		ICoord2D			pos;								///< top left corner of the cameo on screen
	};

	/// a click landed on the strip: jump to that producer, or cancel the item when 'cancel' is set
	Bool handleProductionStripClick( const ICoord2D *mouse, Bool cancel );

	//
	// One superweapon countdown of the top right strip. The list is rebuilt every frame out of
	// whatever timers are live, so nothing here outlives the draw that filled it in.
	//
	enum { SUPERWEAPON_STRIP_COLS = 6 };		///< cameos in one row, the soonest at the right hand end
	enum { SUPERWEAPON_STRIP_ROWS = 3 };		///< rows of them, and the rest become a "+N"
	enum { SUPERWEAPON_STRIP_MAX = SUPERWEAPON_STRIP_COLS * SUPERWEAPON_STRIP_ROWS };
	struct SuperweaponIconSlot
	{
		const Image *	image;							///< the cameo the command bar wears for this power
		Int						seconds;						///< seconds until it can be fired, 0 once it is ready
		Int						percent;						///< how much of the charge is done, for the radial sweep
		Bool					ready;							///< charged: it flashes instead of counting
		Color					color;							///< whose it is, the colour the timer was registered with
	};

	/// Ingame video playback 
	virtual void playMovie( const AsciiString& movieName );
	virtual void stopMovie( void );
	virtual VideoBuffer* videoBuffer( void );

	/// Ingame cameo video playback 
	virtual void playCameoMovie( const AsciiString& movieName );
	virtual void stopCameoMovie( void );
	virtual VideoBuffer* cameoVideoBuffer( void );

  // mouse over information	
	virtual DrawableID getMousedOverDrawableID( void ) const;	///< Get drawble ID of drawable under cursor
	
	/// Set the ingame flag as to if we have the Quit menu up or not
	virtual void setQuitMenuVisible( Bool t ) { m_isQuitMenuVisible = t; }
	virtual Bool isQuitMenuVisible( void ) const { return m_isQuitMenuVisible; }

	// INI file parsing
	virtual const FieldParse* getFieldParse( void ) const { return s_fieldParseTable; }


	//Provides a global way to determine whether or not we can issue orders to what we have selected.
	Bool areSelectedObjectsControllable() const;
	//Wrapper function that includes any non-attack canSelectedObjectsXXX checks.
	Bool canSelectedObjectsNonAttackInteractWithObject( const Object *objectToInteractWith, SelectionRules rule ) const;
	//Wrapper function that checks a specific action.
	CanAttackResult getCanSelectedObjectsAttack( ActionType action, const Object *objectToInteractWith, SelectionRules rule, Bool additionalChecking = FALSE ) const;
	Bool canSelectedObjectsDoAction( ActionType action, const Object *objectToInteractWith, SelectionRules rule, Bool additionalChecking = FALSE ) const;
	Bool canSelectedObjectsDoSpecialPower( const CommandButton *command, const Object *objectToInteractWith, const Coord3D *position, SelectionRules rule, UnsignedInt commandOptions, Object* ignoreSelObj ) const;
	Bool canSelectedObjectsEffectivelyUseWeapon( const CommandButton *command, const Object *objectToInteractWith, const Coord3D *position, SelectionRules rule ) const;
	Bool canSelectedObjectsOverrideSpecialPowerDestination( const Coord3D *loc, SelectionRules rule, SpecialPowerType spType = SPECIAL_INVALID ) const;

	// Selection Methods
	virtual Int selectUnitsMatchingCurrentSelection();                        ///< selects matching units
	virtual Int selectMatchingAcrossScreen();                         ///< selects matching units across screen
	virtual Int selectMatchingAcrossMap();                            ///< selects matching units across map
	virtual Int selectMatchingAcrossRegion( IRegion2D *region );			// -1 = no locally-owned selection, 0+ = # of units selected

	virtual Int selectAllUnitsByType(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear);                
	virtual Int selectAllUnitsByTypeAcrossScreen(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear);                         
	virtual Int selectAllUnitsByTypeAcrossMap(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear);                            
	virtual Int selectAllUnitsByTypeAcrossRegion( IRegion2D *region, KindOfMaskType mustBeSet, KindOfMaskType mustBeClear );			
	
	virtual void buildRegion( const ICoord2D *anchor, const ICoord2D *dest, IRegion2D *region );  ///< builds a region around the specified coordinates

	virtual Bool getDisplayedMaxWarning( void ) { return m_displayedMaxWarning; }
	virtual void setDisplayedMaxWarning( Bool selected ) { m_displayedMaxWarning = selected; }

	// Floating Test Methods
	virtual void addFloatingText(const UnicodeString& text,const Coord3D * pos, Color color);

	// Drawable caption stuff
	AsciiString	getDrawableCaptionFontName( void )	{ return m_drawableCaptionFont; }
	Int					getDrawableCaptionPointSize( void )	{ return m_drawableCaptionPointSize; }
	Bool				isDrawableCaptionBold( void )				{ return m_drawableCaptionBold; }
	Color				getDrawableCaptionColor( void )			{ return m_drawableCaptionColor; }

	inline Bool shouldMoveRMBScrollAnchor( void ) { return m_moveRMBScrollAnchor; }

	Bool isClientQuiet( void ) const			{ return m_clientQuiet; }
	Bool isInWaypointMode( void ) const			{ return m_waypointMode; }
	Bool isInForceAttackMode( void ) const	{ return m_forceAttackMode; }
	Bool isInForceMoveToMode( void ) const	{ return m_forceMoveToMode; }
	Bool isInPreferSelectionMode( void ) const { return m_preferSelection; }

	void setClientQuiet( Bool enabled )  { m_clientQuiet = enabled; }
	void setWaypointMode( Bool enabled )		{ m_waypointMode = enabled; }
	void setForceMoveMode( Bool enabled )		{ m_forceMoveToMode = enabled; }
	void setForceAttackMode( Bool enabled )		{ m_forceAttackMode = enabled; }
	void setPreferSelectionMode( Bool enabled )		{ m_preferSelection = enabled; }
	
	void toggleAttackMoveToMode( void )				{ m_attackMoveToMode = !m_attackMoveToMode; }
	Bool isInAttackMoveToMode( void ) const		{ return m_attackMoveToMode; }
	void clearAttackMoveToMode( void )				{ m_attackMoveToMode = FALSE; }
	
	// zeroing the repeat clock makes the first quantized step happen on the very next update, so a
	// tap of the key is one eighth and a hold is one eighth every CAMERA_SNAP_REPEAT_MS.
	void setCameraRotateLeft( Bool set )		{ m_cameraRotatingLeft = set; m_cameraSnapRepeatMs = 0; }
	void setCameraRotateRight( Bool set )		{ m_cameraRotatingRight = set; m_cameraSnapRepeatMs = 0; }
	void setCameraZoomIn( Bool set )				{ m_cameraZoomingIn = set; }
	void setCameraZoomOut( Bool set )				{ m_cameraZoomingOut = set; }
  void setCameraTrackingDrawable( Bool set ) { m_cameraTrackingDrawable = set; }
	Bool isCameraRotatingLeft() const { return m_cameraRotatingLeft; }
	Bool isCameraRotatingRight() const { return m_cameraRotatingRight; }
	Bool isCameraZoomingIn() const { return m_cameraZoomingIn; }
	Bool isCameraZoomingOut() const { return m_cameraZoomingOut; }
  Bool isCameraTrackingDrawable() const { return m_cameraTrackingDrawable; }
	void resetCamera();

	virtual void addIdleWorker( Object *obj );
	virtual void removeIdleWorker( Object *obj, Int playerNumber );
	virtual void selectNextIdleWorker( void );

	virtual void recreateControlBar( void );

	virtual void disableTooltipsUntil(UnsignedInt frameNum);
	virtual void clearTooltipsDisabled();
	virtual Bool areTooltipsDisabled() const;

	Bool getDrawRMBScrollAnchor() const { return m_drawRMBScrollAnchor; }
	Bool getMoveRMBScrollAnchor() const { return m_moveRMBScrollAnchor; }

	void setDrawRMBScrollAnchor(Bool b) { m_drawRMBScrollAnchor = b; }
	void setMoveRMBScrollAnchor(Bool b) { m_moveRMBScrollAnchor = b; }

private:
	virtual Int getIdleWorkerCount( void );
	virtual Object *findIdleWorker( Object *obj);
	virtual void showIdleWorkerLayout( void );
	virtual void hideIdleWorkerLayout( void );
	virtual void updateIdleWorker( void );
	virtual void resetIdleWorker( void );

public:
	void registerWindowLayout(WindowLayout *layout); // register a layout for updates
	void unregisterWindowLayout(WindowLayout *layout); // stop updates for this layout

  void triggerDoubleClickAttackMoveGuardHint( void );
  

public:
	// World 2D animation methods
	void addWorldAnimation( Anim2DTemplate *animTemplate, 
													const Coord3D *pos,
													WorldAnimationOptions options,
													Real durationInSeconds,
													Real zRisePerSecond );

#if defined(_DEBUG) || defined(_INTERNAL)
	virtual void DEBUG_addFloatingText(const AsciiString& text,const Coord3D * pos, Color color);
#endif

protected:
	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

protected: 

	// ----------------------------------------------------------------------------------------------
	// Protected Types ------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	enum HintType
	{
		MOVE_HINT = 0,
		ATTACK_HINT,
#ifdef _DEBUG
		DEBUG_HINT,
#endif
		NUM_HINT_TYPES  // keep this one last
	};

	// mouse mode interface
	enum MouseMode 
	{
		MOUSEMODE_DEFAULT = 0,
		MOUSEMODE_BUILD_PLACE,
		MOUSEMODE_GUI_COMMAND,
		MOUSEMODE_MAX
	};

	enum { MAX_MOVE_HINTS = 256 };
	struct MoveHintStruct
	{
		Coord3D pos;						///< World coords of destination point
		UnsignedInt sourceID;		///< id of who will move to this point
		UnsignedInt frame;			///< frame the command was issued on
	};

	struct UIMessage
	{
		UnicodeString fullText;									///< the whole text message
		DisplayString *displayString;						///< display string used to render the message
		UnsignedInt timestamp;									///< logic frame message was created on
		Color color;														///< color to render this in
	};
	enum { MAX_UI_MESSAGES = 6 };

	struct MilitarySubtitleData
	{
		UnicodeString subtitle;										///< The complete subtitle to be drawn, each line is separated by L"\n"
		UnsignedInt index;												///< the current index that we are at through the sibtitle
		ICoord2D position;												///< Where on the screen the subtitle should be drawn
		DisplayString *displayStrings[MAX_SUBTITLE_LINES];	///< We'll only allow MAX_SUBTITLE_LINES worth of display strings
		UnsignedInt currentDisplayString;					///< contains the current display string we're on. (also lets us know the last display string allocated
		UnsignedInt lifetime;											///< the Lifetime of the Military Subtitle in frames
		Bool blockDrawn;													///< True if the block is drawn false if it's blank
		UnsignedInt blockBeginFrame;							///< The frame at which the block started it's current state
		ICoord2D blockPos;												///< where the upper left of the block should begin
		UnsignedInt incrementOnFrame;							///< if we're currently on a frame greater then this, increment our position
		Color color;															///< what color should we display the military subtitles
	};

	typedef std::list<Object *> ObjectList;
	typedef std::list<Object *>::iterator ObjectListIt;
	
	// ----------------------------------------------------------------------------------------------
	// Protected Methods ----------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	void destroyPlacementIcons( void );													///< Destroy placement icons
	void handleBuildPlacements( void );													///< handle updating of placement icons based on mouse pos
	void handleRadiusCursor();																	///< handle updating of "radius cursors" that follow the mouse pos

	void incrementSelectCount( void ) { ++m_selectCount; }			///< Increase by one the running total of "selected" drawables
	void decrementSelectCount( void ) { --m_selectCount; }			///< Decrease by one the running total of "selected" drawables
	virtual View *createView( void ) = 0;												///< Factory for Views
	void evaluateSoloNexus( Drawable *newlyAddedDrawable = NULL );

	/// expire a hint from of the specified type at the hint index
	void expireHint( HintType type, UnsignedInt hintIndex );

	void createControlBar( void );			///< create the control bar user interface
	void createReplayControl( void );		///< create the replay control window

	void setMouseCursor(Mouse::MouseCursor c);

	
	void addMessageText( const UnicodeString& formattedMessage, const RGBColor *rgbColor = NULL );  ///< internal workhorse for adding plain text for messages
	void removeMessageAtIndex( Int i );				///< remove the message at index i

	void updateFloatingText( void );						///< Update function to move our floating text
	void drawFloatingText( void );							///< Draw all our floating text
	void clearFloatingText( void );							///< clear the floating text list

public:
	/** The pathfinder's build grid under a pending structure.  Public and called from the terrain
		* renderer rather than from preDraw: it is geometry lying on the ground, and it has to go down
		* with the terrain so that everything drawn after the terrain covers it. */
	virtual void drawBuildGrid( void ) { }
protected:

	void clearWorldAnimations( void );					///< delete all world animations
	void updateAndDrawWorldAnimations( void );	///< update and draw visible world animations

	SuperweaponInfo* findSWInfo(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate);

	// ----------------------------------------------------------------------------------------------
	// Protected Data THAT IS SAVED/LOADED ----------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	Bool												m_superweaponHiddenByScript;
	Bool												m_inputEnabled;		/// sort of

	// ----------------------------------------------------------------------------------------------
	// Protected Data -------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	std::list<WindowLayout *>		m_windowLayouts;
	AsciiString									m_currentlyPlayingMovie;											///< Used to push updates to TheScriptEngine
	DrawableList								m_selectedDrawables;													///< A list of all selected drawables.
	DrawableList								m_selectedLocalDrawables;											///< A list of all selected drawables owned by the local player
	Bool												m_isDragSelecting;														///< If TRUE, an area selection is in progress
	IRegion2D										m_dragSelectRegion;														///< if isDragSelecting is TRUE, this contains select region
	Bool												m_displayedMaxWarning;                        ///< keeps the warning from being shown over and over
	MoveHintStruct							m_moveHint[ MAX_MOVE_HINTS ];
	Int													m_nextMoveHint;
	const CommandButton *				m_pendingGUICommand;										///< GUI command that needs additional interaction from the user
	BuildProgress								m_buildProgress[ MAX_BUILD_PROGRESS ];	///< progress for building units
	const ThingTemplate *				m_pendingPlaceType;											///< type of built thing we're trying to place
	ObjectID										m_pendingPlaceSourceObjectID;						///< source object of the thing constructing the item
	Bool										m_preventLeftClickDeselectionInAlternateMouseModeForOneClick;
	Drawable **									m_placeIcon;														///< array for drawables to appear at the cursor when building in the world
	Bool												m_placeAnchorInProgress;								///< is place angle interface for placement active
	ICoord2D										m_placeAnchorStart;											///< place angle anchor start
	ICoord2D										m_placeAnchorEnd;												///< place angle anchor end
	Real												m_placeAngleOffset;											///< wheel-chosen heading, added to every structure's own view angle
	const ThingTemplate *				m_placeAngleType;												///< the structure that heading was chosen for; another type starts square again
	Bool												m_placementLegal;												///< last legality verdict for the spot under the structure being placed
	Coord3D											m_placementNudge;												///< how far the last legality check had to slide the structure to make it fit

	/// a structure ordered here, and the logic frame it was ordered on - see recordPendingPlacement
	struct PendingPlacement
	{
		Region2D		footprint;
		UnsignedInt	frame;				///< 0 for a slot nothing has been written to yet
	};
	PendingPlacement						m_pendingPlacement[ PENDING_PLACEMENTS ];
	Int													m_pendingPlacementAt;										///< where the next order is written, round the ring

	Int													m_selectCount;													///< Number of objects currently "selected"
	Int													m_maxSelectCount;												///< Max number of objects to select
	UnsignedInt									m_frameSelectionChanged;								///< Frame when the selection last changed.

  Int                         m_duringDoubleClickAttackMoveGuardHintTimer; ///< Frames left to draw the doubleClickFeedbackTimer 
  Coord3D                     m_duringDoubleClickAttackMoveGuardHintStashedPosition; 
  
	// Video playback data
	VideoBuffer*								m_videoBuffer;			///< video playback buffer
	VideoStreamInterface*				m_videoStream;			///< Video stream;

	// Video playback data
	VideoBuffer*								m_cameoVideoBuffer;///< video playback buffer
	VideoStreamInterface*				m_cameoVideoStream;///< Video stream;

	// message data
	UIMessage										m_uiMessages[ MAX_UI_MESSAGES ];/**< messages to display to the user, the
																						array is organized with newer messages at
																						index 0, and increasing to older ones */
	// superweapon timer data
	SuperweaponMap							m_superweapons[MAX_PLAYER_COUNT];
	enum { HUD_OVERLAY_POINT_SIZE = 9 };	///< small: this sits over the battlefield, not in a panel
	enum { HUD_CLOCK_POINT_SIZE = 7 };		///< smaller still, and bold: the clock/rate plate is a glance, not a read

	void drawHudOverlay( void );					///< the small elapsed-time / fps plate (ShowHudOverlay)
	void drawIncomeRate( void );					///< income per minute, drawn beside the money window
	void updateIncomeEstimate( Player *player );	///< income per minute, shown beside the money
	void drawProductionStrip( void );			///< the production queue rows above the control bar
	///< one run of cells - a column while playing, a player's row while watching - with its left
	///< edge at 'left' and its first cell's top edge at 'bottomY'
	void drawProductionStripColumn( Int row, Int left, Int bottomY );
	const Image *productionStripTray( void );	///< the bar's tray, mirrored, kept until the bar changes side
	void stripTrayMetrics( ICoord2D *tray, ICoord2D *cameo, ICoord2D *hole, Int *step );	///< that tray's size, its cameo hole, and the column step
	void drawStripSeconds( Int which, Int x, Int y, Int w, Int h, Int seconds );	///< countdown written inside a cameo
	void drawStripQuantity( Int which, Int x, Int y, Int w, Int quantity );	///< the "xN" in a cameo's top right corner
	void addSuperweaponIcon( const Image *image, Int seconds, Int percent, Bool ready, Color color );
	void drawSuperweaponStrip( void );		///< those icons, top right, soonest at the right hand end

	Bool												m_placementRangeRingUp;	///< we put a radius cursor up for a pending structure, so we owe a clear
	Real												m_placementRingRadius;	///< the radius that ring was built at, so it is not rebuilt every frame

	DisplayString *							m_hudDisplayString;			///< the ShowHudOverlay line (fps / clock / income)
	DisplayString *							m_incomeDisplayString;	///< the "(+N/min)" drawn beside the money
	Int													m_lastIncomeDisplayed;	///< so that string is only rebuilt when the rate changes
	UnsignedInt									m_hudDrawCount;					///< rendered frames counted by drawHudOverlay itself
	UnsignedInt									m_hudLastSampleFrame;		///< m_hudDrawCount the fps sample was last refreshed on
	UnsignedInt									m_hudLastSampleMs;			///< wall clock of that sample
	Real												m_hudFps;								///< smoothed render rate
	UnsignedInt									m_hudLastSampleLogicFrame;	///< logic frame at that same sample
	Real												m_hudLogicHz;						///< logic frames actually simulated per real second
	UnsignedInt									m_hudRealClockBaseMs;		///< wall clock the two elapsed-time readouts were aligned at
	UnsignedInt									m_hudLastDrawMs;				///< wall clock of the previous overlay draw, so a pause can be taken back out of it
	Int													m_hudOverlayBottom;			///< bottom of that corner plate, so the superweapon timers start under it
	// A script time freeze stops the logic clock, and the military subtitle's counters are
	// logic frames, so they have to be stepped by hand while it lasts.  These two turn the
	// wall clock into that step without drift: every update works out how many 30Hz frames
	// have passed since the freeze began and applies only the ones not applied yet.
	UnsignedInt									m_subtitleFreezeStartMs;	///< wall clock the current freeze started on, 0 = not frozen
	UnsignedInt									m_subtitleFreezeSteps;		///< frames already handed to the subtitle during it
	// Income is sampled from the player's cumulative earnings every INCOME_SAMPLE_SECONDS and
	// averaged over the whole ring, so a lumpy supply run reads as a rate instead of a spike.
	enum { INCOME_SAMPLES = 16, INCOME_SAMPLE_SECONDS = 2 };	///< 16 buckets of 2s = a 30s window
	Int													m_incomeSamples[ INCOME_SAMPLES ];	///< cumulative money earned, one per bucket
	UnsignedInt									m_incomeSampleCount;		///< buckets taken so far, the ring index is this mod INCOME_SAMPLES
	Int													m_incomeSamplePlayer;		///< whose earnings those are, so an observer switching players restarts
	UnsignedInt									m_hudLastMoneyFrame;		///< logic frame of the last sample
	Int													m_hudIncomePerMin;			///< most recent income estimate, cash per minute, -1 until the window is warm

	//
	// The global production strip: everything the local player has coming - queued in any factory,
	// or going up on the ground - one cameo each, soonest to finish first, in a column standing on
	// the corner above the control bar. drawProductionStrip() lays it out and records where each
	// cameo landed; handleProductionStripClick() reads those back.
	//
	ProductionStripSlot					m_productionStrip[ PRODUCTION_STRIP_ROWS ][ PRODUCTION_STRIP_ROW_MAX ];
	Int													m_productionStripCount[ PRODUCTION_STRIP_ROWS ];	///< cameos drawn per row
	Int													m_productionStripTotal[ PRODUCTION_STRIP_ROWS ];	///< items queued per row, drawn or not
	Color												m_productionStripRowColor[ PRODUCTION_STRIP_ROWS ];	///< whose row it is, 0 for the local player's own
	Bool												m_productionStripWatching;	///< the rows are every player's, not ours
	Int													m_productionStripCameoW;		///< cameo size this frame, in the control bar's aspect
	Int													m_productionStripCameoH;

	//
	// The strip's cameos face the other way from the power bar's, so the tray behind them is a
	// mirrored copy of it.  The whole strip wears one side's metal - the side the bar underneath it
	// is showing - so one is kept at a time and remade when the bar changes side, which watching a
	// match it does every time the observer picks a different player out of the list.
	//
	Image *											m_productionStripTray;			///< our mirrored copy of the bar's tray
	const Image *								m_productionStripTraySource;	///< the tray it was made from, so a side change remakes it
	//
	// One kept string per cameo, not one string walked across all of them.  A DisplayString
	// rebuilds its sentence whenever the text it holds changes, and a rebuild is a fresh font
	// surface, a fresh texture and a surface copy - so a single shared string paid that once per
	// cameo per frame, and watching a match the strips cost more to draw than the terrain under
	// them.  Kept per cameo, a countdown rebuilds when its own second changes: once a second.
	//
	enum { STRIP_SECONDS_STRINGS = PRODUCTION_STRIP_ROWS * PRODUCTION_STRIP_ROW_MAX
																 + SUPERWEAPON_STRIP_MAX };
	enum { STRIP_OVERFLOW_STRINGS = PRODUCTION_STRIP_ROWS + 1 };	///< one per production row, plus the superweapon strip's

	enum { STRIP_QUANTITY_STRINGS = PRODUCTION_STRIP_ROWS * PRODUCTION_STRIP_ROW_MAX };

	DisplayString *							m_productionStripOverflow[ STRIP_OVERFLOW_STRINGS ];	///< the "+N" that stands for the rest of a row
	DisplayString *							m_stripSecondsString[ STRIP_SECONDS_STRINGS ];			///< the countdown written inside a cameo, either strip's
	DisplayString *							m_stripQuantityString[ STRIP_QUANTITY_STRINGS ];		///< the "xN" a run of the same item wears

	//
	// The superweapon strip: the same cameos the command bar fires them from, in the top right
	// under the clock plate, rebuilt every frame from the timers below and laid out soonest first.
	//
	SuperweaponIconSlot					m_superweaponIcons[ SUPERWEAPON_STRIP_MAX ];
	Int													m_superweaponIconCount;		///< icons drawn
	Int													m_superweaponIconTotal;		///< timers live, drawn or not; the difference is the "+N"

	Coord2D											m_superweaponPosition;
	Real												m_superweaponFlashDuration;
	
	// superweapon timer font info
	AsciiString									m_superweaponNormalFont;
	Int													m_superweaponNormalPointSize;
	Bool												m_superweaponNormalBold;
	AsciiString									m_superweaponReadyFont;
	Int													m_superweaponReadyPointSize;
	Bool												m_superweaponReadyBold;

	Int													m_superweaponLastFlashFrame;										///< for flashing the text when the weapon is ready
	Color												m_superweaponFlashColor;
	Bool												m_superweaponUsedFlashColor;

	NamedTimerMap								m_namedTimers;
	Coord2D											m_namedTimerPosition;
	Real												m_namedTimerFlashDuration;
	Int													m_namedTimerLastFlashFrame;
	Color												m_namedTimerFlashColor;
	Bool												m_namedTimerUsedFlashColor;
	Bool												m_showNamedTimers;

	AsciiString									m_namedTimerNormalFont;
	Int													m_namedTimerNormalPointSize;
	Bool												m_namedTimerNormalBold;
	Color												m_namedTimerNormalColor;
	AsciiString									m_namedTimerReadyFont;
	Int													m_namedTimerReadyPointSize;
	Bool												m_namedTimerReadyBold;
	Color												m_namedTimerReadyColor;

	// Drawable caption data
	AsciiString									m_drawableCaptionFont;
	Int													m_drawableCaptionPointSize;
	Bool												m_drawableCaptionBold;
	Color												m_drawableCaptionColor;

	UnsignedInt									m_tooltipsDisabledUntil;

	// Military Subtitle data
	MilitarySubtitleData *			m_militarySubtitle;		///< The pointer to subtitle class, if it's present then draw it.
	Bool												m_isScrolling;
	Bool												m_isSelecting;
	MouseMode										m_mouseMode;
	Int													m_mouseModeCursor;
	DrawableID									m_mousedOverDrawableID;
	Coord2D											m_scrollAmt;
	Bool												m_isQuitMenuVisible;
	Bool												m_messagesOn;

	Color												m_messageColor1;
	Color												m_messageColor2;
	ICoord2D										m_messagePosition;
	AsciiString									m_messageFont;
	Int													m_messagePointSize;
	Bool												m_messageBold;
	Int													m_messageDelayMS;

	RGBAColorInt								m_militaryCaptionColor;				///< color for the military-style caption
	ICoord2D										m_militaryCaptionPosition;					///< position for the military-style caption

	AsciiString									m_militaryCaptionTitleFont;
	Int													m_militaryCaptionTitlePointSize;
	Bool												m_militaryCaptionTitleBold;

	AsciiString									m_militaryCaptionFont;
	Int													m_militaryCaptionPointSize;
	Bool												m_militaryCaptionBold;

	Bool												m_militaryCaptionRandomizeTyping;
	Int													m_militaryCaptionSpeed;

	RadiusDecalTemplate					m_radiusCursors[RADIUSCURSOR_COUNT];
	RadiusDecal									m_curRadiusCursor;
	RadiusCursorType						m_curRcType;

	//Floating Text Data
	FloatingTextList						m_floatingTextList;				///< Our list of floating text
	UnsignedInt									m_floatingTextTimeOut;									///< Ini value of our floating text timeout
	Real												m_floatingTextMoveUpSpeed;							///< INI value of our Move up speed
	Real												m_floatingTextMoveVanishRate;					///< INI value of our move vanish rate

	PopupMessageData *					m_popupMessageData;
	Color												m_popupMessageColor;
	
 	Bool												m_waypointMode;			///< are we in waypoint plotting mode?
	Bool												m_forceAttackMode;		///< are we in force attack mode?
	Bool												m_forceMoveToMode;		///< are we in force move mode?
	Bool												m_attackMoveToMode;	///< are we in attack move mode?
	Bool												m_preferSelection;		///< the shift key has been depressed.

	// wall clock of the previous update(), so a held camera key can be stepped by elapsed
	// time instead of by render frames.  0 = no previous update yet.
	UnsignedInt									m_cameraKeyLastMs;

	// wall clock of the last quantized rotate step under SnapCameraRotateTo45.  0 = step now.
	UnsignedInt									m_cameraSnapRepeatMs;

	Bool												m_cameraRotatingLeft; 
	Bool 												m_cameraRotatingRight;
	Bool 												m_cameraZoomingIn;
	Bool 												m_cameraTrackingDrawable;
	Bool 												m_cameraZoomingOut;
	
	Bool												m_drawRMBScrollAnchor;
	Bool												m_moveRMBScrollAnchor;
	Bool												m_clientQuiet;         ///< When the user clicks exit,restart, etc. this is set true 
																												///< to skip some client sounds/fx during shutdown

	// World Animation Data
	WorldAnimationList					m_worldAnimationList;		///< the list of world animations

	// Idle worker animation
	ObjectList									m_idleWorkers[MAX_PLAYER_COUNT];
	GameWindow *								m_idleWorkerWin;
	Int													m_currentIdleWorkerDisplay;

	DrawableID									m_soloNexusSelectedDrawableID;  ///< The drawable of the nexus, if only one angry mob is selected, otherwise, null

	// ----------------------------------------------------------------------------------------------
	// STATIC Protected Data -------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	static const FieldParse s_fieldParseTable[];

};

// the singleton
extern InGameUI *TheInGameUI;

#endif // _IN_GAME_UI_H_
