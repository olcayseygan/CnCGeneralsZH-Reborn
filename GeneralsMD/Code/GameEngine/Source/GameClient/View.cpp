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

// View.cpp ///////////////////////////////////////////////////////////////////
// A "view", or window, into the World
// Author: Michael S. Booth, February 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "Common/Xfer.h"
#include "GameClient/View.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"

UnsignedInt View::m_idNext = 1;

// the tactical view singleton
View *TheTacticalView = NULL;

//-------------------------------------------------------------------------------------------------
/** W3D's own default cone, and the one every camera number in the game was authored against. */
static const Real VIEW_DESIGN_HFOV = 50.0f * PI / 180.0f;

/** The widest screen that keeps that cone whole.  16:9 is where the shipped game is played and
	* where the fork's own screenshots are taken, so it is the shape nothing may move under. */
static const Real VIEW_REFERENCE_ASPECT = 16.0f / 9.0f;

//-------------------------------------------------------------------------------------------------
Real ViewHorizontalFovForScreen( Int displayWidth, Int displayHeight )
{
	if( displayWidth <= 0 || displayHeight <= 0 )
		return VIEW_DESIGN_HFOV;

	const Real aspect = (Real)displayWidth / (Real)displayHeight;
	if( aspect <= VIEW_REFERENCE_ASPECT )
		return VIEW_DESIGN_HFOV;

	//
	// Past 16:9 the horizontal cone opens instead of the vertical one closing.  Retail pins the
	// horizontal angle and lets the vertical follow the aspect, which on a 5120x1440 screen leaves
	// 14.9 degrees of world from top to bottom against 29.4 at 16:9 - the same building fills the
	// same slice of the screen's *width* and twice as much of its height, so an ultrawide shot and
	// a 16:9 shot of the same camera look like two different zoom levels.  Holding the vertical
	// half-angle at its 16:9 value is what makes the two overlay.
	//
	const Real verticalHalf = (Real)tan( VIEW_DESIGN_HFOV * 0.5f ) / VIEW_REFERENCE_ASPECT;
	return 2.0f * (Real)atan( verticalHalf * aspect );
}


View::View( void )
{
	//Added By Sadullah Nader
	//Initialization(s) inserted
	m_currentHeightAboveGround = 0.0f;
	m_defaultAngle = 0.0f;
	m_defaultPitchAngle = 0.0f;
	m_heightAboveGround = 0.0f;
	m_lockDist = 0.0f;
	m_maxHeightAboveGround = 0.0f;
	m_minHeightAboveGround = 0.0f;
	m_viewLockedUntilFrame = 0;
	m_next = NULL;
	m_okToAdjustHeight = TRUE;
	m_originX = 0;
	m_originY = 0;
	m_snapImmediate = FALSE;
	m_terrainHeightUnderCamera = 0.0f;
	m_zoom = 0.0f;
	//
	m_pos.x = 0;
	m_pos.y = 0;
	m_width = 0;
	m_height = 0;
	m_angle = 0.0f;
	m_pitchAngle = 0.0f;
	m_cameraLock = INVALID_ID;
	m_cameraLockDrawable = NULL;
	m_zoomLimited = TRUE;

	// create unique view ID
	m_id = m_idNext++;

	// default field of view
	m_FOV = 50.0f * PI/180.0f;
	
	m_mouseLocked = FALSE;
	
	m_guardBandBias.x = 0.0f;
	m_guardBandBias.y = 0.0f;
}

View::~View()
{
}

void View::init( void )
{
	m_width = DEFAULT_VIEW_WIDTH;
	m_height = DEFAULT_VIEW_HEIGHT;
	m_originX = DEFAULT_VIEW_ORIGIN_X;
	m_originY = DEFAULT_VIEW_ORIGIN_Y;
	m_pos.x = 0;
	m_pos.y = 0;
	m_angle = 0.0f;
	m_cameraLock = INVALID_ID;
	m_cameraLockDrawable = NULL;
	m_zoomLimited = TRUE;
	
	m_zoom = 1.0f;
	m_maxHeightAboveGround = TheGlobalData->m_maxCameraHeight;
	m_minHeightAboveGround = TheGlobalData->m_minCameraHeight;
	m_okToAdjustHeight = FALSE;

	m_defaultAngle = 0.0f;
	m_defaultPitchAngle = 0.0f;
}

void View::reset( void )
{
	// Only fixing the reported bug.  Who knows what side effects resetting the rest could have.
	m_zoomLimited = TRUE;

	m_viewLockedUntilFrame = 0;
}

void View::lockViewForOneFrame()
{
	m_viewLockedUntilFrame = TheGameClient->getFrame() + 1;
}

/**
 * Prepend this view to the given list, return the new list.
 */
View *View::prependViewToList( View *list )
{
	m_next = list;
	return this;
}

// One "step" is what a single mouse wheel notch does, and what one 30Hz frame of a held
// zoom key does; a caller driving this from a render frame passes the fraction of a logic
// frame that has actually gone by, so the zoom rate stops depending on the frame rate.
//
// EA's step was 10 world units.  The reachable band is roughly 100 (MinCameraHeight) to 480
// (MaxCameraHeight * ZOOM_OUT_LIMIT_FACTOR), so crossing it took about 38 notches - most of a
// wheel's travel to go from the ground to the whole map.  60 puts it at about six.
#define ZOOM_STEP_HEIGHT (60.0f)

