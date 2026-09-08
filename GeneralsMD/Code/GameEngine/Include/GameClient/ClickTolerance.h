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

// ClickTolerance.h ///////////////////////////////////////////////////////////////////////////////
// Whether a press and a release were close enough to count as a click rather than a drag.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _CLICK_TOLERANCE_H_
#define _CLICK_TOLERANCE_H_

#include "Lib/BaseType.h"

/**
	* A click is a press and a release that are close together in three senses: in time, in where
	* the pointer sat on the screen, and in where the camera sat in the world.  The third one is
	* what a fast scroll breaks - the pointer can hold still while the world slides under it, and
	* the two ends of the click then mean different places on the map.
	*
	* Distances are compared as radii.  Testing each screen axis against the tolerance separately
	* let a diagonal drag of 1.41 times the tolerance still read as a click.
	*/
inline Bool ClickTolerance_isClick( Real screenDeltaX, Real screenDeltaY, Real cameraDelta,
																		UnsignedInt elapsedMilliseconds,
																		Real screenTolerance, Real cameraTolerance,
																		UnsignedInt millisecondTolerance )
{
	if( elapsedMilliseconds > millisecondTolerance )
		return FALSE;

	if( screenDeltaX * screenDeltaX + screenDeltaY * screenDeltaY > screenTolerance * screenTolerance )
		return FALSE;

	if( cameraDelta > cameraTolerance )
		return FALSE;

	return TRUE;
}

#endif // _CLICK_TOLERANCE_H_
