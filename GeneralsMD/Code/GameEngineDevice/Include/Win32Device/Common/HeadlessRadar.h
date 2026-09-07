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

// FILE: HeadlessRadar.h /////////////////////////////////////////////////////////////////////////
//
// The radar for a run with no render device.
//
// W3DRadar is three textures it paints the terrain, the blips and the shroud into, and every one of
// its methods reaches for one of them.  Under -nodevice there is no device to hold them, and
// guarding thirty methods one at a time would be thirty chances to miss one.  The engine already
// has the seam for this: Win32GameEngine picks the concrete class behind every abstract interface,
// so a run with no picture gets a radar that keeps the half logic cares about - the events, the
// under-attack pulse, the object lists - and draws nothing.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __HEADLESSRADAR_H_
#define __HEADLESSRADAR_H_

#include "Common/Radar.h"

//-------------------------------------------------------------------------------------------------
class HeadlessRadar : public Radar
{

public:

	HeadlessRadar( void ) { }
	virtual ~HeadlessRadar( void ) { }

	/// nothing is drawn, so there is nothing to draw with
	virtual void draw( Int pixelX, Int pixelY, Int width, Int height ) { }

	/// the shroud lives in a texture that was never made; logic keeps its own in PartitionManager
	virtual void clearShroud( void ) { }
	virtual void setShroudLevel( Int x, Int y, CellShroudStatus setting ) { }

};

#endif // __HEADLESSRADAR_H_