void View::zoomIn( Real steps )
{
	setHeightAboveGround(getHeightAboveGround() - ZOOM_STEP_HEIGHT * steps);
}

void View::zoomOut( Real steps )
{
	setHeightAboveGround(getHeightAboveGround() + ZOOM_STEP_HEIGHT * steps);
}

/**
 * Center the view on the given coordinate.
 */
void View::lookAt( const Coord3D *o ) 
{ 

	/// @todo this needs to be changed to be 3D, this is still old 2D stuff
	Coord3D pos = *getPosition();
	pos.x = o->x - m_width * 0.5f; 
	pos.y = o->y - m_height * 0.5f; 
	setPosition(&pos);
}

/**
 * Shift the view by the given delta.
 */
void View::scrollBy( Coord2D *delta ) 
{ 
	// update view's world position
	m_pos.x += delta->x;
	m_pos.y += delta->y;
}

/**
 * SnapCameraRotateTo45: the nearest multiple of 45 degrees.  floor(x + 0.5) is the round for both
 * signs - the earlier copies of this used floor(x - 0.5) below zero, which is not the nearest one
 * but the next one away from zero, so a heading of -5 degrees snapped to -45 instead of 0.
 */
Real View_snapAngleToEighth( Real angle )
{
	const Real step = PI / 4.0f;
	return ((Real)REAL_TO_INT_FLOOR( angle / step + 0.5f )) * step;
}

/**
 * SnapCameraRotateTo45: the eighth that is 'steps' away from the one this angle is nearest to.
 */
Real View_stepAngleByEighths( Real angle, Int steps )
{
	const Real step = PI / 4.0f;
	return View_snapAngleToEighth( angle ) + steps * step;
}

/**
 * Rotate the view around the up axis by the given angle.
 */
void View::setAngle( Real angle )
{
	m_angle = angle; 
}

/**
 * Rotate the view around the horizontal (X) axis to the given angle.
 */
void View::setPitch( Real angle )
{
	m_pitchAngle = angle;

	Real limit = PI/5.0f;

	if (m_pitchAngle < -limit)
		m_pitchAngle = -limit;
	else if (m_pitchAngle > limit)
		m_pitchAngle = limit;
}

/**
 * Set the view angle back to default
 */
void View::setAngleAndPitchToDefault( void )
{ 
	m_angle = m_defaultAngle;
	m_pitchAngle = m_defaultPitchAngle;
}

/**
 * write the view's current location in to the view location object
 */
void View::getLocation( ViewLocation *location )
{

	const Coord3D *pos = getPosition();
	location->init( pos->x, pos->y, pos->z, getAngle(), getPitch(), getZoom() );

}


/**
 * set the view's current location from to the view location object
 */
void View::setLocation( const ViewLocation *location )
{
	if ( location->m_valid )
	{
		setPosition(&location->m_pos);
		setAngle(location->m_angle);
		setPitch(location->m_pitch);
		setZoom(location->m_zoom);
		forceRedraw();
	}

}

//-------------------------------------------------------------------------------------------------
/** project the 4 corners of this view into the world and return each point as a parameter,
		the world points are at the requested Z */
//-------------------------------------------------------------------------------------------------
void View::getScreenCornerWorldPointsAtZ( Coord3D *topLeft, Coord3D *topRight,
																					Coord3D *bottomLeft, Coord3D *bottomRight,
																					Real z )
{
	ICoord2D screenTopLeft, screenTopRight, screenBottomLeft, screenBottomRight;
	ICoord2D origin;
	Int viewWidth = getWidth();
	Int viewHeight = getHeight();

	// sanity
	if( topLeft == NULL || topRight == NULL || bottomLeft == NULL || bottomRight == NULL )
		return;

	// setup the screen coords for the 4 corners of the viewable display
	getOrigin( &origin.x, &origin.y );
	screenTopLeft.x     = origin.x;								// upper left
	screenTopLeft.y     = origin.y;								// upper left
	screenTopRight.x    = origin.x + viewWidth;		// upper right
	screenTopRight.y    = origin.y;								// upper right
	screenBottomLeft.x  = origin.x + viewWidth;		// lower right
	screenBottomLeft.y  = origin.y + viewHeight;  // lower right
	screenBottomRight.x = origin.x;								// lower left
	screenBottomRight.y = origin.y + viewHeight;	// lower left

	// project
	screenToWorldAtZ( &screenTopLeft, topLeft, z );
	screenToWorldAtZ( &screenTopRight, topRight, z );
	screenToWorldAtZ( &screenBottomLeft, bottomLeft, z );
	screenToWorldAtZ( &screenBottomRight, bottomRight, z );

}  // end getScreenCornerWorldPointsAtZ

// ------------------------------------------------------------------------------------------------
/** Xfer method for a view */
// ------------------------------------------------------------------------------------------------
void View::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// camera angle
	Real angle = getAngle();
	xfer->xferReal( &angle );
	setAngle( angle );

	// view position
	Coord3D viewPos;
	getPosition( &viewPos );
	xfer->xferReal( &viewPos.x );
	xfer->xferReal( &viewPos.y );
	xfer->xferReal( &viewPos.z );
	lookAt( &viewPos );

}  // end xfer
