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

// FILE: DrawnPath.h ////////////////////////////////////////////////////////////////////////////
//
// The curve the player traces with the right button, and the arithmetic that turns it into
// standing room.  Both sides of the engine need the same answers: the logic to decide who goes
// where, the client to draw it while the line is still being dragged.  Written once here so the
// two pictures cannot drift apart.
//
///////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _H_DrawnPath
#define _H_DrawnPath

#include "Lib/BaseType.h"
#include "Common/STLTypedefs.h"

class Object;

/// running length of the curve up to each of its points; arc[0] is zero, arc.back() is the whole
extern void buildPathArcLengths( const std::vector<Coord3D>& path, std::vector<Real>& arc );

/// how far along the curve the point on it nearest to (x,y) sits
extern Real distanceAlongPath( const std::vector<Coord3D>& path, const std::vector<Real>& arc,
															 Real x, Real y );

/// the point that far along the curve; the ground height is not filled in
extern void pointAlongPath( const std::vector<Coord3D>& path, const std::vector<Real>& arc,
														Real dist, Coord3D *out );

/**
 * Put these units in the order the stations are handed out: whoever is nearest the start of the
 * curve takes the first one, so nobody crosses anybody on the way in.  The object id breaks a tie,
 * which makes the order a total one - the same set of units sorts the same way on every machine
 * and in the client's own picture of it, whatever order they arrived in.
 */
extern void orderAlongPath( std::vector<Object *>& movers, const std::vector<Coord3D>& path,
														const std::vector<Real>& arc );

#endif // _H_DrawnPath
