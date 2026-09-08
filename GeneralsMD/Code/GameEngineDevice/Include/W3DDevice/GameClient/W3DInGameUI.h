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

// FILE: W3DInGameUI.h ////////////////////////////////////////////////////////
//
// W3D implementation of the in game user interface, the in game ui is 
// responsible for 
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DINGAMEUI_H_
#define __W3DINGAMEUI_H_

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "W3DDevice/GameClient/W3DView.h"
#include "WW3D2/Render2D.h"
#include "WW3D2/RendObj.h"
#include "WW3D2/Line3D.h"

class HAnimClass;

// W3DInGameUI ----------------------------------------------------------------
/** Implementation for the W3D game user interface.  This singleton is 
  * responsible for all user interaction and feedback with as part of the
	* user interface */
//-----------------------------------------------------------------------------
class W3DInGameUI : public InGameUI
{

public:

	W3DInGameUI();
	virtual ~W3DInGameUI();

	// Inherited from subsystem interface -----------------------------------------------------------
	virtual	void init( void );		///< Initialize the in-game user interface
	virtual void update( void );	//< Update the UI by calling preDraw(), draw(), and postDraw()
	virtual void reset( void );		///< Reset
	//-----------------------------------------------------------------------------------------------

	virtual void draw( void ); ///< Render the in-game user interface

protected:

	/// factory for views
	virtual View *createView( void ) { return NEW W3DView; }

	virtual void drawSelectionRegion( void );			///< draw the selection region on screen
	virtual void drawAttackHints( View *view );		///< draw attack hint visual feedback
	virtual void drawPlaceAngle( View *view ); 		///< draw place building angle if needed
	virtual void drawBuildGrid( void );						///< the pathfinder's build grid under a pending structure
	virtual void drawAttackCircleFill( void );		///< the wash on the ground inside an attack circle
	virtual void drawFormationLine( void );				///< the line a right drag is spreading the selection along
	virtual void drawOrderHints( void );					///< a line from each selected unit to where its order sends it
	virtual void drawAttackCircle( void );				///< the circle a left drag is sweeping targets out of
	virtual void drawAllyCursorLights( void );		///< the patch of an ally's colour lying under their cursor
	void drawAllyCursors( void );									///< each ally's pointer and name, over the world

	RenderObjClass *m_buildingPlacementAnchor;
	RenderObjClass *m_buildingPlacementArrow;

	/// One name plate per player, built the first time that ally's cursor is drawn and kept, because
	/// laying out the same string every frame for every ally is work nobody sees.
	DisplayString *m_allyCursorNames[ MAX_PLAYER_COUNT ];

};  // end class W3DInGameUI

#endif  // end __W3DINGAMEUI_H_
