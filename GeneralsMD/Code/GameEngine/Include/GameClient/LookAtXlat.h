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

// FILE: LookAtXlat.h ///////////////////////////////////////////////////////////
// Author: Steven Johnson, Dec 2001

#pragma once

#ifndef _H_LookAtXlat
#define _H_LookAtXlat

#include "GameClient/InGameUI.h"

//-----------------------------------------------------------------------------
class LookAtTranslator : public GameMessageTranslator
{
public:
	LookAtTranslator();
	~LookAtTranslator();
	virtual GameMessageDisposition translateGameMessage(const GameMessage *msg);
	/// where the middle-button scroll was started from, or NULL if no such scroll is running
	const ICoord2D* getScrollAnchor( void );
	Bool hasMouseMovedRecently( void );
	void setCurrentPos( const ICoord2D& pos );

	void resetModes(); //Used when disabling input, so when we reenable it we aren't stuck in a mode.

private:
	//
	// ZoomToCursor: the wheel only moves the camera's *desired* height, and W3DView::update eases
	// the real zoom towards it over the frames that follow, so the world point under the cursor is
	// held in place by re-measuring it every frame for as long as that easing lasts.
	//
	enum
	{
		ZOOM_ANCHOR_MAX_TICKS = 240,	///< hard budget, so a stalled camera cannot pin the view forever
		ZOOM_ANCHOR_GRACE_TICKS = 4		///< ticks before a still zoom is allowed to end the pin
	};
	void updateZoomToCursor( void );	///< keep the anchored world point under its pixel
	Bool m_zoomAnchorValid;						///< a wheel zoom is being followed
	ICoord2D m_zoomAnchorPixel;				///< the pixel the wheel was spun over
	Coord3D m_zoomAnchorWorld;				///< the terrain point that has to stay under it
	Real m_zoomAnchorZoom;						///< last zoom seen, to tell whether the camera is still easing
	Int m_zoomAnchorTicks;						///< ticks left in the budget

	enum 
	{
		MAX_VIEW_LOCS = 8
	};
	// The right button used to be in here as SCROLL_RMB.  It is not any more: the right button
	// gives orders now, and the camera drag lives on the middle one.
	enum
	{
		SCROLL_NONE = 0,
		SCROLL_KEY,
		SCROLL_SCREENEDGE,
		SCROLL_MMB				// middle-button drag pan
	};
	ICoord2D m_anchor;
	ICoord2D m_originalAnchor;
	ICoord2D m_currentPos;									
	Bool m_isScrolling;				// set to true if we are in the act of RMB scrolling
	Bool m_isRotating;					// set to true if we are in the act of MMB rotating
	Real m_freeRotateAngle;		// heading the drag has asked for, before SnapCameraRotateTo45 quantizes it
	Bool m_isPitching;					// set to true if we are in the act of ALT pitch rotation
	Bool m_isChangingFOV;			// set to true if we are in the act of changing the field of view
	UnsignedInt m_timestamp;				// set when button goes down
	DrawableID m_lastPlaneID;
	ViewLocation m_viewLocation[ MAX_VIEW_LOCS ];
	Int m_scrollType;
	void setScrolling( Int );
	void stopScrolling( void );
	UnsignedInt m_lastMouseMoveFrame;
};	

extern LookAtTranslator *TheLookAtTranslator;

#endif
