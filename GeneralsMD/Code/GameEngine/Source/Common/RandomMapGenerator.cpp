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

// RandomMapGenerator.cpp
// Writes a .map image straight into memory: the same CkMp chunk stream
// WorldBuilder saves (see WHeightMapEdit::saveToFile), minus everything a
// skirmish map does not need.
//
// The layout is built in one canonical sector and rotated into the others, so
// every player gets the same ground in their own frame and a win rate measured
// over these maps is not measuring the terrain.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <math.h>
#include <float.h>

#include "Common/RandomMapGenerator.h"
#include "Lib/Trig.h"
#include "Common/crc.h"
#include "Common/DataChunk.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/Dict.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "GameLogic/FPUControl.h"

// Chunk versions the writers of these chunks use. They live in the .cpp files
// that write them (SidesList.cpp, Scripts.cpp), not in MapReaderWriterInfo.h.
static const Int K_SIDES_DATA_VERSION_3 = 3;
static const Int K_SCRIPTS_DATA_VERSION_1 = 1;
static const Int K_SCRIPT_LIST_DATA_VERSION_1 = 1;

//-----------------------------------------------------------------------------
// The map we build
//-----------------------------------------------------------------------------

// Terrain outside the playable area. The camera looks across it at the map
// edges, so it is scenery, not play space.
#define RMG_BORDER_CELLS		20

// Four texture classes, each read as the four tiles of a 2x2 sheet. Named the
// way Terrain.ini names them, since the reader looks the texture up by name.
#define RMG_TILES_PER_CLASS		4
#define RMG_TILE_SHEET_WIDTH	2

// Height field, in height map bytes (a byte is MAP_HEIGHT_SCALE world units).
#define RMG_BASE_HEIGHT			40.0f
#define RMG_AMPLITUDE			20.0f
#define RMG_OCTAVES				4
#define RMG_FEATURES_PER_MAP	4.0f

// A start position gets a flat disc to build on, easing back into the terrain.
// Eight players on a small map sit close enough together that the full-size
// discs would overlap and flatten the whole map, so both radii are capped by
// the spacing between neighbouring starts as well.
#define RMG_FLAT_RADIUS			14.0f
#define RMG_BLEND_RADIUS		30.0f
#define RMG_FLAT_OF_SPACING		0.35f
#define RMG_BLEND_OF_SPACING	0.75f

// No ridge comes closer than this to a start, whatever the player count does to
// the geometry. A base has to fit, and the pathfinder has to be able to leave.
#define RMG_BASE_CLEARANCE		20.0f

// Where the start positions sit, as a fraction of the playable size.
#define RMG_START_RING			0.34f
// Supply docks per player, and how far from the start they sit, in cells.
#define RMG_SUPPLY_PER_PLAYER	2
#define RMG_SUPPLY_DISTANCE		20.0f

// The ridge that separates one player's ground from the next. It runs along the
// sector boundary, is raised in one step so both of its sides read as cliff to
// the pathfinder, and is cut by one gap - the chokepoint every crossing between
// two neighbours has to use.
#define RMG_RIDGE_RISE			34.0f	///< height bytes; a cliff needs more than 15.7
#define RMG_RIDGE_HALF_WIDTH	2.5f	///< cells to either side of the boundary
#define RMG_RIDGE_INNER			0.12f	///< fraction of the playable size
#define RMG_RIDGE_OUTER			0.46f
#define RMG_GAP_RADIUS			0.30f	///< where the chokepoint sits along the ridge
#define RMG_GAP_HALF			0.05f	///< half the gap, as a fraction of the playable size
#define RMG_GAP_WIDEN_STEP		1.4f	///< how much a failed connectivity check widens it
#define RMG_LAYOUT_ATTEMPTS		5

// One lake per player, off to the side of the lane rather than across it.
#define RMG_LAKE_MIN_CELLS		96
#define RMG_LAKE_RADIUS			0.045f	///< fraction of the playable size
#define RMG_LAKE_DOCK_CLEARANCE	8.0f	///< cells of dry land kept around a supply dock
#define RMG_LAKE_OF_SPACING		0.16f	///< and no wider than this much of the start spacing
#define RMG_LAKE_SHORE			4.0f	///< cells of shore the basin eases out over
#define RMG_LAKE_DEPTH			16.0f	///< height bytes below the water surface
#define RMG_WATER_DROP			7.0f	///< height bytes: water surface below the base height

// Scenery. The cap is per sector, so the map keeps the same object count per
// player whatever the seed does.
#define RMG_PROPS_PER_SECTOR	44
#define RMG_PROP_STEP			3		///< cells between placement candidates
#define RMG_PROP_CLEARANCE		4.0f	///< cells kept clear beyond a start's blend radius
#define RMG_SUPPLY_CLEARANCE	12.0f	///< cells kept clear around a supply dock
#define RMG_GAP_CLEARANCE		9.0f	///< cells kept clear around the chokepoint
#define RMG_CIVILIANS_PER_SECTOR 2
#define RMG_CIVILIAN_RING		0.24f
#define RMG_CIVILIAN_SPREAD		6.0f	///< cells between the two buildings of a cluster
#define RMG_HOSPITAL_MIN_CELLS	128

// What counts as a cliff. WorldHeightMap marks a cell impassable when its four
// corners span more than this many world units (PATHFIND_CLIFF_SLOPE_LIMIT_F).
#define RMG_CLIFF_WORLD_SPAN	9.8f

//-----------------------------------------------------------------------------
// Chunk writer
//-----------------------------------------------------------------------------

/** Writes the CkMp stream DataChunkOutput writes, but into a buffer instead of
	through a temp file in the user data directory. Chunk names and dictionary
	keys share one table of contents, exactly as the reader expects. */
class MapChunkWriter
{
public:
	void openChunk( const char *name, DataChunkVersionType version );
	void closeChunk( void );

	void writeInt( Int v );
	void writeReal( Real v );
	void writeByte( Byte v );
	void writeBytes( const void *data, Int len );
	void writeAsciiString( const char *s );

	void beginDict( Int pairCount );
	void dictBool( const char *key, Bool v );
	void dictInt( const char *key, Int v );
	void dictAsciiString( const char *key, const char *v );

	/// Table of contents followed by the chunk stream.
	void finish( std::vector<char>& out );

private:
	UnsignedInt idFor( const char *name );
	void writeKeyAndType( const char *key, Dict::DataType type );

	std::vector<char> m_body;
	std::vector<AsciiString> m_names;	///< index i holds the name of id i+1
	std::vector<Int> m_openChunks;		///< offsets of the size fields still to patch
};

UnsignedInt MapChunkWriter::idFor( const char *name )
{
	for( UnsignedInt i = 0; i < m_names.size(); i++ )
	{
		if( m_names[i].compare( name ) == 0 )
			return i + 1;
	}

	m_names.push_back( AsciiString( name ) );
	return m_names.size();
}

void MapChunkWriter::writeBytes( const void *data, Int len )
{
	const char *p = (const char *)data;
	m_body.insert( m_body.end(), p, p + len );
}

void MapChunkWriter::writeInt( Int v )		{ writeBytes( &v, sizeof(Int) ); }
void MapChunkWriter::writeReal( Real v )	{ writeBytes( &v, sizeof(Real) ); }
void MapChunkWriter::writeByte( Byte v )	{ writeBytes( &v, sizeof(Byte) ); }

void MapChunkWriter::writeAsciiString( const char *s )
{
	UnsignedShort len = (UnsignedShort)strlen( s );
	writeBytes( &len, sizeof(UnsignedShort) );
	writeBytes( s, len );
}

void MapChunkWriter::openChunk( const char *name, DataChunkVersionType version )
{
	UnsignedInt id = idFor( name );
	writeBytes( &id, sizeof(UnsignedInt) );
	writeBytes( &version, sizeof(DataChunkVersionType) );

	m_openChunks.push_back( m_body.size() );
	Int placeholder = 0;
	writeBytes( &placeholder, sizeof(Int) );
}

void MapChunkWriter::closeChunk( void )
{
	Int sizeFieldPos = m_openChunks.back();
	m_openChunks.pop_back();

	Int size = m_body.size() - sizeFieldPos - sizeof(Int);
	memcpy( &m_body[sizeFieldPos], &size, sizeof(Int) );
}

void MapChunkWriter::beginDict( Int pairCount )
{
	UnsignedShort len = (UnsignedShort)pairCount;
	writeBytes( &len, sizeof(UnsignedShort) );
}

void MapChunkWriter::writeKeyAndType( const char *key, Dict::DataType type )
{
	Int keyAndType = idFor( key );
	keyAndType <<= 8;
	keyAndType |= (type & 0xff);
	writeInt( keyAndType );
}

void MapChunkWriter::dictBool( const char *key, Bool v )
{
	writeKeyAndType( key, Dict::DICT_BOOL );
	writeByte( v ? 1 : 0 );
}

void MapChunkWriter::dictInt( const char *key, Int v )
{
	writeKeyAndType( key, Dict::DICT_INT );
	writeInt( v );
}

void MapChunkWriter::dictAsciiString( const char *key, const char *v )
{
	writeKeyAndType( key, Dict::DICT_ASCIISTRING );
	writeAsciiString( v );
}

void MapChunkWriter::finish( std::vector<char>& out )
{
	out.clear();

	const char tag[4] = { 'C', 'k', 'M', 'p' };
	out.insert( out.end(), tag, tag + 4 );

	Int listLength = m_names.size();
	const char *p = (const char *)&listLength;
	out.insert( out.end(), p, p + sizeof(Int) );

	for( Int i = 0; i < listLength; i++ )
	{
		unsigned char len = (unsigned char)m_names[i].getLength();
		out.push_back( (char)len );
		out.insert( out.end(), m_names[i].str(), m_names[i].str() + len );

		UnsignedInt id = i + 1;
		p = (const char *)&id;
		out.insert( out.end(), p, p + sizeof(UnsignedInt) );
	}

	out.insert( out.end(), m_body.begin(), m_body.end() );
}

//-----------------------------------------------------------------------------
// Perlin noise
//-----------------------------------------------------------------------------

/** Permutation seeded by an integer generator only - no rand(), no clock - so
	the field is identical on every machine that asks for the same seed. */
static void seedPermutation( Int seed, UnsignedByte perm[512] )
{
	UnsignedInt state = (UnsignedInt)seed * 1664525U + 1013904223U;
	if( state == 0 )
		state = 0x9E3779B9U;

	Int i;
	for( i = 0; i < 256; i++ )
		perm[i] = (UnsignedByte)i;

	for( i = 255; i > 0; i-- )
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		Int j = (Int)(state % (UnsignedInt)(i + 1));

		UnsignedByte tmp = perm[i];
		perm[i] = perm[j];
		perm[j] = tmp;
	}

	for( i = 0; i < 256; i++ )
		perm[256 + i] = perm[i];
}

static Real fadeCurve( Real t )
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static Real lerpReal( Real a, Real b, Real t )
{
	return a + t * (b - a);
}

static Real gradient( Int hash, Real x, Real y )
{
	switch( hash & 7 )
	{
		case 0:		return  x;
		case 1:		return  x + y;
		case 2:		return  y;
		case 3:		return -x + y;
		case 4:		return -x;
		case 5:		return -x - y;
		case 6:		return -y;
		default:	return  x - y;
	}
}

/// Classic 2D Perlin noise, in [-1,1].
static Real perlin2( const UnsignedByte perm[512], Real x, Real y )
{
	Int xi = (Int)floorf( x );
	Int yi = (Int)floorf( y );
	Real xf = x - (Real)xi;
	Real yf = y - (Real)yi;

	Int gx = xi & 255;
	Int gy = yi & 255;

	Real u = fadeCurve( xf );
	Real v = fadeCurve( yf );

	Int aa = perm[perm[gx] + gy];
	Int ab = perm[perm[gx] + gy + 1];
	Int ba = perm[perm[gx + 1] + gy];
	Int bb = perm[perm[gx + 1] + gy + 1];

	Real x1 = lerpReal( gradient( aa, xf, yf ), gradient( ba, xf - 1.0f, yf ), u );
	Real x2 = lerpReal( gradient( ab, xf, yf - 1.0f ), gradient( bb, xf - 1.0f, yf - 1.0f ), u );

	return lerpReal( x1, x2, v );
}

/// Sum of octaves, normalized back into [-1,1].
static Real fractalNoise( const UnsignedByte perm[512], Real x, Real y )
{
	Real total = 0.0f;
	Real amplitude = 1.0f;
	Real frequency = 1.0f;
	Real range = 0.0f;

	for( Int octave = 0; octave < RMG_OCTAVES; octave++ )
	{
		total += amplitude * perlin2( perm, x * frequency, y * frequency );
		range += amplitude;
		amplitude *= 0.5f;
		frequency *= 2.0f;
	}

	return total / range;
}

/// Repeatable per-cell randomness, for scattering that must not depend on order.
static UnsignedInt hashCell( Int seed, Int x, Int y )
{
	UnsignedInt h = (UnsignedInt)seed * 374761393U;
	h += (UnsignedInt)x * 668265263U;
	h ^= h >> 13;
	h += (UnsignedInt)y * 2246822519U;
	h ^= h >> 16;
	h *= 2654435761U;
	return h ^ (h >> 15);
}

//-----------------------------------------------------------------------------
// Layout
//-----------------------------------------------------------------------------

enum RMGTerrainClass
{
	RMG_TERRAIN_GRASS = 0,
	RMG_TERRAIN_DIRT,
	RMG_TERRAIN_ROCK,
	RMG_TERRAIN_SAND,

	RMG_TERRAIN_COUNT
};

/// Index order matches RMGTerrainClass; the names are Terrain.ini's.
static const char *theTerrainClassNames[RMG_TERRAIN_COUNT] =
{
	"GrassType1",
	"DirtType1",
	"RocksType1",
	"SandLargeType1"
};

struct RMGStart
{
	Real m_cellX;		///< in playable cells
	Real m_cellY;
	Real m_angle;		///< direction from the middle of the map outwards
};

/// Where a point sits relative to the sector its player owns.
struct RMGPolar
{
	Real m_radius;			///< cells from the middle of the playable area
	Real m_offsetAngle;		///< radians from the sector's own start bearing, in [-half, +half)
	Int m_sector;			///< which player's sector the point fell in
};

struct RMGObject
{
	AsciiString m_templateName;
	AsciiString m_uniqueID;
	Real m_worldX;
	Real m_worldY;
	Real m_angle;
	Int m_waypointID;		///< 0 for anything that is not a waypoint
};

struct RMGLake
{
	Real m_cellX;
	Real m_cellY;
	Real m_radius;
};

/** Everything derived from the settings, in one place, so the map bytes and the
	preview tga are two views of the same layout rather than two guesses at it. */
class RMGLayout
{
public:
	void build( const RandomMapSettings& settings );

	Int cellIndex( Int x, Int y ) const { return y * m_width + x; }
	UnsignedByte heightAtCell( Int x, Int y ) const { return m_heights[cellIndex( x, y )]; }
	UnsignedByte terrainAtCell( Int x, Int y ) const { return m_terrain[cellIndex( x, y )]; }
	Bool passableAtCell( Int x, Int y ) const { return m_passable[cellIndex( x, y )] != 0; }

	/// True when the point is inside a lake outline; the distance is to the nearest shore.
	Bool insideLake( Real cellX, Real cellY, Real *distanceOut ) const;

	RandomMapSettings m_settings;
	Int m_width;						///< map cells per side, border included
	Int m_height;
	Real m_baseAngle;					///< bearing of player one, from the middle
	Real m_sectorAngle;					///< 2 pi / players
	Real m_gapHalfCells;				///< half the chokepoint, after any widening
	Real m_flatRadius;					///< the disc a base is laid out on
	Real m_blendRadius;					///< where that disc has eased back into the terrain
	Real m_waterHeight;					///< height bytes; the surface of every lake
	RMGStart m_starts[RandomMapGenerator::MAX_PLAYERS];
	std::vector<UnsignedByte> m_heights;
	std::vector<UnsignedByte> m_terrain;
	std::vector<char> m_passable;
	std::vector<RMGLake> m_lakes;
	std::vector<RMGObject> m_objects;

private:
	void computeStarts( void );
	void placeLakes( void );
	void buildHeights( const UnsignedByte perm[512] );
	void buildTerrainClasses( void );
	void buildPassability( void );
	Bool everyStartReachable( void ) const;
	void buildObjects( const UnsignedByte perm[512] );
	void addProps( const UnsignedByte perm[512] );
	void addTechAndCivilians( void );
	void addRotatedCopies( const char *templateName, const char *idPrefix, Real cellX, Real cellY,
												 Real angle, Int& runningID );

	RMGPolar toPolar( Real cellX, Real cellY ) const;
	Real distanceToNearestStart( Real cellX, Real cellY ) const;
	Real ridgeAndLakeHeight( const UnsignedByte perm[512], Real cellX, Real cellY ) const;
	Real cellSpanWorld( Int x, Int y ) const;
	void rotateAboutCentre( Real cellX, Real cellY, Real angle, Real *outX, Real *outY ) const;
};

RMGPolar RMGLayout::toPolar( Real cellX, Real cellY ) const
{
	Real centre = (Real)m_settings.m_playableCells * 0.5f;
	Real dx = cellX - centre;
	Real dy = cellY - centre;

	RMGPolar polar;
	polar.m_radius = sqrtf( dx * dx + dy * dy );

	// ATan2, not atan2f: the simulation takes its trigonometry from Lib/Trig.h so
	// every machine gets the same answer, and a generated map is simulation input.
	Real angle = ATan2( dy, dx ) - m_baseAngle + m_sectorAngle * 0.5f;
	Real turns = angle / (2.0f * PI);
	angle -= floorf( turns ) * 2.0f * PI;			// into [0, 2 pi)

	Real sector = floorf( angle / m_sectorAngle );
	if( sector >= (Real)m_settings.m_numPlayers )
		sector = (Real)(m_settings.m_numPlayers - 1);

	polar.m_sector = (Int)sector;
	polar.m_offsetAngle = angle - sector * m_sectorAngle - m_sectorAngle * 0.5f;
	return polar;
}

void RMGLayout::rotateAboutCentre( Real cellX, Real cellY, Real angle, Real *outX, Real *outY ) const
{
	Real centre = (Real)m_settings.m_playableCells * 0.5f;
	Real dx = cellX - centre;
	Real dy = cellY - centre;
	Real c = Cos( angle );
	Real s = Sin( angle );

	*outX = centre + dx * c - dy * s;
	*outY = centre + dx * s + dy * c;
}

void RMGLayout::computeStarts( void )
{
	Real playable = (Real)m_settings.m_playableCells;
	Real centre = playable * 0.5f;
	Real ring = playable * RMG_START_RING;

	// One of 1024 rotations, so different seeds do not all put player one in
	// the same corner.
	UnsignedInt state = (UnsignedInt)m_settings.m_seed * 2654435761U + 12345U;
	state ^= state >> 15;
	m_baseAngle = (Real)(state % 1024U) * (2.0f * PI / 1024.0f);
	m_sectorAngle = 2.0f * PI / (Real)m_settings.m_numPlayers;

	for( Int i = 0; i < m_settings.m_numPlayers; i++ )
	{
		Real angle = m_baseAngle + m_sectorAngle * (Real)i;
		m_starts[i].m_cellX = centre + ring * Cos( angle );
		m_starts[i].m_cellY = centre + ring * Sin( angle );
		m_starts[i].m_angle = angle;
	}
}

/** One lake per sector, off the lane rather than across it. The first candidate that clears the
	start it belongs to, and both of that start's supply docks, wins; a dock standing in water is
	not a dock anybody can use. If none of them is clear the map simply has no lakes. */
void RMGLayout::placeLakes( void )
{
	m_lakes.clear();

	if( m_settings.m_playableCells < RMG_LAKE_MIN_CELLS )
		return;

	Real playable = (Real)m_settings.m_playableCells;
	Real centre = playable * 0.5f;

	// A lake is sized by the sector it has to fit in as well as by the map: eight players leave
	// each other a wedge, and a lake that does not fit in the wedge is not placed at all.
	Real spacing = 2.0f * PI * playable * RMG_START_RING / (Real)m_settings.m_numPlayers;
	Real radius = playable * RMG_LAKE_RADIUS;
	if( radius > spacing * RMG_LAKE_OF_SPACING )
		radius = spacing * RMG_LAKE_OF_SPACING;

	// Where the docks of the first player sit, which is where every player's sit once the layout
	// is turned into their sector.
	Real dockX[RMG_SUPPLY_PER_PLAYER];
	Real dockY[RMG_SUPPLY_PER_PLAYER];
	for( Int k = 0; k < RMG_SUPPLY_PER_PLAYER; k++ )
	{
		Real side = (k == 0) ? (PI * 0.5f) : (-PI * 0.5f);
		Real angle = m_starts[0].m_angle + side;
		dockX[k] = m_starts[0].m_cellX + RMG_SUPPLY_DISTANCE * Cos( angle );
		dockY[k] = m_starts[0].m_cellY + RMG_SUPPLY_DISTANCE * Sin( angle );
	}

	Real edge = radius + RMG_LAKE_SHORE;
	Real startClearance = edge + m_flatRadius;
	Real dockClearance = edge + RMG_LAKE_DOCK_CLEARANCE;

	/* Walk the sector out from the middle rather than trying a handful of named spots: on a small
		map with eight players there is very little room that is not somebody's base, somebody's
		dock or the ridge, and a fixed list either misses it or drops a lake on a dock. */
	// Outward first: a lake in the quiet part of a sector is better than one beside the middle of
	// the map, and the inner radii are what a cramped layout falls back on.
	for( Int radiusStep = 48; radiusStep >= 12; radiusStep -= 2 )
	{
		for( Int angleStep = 9; angleStep >= -9; angleStep-- )
		{
			Real lakeAngle = m_baseAngle + m_sectorAngle * 0.5f * (Real)angleStep * 0.1f;
			Real lakeRing = playable * (Real)radiusStep * 0.01f;
			Real x = centre + lakeRing * Cos( lakeAngle );
			Real y = centre + lakeRing * Sin( lakeAngle );

			if( distanceToNearestStart( x, y ) < startClearance )
				continue;

			// The ridge and its chokepoint stay out of the water.
			Real arcToBoundary = (m_sectorAngle * 0.5f
				- fabsf( m_sectorAngle * 0.5f * (Real)angleStep * 0.1f )) * lakeRing;
			if( arcToBoundary < edge + RMG_RIDGE_HALF_WIDTH )
				continue;

			Bool clear = TRUE;
			for( Int k = 0; k < RMG_SUPPLY_PER_PLAYER; k++ )
			{
				Real dx = x - dockX[k];
				Real dy = y - dockY[k];
				if( sqrtf( dx * dx + dy * dy ) < dockClearance )
					clear = FALSE;
			}
			if( !clear )
				continue;

			// The lake also has to stay inside the playable area, shore and all.
			if( x - edge < 0.0f || y - edge < 0.0f || x + edge > playable || y + edge > playable )
				continue;

			for( Int i = 0; i < m_settings.m_numPlayers; i++ )
			{
				RMGLake lake;
				rotateAboutCentre( x, y, m_sectorAngle * (Real)i, &lake.m_cellX, &lake.m_cellY );
				lake.m_radius = radius;
				m_lakes.push_back( lake );
			}
			return;
		}
	}
}

Real RMGLayout::distanceToNearestStart( Real cellX, Real cellY ) const
{
	Real nearest = 1.0e9f;

	for( Int i = 0; i < m_settings.m_numPlayers; i++ )
	{
		Real dx = cellX - m_starts[i].m_cellX;
		Real dy = cellY - m_starts[i].m_cellY;
		Real distance = sqrtf( dx * dx + dy * dy );
		if( distance < nearest )
			nearest = distance;
	}

	return nearest;
}

Bool RMGLayout::insideLake( Real cellX, Real cellY, Real *distanceOut ) const
{
	Bool inside = FALSE;
	Real nearest = 1.0e9f;

	for( UnsignedInt i = 0; i < m_lakes.size(); i++ )
	{
		Real dx = cellX - m_lakes[i].m_cellX;
		Real dy = cellY - m_lakes[i].m_cellY;
		Real dist = sqrtf( dx * dx + dy * dy ) - m_lakes[i].m_radius;
		if( dist < nearest )
			nearest = dist;
		if( dist < 0.0f )
			inside = TRUE;
	}

	if( distanceOut )
		*distanceOut = nearest;

	return inside;
}

/** The parts of the height field that come from the layout rather than the
	noise: the ridge between two players and the basin under a lake. Both are
	written in sector-local terms, so they appear identically in every sector. */
Real RMGLayout::ridgeAndLakeHeight( const UnsignedByte perm[512], Real cellX, Real cellY ) const
{
	Real playable = (Real)m_settings.m_playableCells;
	Real scale = RMG_FEATURES_PER_MAP / playable;

	RMGPolar polar = toPolar( cellX, cellY );

	// Sample the noise at the sector-zero image of this point, which is what
	// makes two players' ground the same ground.
	Real canonicalAngle = m_baseAngle + polar.m_offsetAngle;
	Real centre = playable * 0.5f;
	Real nx = centre + polar.m_radius * Cos( canonicalAngle );
	Real ny = centre + polar.m_radius * Sin( canonicalAngle );

	Real height = RMG_BASE_HEIGHT + RMG_AMPLITUDE * fractalNoise( perm, nx * scale, ny * scale );

	// The ridge, along the boundary the sector shares with its neighbour.
	Real halfSector = m_sectorAngle * 0.5f;
	Real arcToBoundary = (halfSector - fabsf( polar.m_offsetAngle )) * polar.m_radius;
	Real gapCentre = playable * RMG_GAP_RADIUS;
	Bool insideGap = fabsf( polar.m_radius - gapCentre ) < m_gapHalfCells;

	if( arcToBoundary < RMG_RIDGE_HALF_WIDTH &&
			polar.m_radius > playable * RMG_RIDGE_INNER &&
			polar.m_radius < playable * RMG_RIDGE_OUTER &&
			!insideGap &&
			distanceToNearestStart( cellX, cellY ) > RMG_BASE_CLEARANCE )
	{
		height += RMG_RIDGE_RISE;
	}

	// The lake basin. Inside the shore it drops to a flat floor, so the water
	// surface has something to sit on.
	Real distanceToShore;
	if( insideLake( cellX, cellY, &distanceToShore ) || distanceToShore < RMG_LAKE_SHORE )
	{
		Real floorHeight = RMG_BASE_HEIGHT - RMG_WATER_DROP - RMG_LAKE_DEPTH;
		Real t = 0.0f;
		if( distanceToShore > 0.0f )
			t = distanceToShore / RMG_LAKE_SHORE;

		height = lerpReal( floorHeight, height, fadeCurve( t ) );
	}

	return height;
}

void RMGLayout::buildHeights( const UnsignedByte perm[512] )
{
	m_heights.resize( m_width * m_height );

	// The flat disc under each start takes the height the terrain has at that
	// spot, so the discs sit in the landscape instead of on top of it. Every
	// start has the same one, because the sampled point is the same point.
	Real startHeight = ridgeAndLakeHeight( perm, m_starts[0].m_cellX, m_starts[0].m_cellY );

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real px = (Real)(x - RMG_BORDER_CELLS);
			Real py = (Real)(y - RMG_BORDER_CELLS);

			Real h = ridgeAndLakeHeight( perm, px, py );

			// The nearest start only. Blending against every start in turn lets
			// two overlapping discs flatten ground that belongs to neither.
			Real dist = distanceToNearestStart( px, py );
			if( dist < m_blendRadius )
			{
				Real t = 0.0f;
				if( dist > m_flatRadius )
					t = (dist - m_flatRadius) / (m_blendRadius - m_flatRadius);

				h = lerpReal( startHeight, h, fadeCurve( t ) );
			}

			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;

			m_heights[cellIndex( x, y )] = (UnsignedByte)(h + 0.5f);
		}
	}
}

Real RMGLayout::cellSpanWorld( Int x, Int y ) const
{
	Int a = m_heights[cellIndex( x, y )];
	Int b = m_heights[cellIndex( x + 1, y )];
	Int c = m_heights[cellIndex( x, y + 1 )];
	Int d = m_heights[cellIndex( x + 1, y + 1 )];

	Int lo = a, hi = a;
	if( b < lo ) lo = b;
	if( b > hi ) hi = b;
	if( c < lo ) lo = c;
	if( c > hi ) hi = c;
	if( d < lo ) lo = d;
	if( d > hi ) hi = d;

	return (Real)(hi - lo) * MAP_HEIGHT_SCALE;
}

void RMGLayout::buildTerrainClasses( void )
{
	m_terrain.resize( m_width * m_height );

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real px = (Real)(x - RMG_BORDER_CELLS);
			Real py = (Real)(y - RMG_BORDER_CELLS);
			Real height = (Real)m_heights[cellIndex( x, y )];

			Real span = 0.0f;
			if( x + 1 < m_width && y + 1 < m_height )
				span = cellSpanWorld( x, y );

			UnsignedByte terrainClass = RMG_TERRAIN_GRASS;

			Real distanceToShore;
			insideLake( px, py, &distanceToShore );

			// Sand is the low ground, whether the low ground is a lake bed or just the bottom of
			// a hollow. A map with no room for a lake still gets its shores.
			if( height < m_waterHeight + 2.0f || distanceToShore < RMG_LAKE_SHORE )
				terrainClass = RMG_TERRAIN_SAND;
			else if( span > RMG_CLIFF_WORLD_SPAN * 0.55f )
				terrainClass = RMG_TERRAIN_ROCK;
			else if( height > RMG_BASE_HEIGHT + RMG_AMPLITUDE * 0.45f )
				terrainClass = RMG_TERRAIN_DIRT;

			m_terrain[cellIndex( x, y )] = terrainClass;
		}
	}
}

void RMGLayout::buildPassability( void )
{
	m_passable.resize( m_width * m_height, 0 );

	for( Int y = 0; y < m_height - 1; y++ )
	{
		for( Int x = 0; x < m_width - 1; x++ )
		{
			Bool passable = cellSpanWorld( x, y ) <= RMG_CLIFF_WORLD_SPAN;

			if( passable )
			{
				Real px = (Real)(x - RMG_BORDER_CELLS);
				Real py = (Real)(y - RMG_BORDER_CELLS);
				if( insideLake( px, py, NULL ) && (Real)m_heights[cellIndex( x, y )] < m_waterHeight )
					passable = FALSE;
			}

			m_passable[cellIndex( x, y )] = passable ? 1 : 0;
		}
	}
}

/** A map nobody can cross is not a map. Flood fill from player one's build
	area and demand that every other start, and every supply dock, comes back.
	The ridge is the only thing that can break this, so a failure widens its gap
	and the whole layout is built again. */
Bool RMGLayout::everyStartReachable( void ) const
{
	std::vector<char> seen( m_width * m_height, 0 );
	std::vector<Int> stack;

	Int startX = (Int)(m_starts[0].m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int startY = (Int)(m_starts[0].m_cellY + 0.5f) + RMG_BORDER_CELLS;
	Int startIndex = cellIndex( startX, startY );
	if( m_passable[startIndex] == 0 )
		return FALSE;

	seen[startIndex] = 1;
	stack.push_back( startIndex );

	while( !stack.empty() )
	{
		Int index = stack.back();
		stack.pop_back();

		Int x = index % m_width;
		Int y = index / m_width;

		static const Int offsetX[4] = { 1, -1, 0, 0 };
		static const Int offsetY[4] = { 0, 0, 1, -1 };

		for( Int i = 0; i < 4; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( nx < 0 || ny < 0 || nx >= m_width - 1 || ny >= m_height - 1 )
				continue;

			Int next = cellIndex( nx, ny );
			if( seen[next] || m_passable[next] == 0 )
				continue;

			seen[next] = 1;
			stack.push_back( next );
		}
	}

	for( Int i = 1; i < m_settings.m_numPlayers; i++ )
	{
		Int x = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		Int y = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		if( !seen[cellIndex( x, y )] )
			return FALSE;
	}

	return TRUE;
}

void RMGLayout::addRotatedCopies( const char *templateName, const char *idPrefix, Real cellX,
																	Real cellY, Real angle, Int& runningID )
{
	for( Int i = 0; i < m_settings.m_numPlayers; i++ )
	{
		Real rotatedX, rotatedY;
		rotateAboutCentre( cellX, cellY, m_sectorAngle * (Real)i, &rotatedX, &rotatedY );

		RMGObject object;
		object.m_templateName = templateName;
		object.m_uniqueID.format( "%s %d", idPrefix, runningID++ );
		object.m_worldX = rotatedX * MAP_XY_FACTOR;
		object.m_worldY = rotatedY * MAP_XY_FACTOR;
		object.m_angle = angle + m_sectorAngle * (Real)i;
		object.m_waypointID = 0;

		m_objects.push_back( object );
	}
}

void RMGLayout::addProps( const UnsignedByte perm[512] )
{
	Real playable = (Real)m_settings.m_playableCells;
	Real scale = RMG_FEATURES_PER_MAP * 2.0f / playable;
	Real gapCentre = playable * RMG_GAP_RADIUS;

	// One name per slot of the hash, so a patch of forest is trees and a patch
	// of scree is rocks rather than both being one repeated model.
	static const char *theTreeNames[] = { "TreeDogwood1", "TreeFir01B", "TreeCherryBlossom01",
																				"TreeCherryBlossom02" };
	static const char *theRockNames[] = { "Rocks1", "Rocks2", "Rocks3", "RockClusterMedium01",
																				"RockClusterSmall01" };
	const Int numTreeNames = sizeof(theTreeNames) / sizeof(theTreeNames[0]);
	const Int numRockNames = sizeof(theRockNames) / sizeof(theRockNames[0]);

	// Snapshot the supply positions before the scan, since the scan appends to
	// the same list it would otherwise have to search.
	std::vector<RMGLake> supplyClearances;
	for( UnsignedInt i = 0; i < m_objects.size(); i++ )
	{
		if( m_objects[i].m_templateName.compare( "SupplyDock" ) != 0 )
			continue;

		RMGLake clearance;
		clearance.m_cellX = m_objects[i].m_worldX / MAP_XY_FACTOR;
		clearance.m_cellY = m_objects[i].m_worldY / MAP_XY_FACTOR;
		clearance.m_radius = RMG_SUPPLY_CLEARANCE;
		supplyClearances.push_back( clearance );
	}

	Int placed = 0;
	Int runningID = 1;

	for( Int cellY = 0; cellY < m_settings.m_playableCells && placed < RMG_PROPS_PER_SECTOR;
			 cellY += RMG_PROP_STEP )
	{
		for( Int cellX = 0; cellX < m_settings.m_playableCells && placed < RMG_PROPS_PER_SECTOR;
				 cellX += RMG_PROP_STEP )
		{
			Real px = (Real)cellX;
			Real py = (Real)cellY;

			// Only sector zero is scanned; the copies are rotated out of it.
			RMGPolar polar = toPolar( px, py );
			if( polar.m_sector != 0 )
				continue;

			Int mapX = cellX + RMG_BORDER_CELLS;
			Int mapY = cellY + RMG_BORDER_CELLS;
			if( !passableAtCell( mapX, mapY ) )
				continue;

			// Keep the ground people build and fight over clear.
			if( distanceToNearestStart( px, py ) < m_blendRadius + RMG_PROP_CLEARANCE )
				continue;

			if( fabsf( polar.m_radius - gapCentre ) < m_gapHalfCells + RMG_GAP_CLEARANCE )
				continue;

			Real distanceToShore;
			insideLake( px, py, &distanceToShore );
			if( distanceToShore < 2.0f )
				continue;

			Bool nearSupply = FALSE;
			for( UnsignedInt i = 0; i < supplyClearances.size(); i++ )
			{
				Real sx = supplyClearances[i].m_cellX - px;
				Real sy = supplyClearances[i].m_cellY - py;
				if( sqrtf( sx * sx + sy * sy ) < supplyClearances[i].m_radius )
				{
					nearSupply = TRUE;
					break;
				}
			}
			if( nearSupply )
				continue;

			// Patchy rather than evenly sprinkled: the noise decides where a
			// stand of trees is, the hash decides which cells of it are used.
			Real density = fractalNoise( perm, px * scale + 100.0f, py * scale - 100.0f );
			UnsignedInt hash = hashCell( m_settings.m_seed, cellX, cellY );
			Real roll = (Real)(hash % 1000U) / 1000.0f;
			if( roll > (density + 1.0f) * 0.25f )
				continue;

			Bool rocky = m_terrain[cellIndex( mapX, mapY )] == RMG_TERRAIN_ROCK;
			const char *name = rocky
				? theRockNames[(hash >> 10) % (UnsignedInt)numRockNames]
				: theTreeNames[(hash >> 10) % (UnsignedInt)numTreeNames];

			Real angle = (Real)((hash >> 20) % 1024U) * (2.0f * PI / 1024.0f);
			addRotatedCopies( name, "Prop", px, py, angle, runningID );
			placed++;
		}
	}
}

/** The neutral economy: a derrick on every chokepoint, so the crossing is worth
	holding, a pair of garrisonable buildings in each sector, and a hospital in
	the middle of a map big enough for one. */
void RMGLayout::addTechAndCivilians( void )
{
	Real playable = (Real)m_settings.m_playableCells;
	Real centre = playable * 0.5f;
	Int runningID = 1;

	// The derrick sits in the gap, which is on the boundary between one sector
	// and the next, so one copy per boundary is one copy per sector.
	Real gapRadius = playable * RMG_GAP_RADIUS;
	Real boundaryAngle = m_baseAngle + m_sectorAngle * 0.5f;
	addRotatedCopies( "TechOilDerrick", "Derrick",
										centre + gapRadius * Cos( boundaryAngle ),
										centre + gapRadius * Sin( boundaryAngle ),
										boundaryAngle + PI, runningID );

	runningID = 1;
	Real civilianRadius = playable * RMG_CIVILIAN_RING;
	for( Int i = 0; i < RMG_CIVILIANS_PER_SECTOR; i++ )
	{
		Real offset = (i == 0) ? RMG_CIVILIAN_SPREAD : -RMG_CIVILIAN_SPREAD;
		Real angle = m_baseAngle + offset / civilianRadius;
		Real cellX = centre + civilianRadius * Cos( angle );
		Real cellY = centre + civilianRadius * Sin( angle );

		Int mapX = (Int)(cellX + 0.5f) + RMG_BORDER_CELLS;
		Int mapY = (Int)(cellY + 0.5f) + RMG_BORDER_CELLS;
		if( mapX < 0 || mapY < 0 || mapX >= m_width || mapY >= m_height )
			continue;
		if( !passableAtCell( mapX, mapY ) )
			continue;

		const char *name = (i == 0) ? "CivilianHighrise01" : "CivilianHighrise02";
		addRotatedCopies( name, "Civilian", cellX, cellY, angle + PI, runningID );
	}

	if( m_settings.m_playableCells >= RMG_HOSPITAL_MIN_CELLS )
	{
		RMGObject hospital;
		hospital.m_templateName = "TechHospital";
		hospital.m_uniqueID = "Hospital 1";
		hospital.m_worldX = centre * MAP_XY_FACTOR;
		hospital.m_worldY = centre * MAP_XY_FACTOR;
		hospital.m_angle = m_baseAngle;
		hospital.m_waypointID = 0;
		m_objects.push_back( hospital );
	}
}

void RMGLayout::buildObjects( const UnsignedByte perm[512] )
{
	m_objects.clear();

	Int waypointID = 1;
	Int supplyID = 1;

	for( Int i = 0; i < m_settings.m_numPlayers; i++ )
	{
		RMGObject waypoint;
		waypoint.m_templateName = "*Waypoints/Waypoint";
		waypoint.m_uniqueID.format( "Player_%d_Start", i + 1 );
		waypoint.m_worldX = m_starts[i].m_cellX * MAP_XY_FACTOR;
		waypoint.m_worldY = m_starts[i].m_cellY * MAP_XY_FACTOR;
		waypoint.m_angle = 0.0f;
		waypoint.m_waypointID = waypointID++;
		m_objects.push_back( waypoint );
	}

	// Supply docks: off to either side of the line from the middle to the start,
	// written straight rather than through the rotation helper so the numbering
	// stays player by player.
	for( Int i = 0; i < m_settings.m_numPlayers; i++ )
	{
		for( Int k = 0; k < RMG_SUPPLY_PER_PLAYER; k++ )
		{
			Real side = (k == 0) ? (PI * 0.5f) : (-PI * 0.5f);
			Real angle = m_starts[i].m_angle + side;

			RMGObject dock;
			dock.m_templateName = "SupplyDock";
			dock.m_uniqueID.format( "SupplyDock %d", supplyID++ );
			dock.m_worldX = (m_starts[i].m_cellX + RMG_SUPPLY_DISTANCE * Cos( angle )) * MAP_XY_FACTOR;
			dock.m_worldY = (m_starts[i].m_cellY + RMG_SUPPLY_DISTANCE * Sin( angle )) * MAP_XY_FACTOR;
			dock.m_angle = m_starts[i].m_angle + PI;
			dock.m_waypointID = 0;
			m_objects.push_back( dock );
		}
	}

	addTechAndCivilians();
	addProps( perm );
}

void RMGLayout::build( const RandomMapSettings& settings )
{
	/* The height field is x87 arithmetic, so it comes out differently at 53-bit precision than at
		the 24 bits the simulation runs in - the same trap GameLogic::update re-asserts the control
		word for at the top of every logic frame. A map generated in whatever mode the last DLL left
		behind is a map the other machine does not have, so put the FPU where the simulation keeps
		it and give the caller back what it had. */
	UnsignedInt callersFPMode = getFPMode();
	setFPMode();

	m_settings = settings;
	RandomMapGenerator::clampSettings( m_settings );

	m_width = m_settings.m_playableCells + 2 * RMG_BORDER_CELLS;
	m_height = m_width;
	m_waterHeight = RMG_BASE_HEIGHT - RMG_WATER_DROP;

	UnsignedByte perm[512];
	seedPermutation( m_settings.m_seed, perm );

	computeStarts();

	Real playable = (Real)m_settings.m_playableCells;

	m_gapHalfCells = playable * RMG_GAP_HALF;

	// Cap the base discs by how far apart neighbouring starts actually are.
	Real spacing = 2.0f * PI * playable * RMG_START_RING / (Real)m_settings.m_numPlayers;
	m_flatRadius = spacing * RMG_FLAT_OF_SPACING;
	if( m_flatRadius > RMG_FLAT_RADIUS )
		m_flatRadius = RMG_FLAT_RADIUS;

	m_blendRadius = spacing * RMG_BLEND_OF_SPACING;
	if( m_blendRadius > RMG_BLEND_RADIUS )
		m_blendRadius = RMG_BLEND_RADIUS;
	if( m_blendRadius < m_flatRadius + 4.0f )
		m_blendRadius = m_flatRadius + 4.0f;

	// After the base discs are sized, since a lake keeps clear of them.
	placeLakes();

	for( Int attempt = 0; attempt < RMG_LAYOUT_ATTEMPTS; attempt++ )
	{
		buildHeights( perm );
		buildPassability();

		if( everyStartReachable() )
			break;

		// Widening the gap is the one lever that opens a map the ridge closed.
		m_gapHalfCells *= RMG_GAP_WIDEN_STEP;
	}

	buildTerrainClasses();
	buildObjects( perm );

	_controlfp( callersFPMode, _MCW_PC | _MCW_RC );
}

//-----------------------------------------------------------------------------
// Map pieces
//-----------------------------------------------------------------------------

/** The tile index WorldBuilder computes for a cell: four quadrants packed into
	each source tile, the source tile picked by tiling the class across the map. */
static Short tileIndexForCell( Int x, Int y, Int firstTile, Int width )
{
	Int ndx = firstTile + ((x / 2) % width) + width * ((y / 2) % width);
	ndx = (ndx << 2) + 2 * (y & 1) + (x & 1);
	return (Short)ndx;
}

/// One waypoint object. The reader calls anything with a waypointID a waypoint.
static void writeWaypoint( MapChunkWriter& w, const RMGObject& object )
{
	w.openChunk( "Object", K_OBJECTS_VERSION_3 );
		w.writeReal( object.m_worldX );
		w.writeReal( object.m_worldY );
		w.writeReal( 0.0f );
		w.writeReal( 0.0f );
		w.writeInt( 0 );
		w.writeAsciiString( object.m_templateName.str() );

		w.beginDict( 12 );
		w.dictInt( "objectInitialHealth", 100 );
		w.dictBool( "objectEnabled", TRUE );
		w.dictBool( "objectPowered", TRUE );
		w.dictBool( "objectRecruitableAI", TRUE );
		w.dictAsciiString( "originalOwner", "team" );
		w.dictAsciiString( "uniqueID", object.m_uniqueID.str() );
		w.dictAsciiString( "objectLayer", "Start Points" );
		w.dictInt( "waypointID", object.m_waypointID );
		w.dictBool( "objectDestructible", TRUE );
		w.dictBool( "objectSellable", TRUE );
		w.dictBool( "objectRepairable", TRUE );
		w.dictAsciiString( "waypointName", object.m_uniqueID.str() );
	w.closeChunk();
}

static void writeNeutralObject( MapChunkWriter& w, const RMGObject& object )
{
	w.openChunk( "Object", K_OBJECTS_VERSION_3 );
		w.writeReal( object.m_worldX );
		w.writeReal( object.m_worldY );
		w.writeReal( 0.0f );
		w.writeReal( object.m_angle );
		w.writeInt( 0 );
		w.writeAsciiString( object.m_templateName.str() );

		w.beginDict( 11 );
		w.dictInt( "objectInitialHealth", 100 );
		w.dictBool( "objectEnabled", TRUE );
		w.dictBool( "objectIndestructible", FALSE );
		w.dictBool( "objectUnsellable", FALSE );
		w.dictBool( "objectPowered", TRUE );
		w.dictBool( "objectRecruitableAI", TRUE );
		w.dictAsciiString( "objectName", "" );
		w.dictAsciiString( "originalOwner", "team" );
		w.dictAsciiString( "uniqueID", object.m_uniqueID.str() );
		w.dictAsciiString( "objectLayer", "" );
		w.dictBool( "objectSelectable", TRUE );
	w.closeChunk();
}

/** The sides every skirmish map carries: the civilians that own map scenery,
	and one side per playable faction for the skirmish scripts to attach to. */
static const char *theSkirmishSides[][2] =
{
	{ "PlyrCivilian",						"FactionCivilian" },
	{ "SkirmishAmerica",					"FactionAmerica" },
	{ "SkirmishChina",						"FactionChina" },
	{ "SkirmishGLA",						"FactionGLA" },
	{ "SkirmishAmericaAirForceGeneral",		"FactionAmericaAirForceGeneral" },
	{ "SkirmishAmericaLaserGeneral",		"FactionAmericaLaserGeneral" },
	{ "SkirmishAmericaSuperWeaponGeneral",	"FactionAmericaSuperWeaponGeneral" },
	{ "SkirmishChinaTankGeneral",			"FactionChinaTankGeneral" },
	{ "SkirmishChinaNukeGeneral",			"FactionChinaNukeGeneral" },
	{ "SkirmishChinaInfantryGeneral",		"FactionChinaInfantryGeneral" },
	{ "SkirmishGLADemolitionGeneral",		"FactionGLADemolitionGeneral" },
	{ "SkirmishGLAToxinGeneral",			"FactionGLAToxinGeneral" },
	{ "SkirmishGLAStealthGeneral",			"FactionGLAStealthGeneral" },
};
static const Int theNumSkirmishSides = sizeof(theSkirmishSides) / sizeof(theSkirmishSides[0]);

static void writeSides( MapChunkWriter& w )
{
	Int numSides = theNumSkirmishSides + 1;		// plus neutral
	Int i;

	w.openChunk( "SidesList", K_SIDES_DATA_VERSION_3 );
		w.writeInt( numSides );

		// Neutral, the side that owns the map itself.
		w.beginDict( 6 );
		w.dictAsciiString( "playerName", "" );
		w.dictBool( "playerIsHuman", FALSE );
		w.dictAsciiString( "playerDisplayName", "Neutral" );
		w.dictAsciiString( "playerFaction", "" );
		w.dictAsciiString( "playerAllies", "" );
		w.dictAsciiString( "playerEnemies", "" );
		w.writeInt( 0 );	// empty build list

		for( i = 0; i < theNumSkirmishSides; i++ )
		{
			w.beginDict( 6 );
			w.dictAsciiString( "playerName", theSkirmishSides[i][0] );
			w.dictBool( "playerIsHuman", FALSE );
			w.dictAsciiString( "playerDisplayName", theSkirmishSides[i][0] );
			w.dictAsciiString( "playerFaction", theSkirmishSides[i][1] );
			w.dictAsciiString( "playerAllies", "" );
			w.dictAsciiString( "playerEnemies", "" );
			w.writeInt( 0 );	// empty build list
		}

		// One default team per side.
		w.writeInt( numSides );

		w.beginDict( 3 );
		w.dictAsciiString( "teamName", "team" );
		w.dictAsciiString( "teamOwner", "" );
		w.dictBool( "teamIsSingleton", TRUE );

		for( i = 0; i < theNumSkirmishSides; i++ )
		{
			AsciiString teamName;
			teamName.format( "team%s", theSkirmishSides[i][0] );

			w.beginDict( 3 );
			w.dictAsciiString( "teamName", teamName.str() );
			w.dictAsciiString( "teamOwner", theSkirmishSides[i][0] );
			w.dictBool( "teamIsSingleton", TRUE );
		}

		// No scripts: skirmish and multiplayer games fall back to the shipped
		// SkirmishScripts.scb / MultiplayerScripts.scb.
		w.openChunk( "PlayerScriptsList", K_SCRIPTS_DATA_VERSION_1 );
			for( i = 0; i < numSides; i++ )
			{
				w.openChunk( "ScriptList", K_SCRIPT_LIST_DATA_VERSION_1 );
				w.closeChunk();
			}
		w.closeChunk();
	w.closeChunk();
}

/** One water area per lake, as the octagon the trigger reader wants. Point zero
	carries the water height for the whole area, which is what isUnderwater
	compares the ground against. */
static void writeWaterAreas( MapChunkWriter& w, const RMGLayout& layout )
{
	const Int numSides = 8;

	w.openChunk( "PolygonTriggers", K_TRIGGERS_VERSION_4 );
		w.writeInt( (Int)layout.m_lakes.size() );

		for( UnsignedInt i = 0; i < layout.m_lakes.size(); i++ )
		{
			AsciiString name;
			name.format( "Lake%d", i + 1 );

			w.writeAsciiString( name.str() );
			w.writeAsciiString( "" );			// layer
			w.writeInt( (Int)i + 1 );			// trigger id
			w.writeByte( 1 );					// is a water area
			w.writeByte( 0 );					// not a river
			w.writeInt( 0 );					// river start
			w.writeInt( numSides );

			Int waterZ = (Int)(layout.m_waterHeight * MAP_HEIGHT_SCALE + 0.5f);

			for( Int point = 0; point < numSides; point++ )
			{
				Real angle = (2.0f * PI * (Real)point) / (Real)numSides;
				Real x = layout.m_lakes[i].m_cellX + layout.m_lakes[i].m_radius * Cos( angle );
				Real y = layout.m_lakes[i].m_cellY + layout.m_lakes[i].m_radius * Sin( angle );

				w.writeInt( (Int)(x * MAP_XY_FACTOR + 0.5f) );
				w.writeInt( (Int)(y * MAP_XY_FACTOR + 0.5f) );
				w.writeInt( waterZ );
			}
		}
	w.closeChunk();
}

/** Daylight. A map with no lighting chunk keeps whatever GameData.ini left in GlobalData, which is
	the dusk the shipped maps all override, so a generated map looked like somebody had turned the
	sun off. These are written for all four times of day: the map has no scripts to change the hour
	with, and a player who forces one should still be able to see the ground. */
static void writeGlobalLighting( MapChunkWriter& w )
{
	// x and y point the sun across the map, z takes it down onto the ground.
	static const Real theSunDirection[3] = { -0.58f, 0.42f, -0.70f };
	static const Real theTerrainAmbient[3] = { 0.35f, 0.35f, 0.36f };
	static const Real theTerrainDiffuse[3] = { 0.78f, 0.75f, 0.68f };
	static const Real theObjectAmbient[3] = { 0.45f, 0.45f, 0.47f };
	static const Real theObjectDiffuse[3] = { 0.82f, 0.79f, 0.72f };

	w.openChunk( "GlobalLighting", K_LIGHTING_VERSION_3 );
		w.writeInt( TIME_OF_DAY_AFTERNOON );

		Int timeOfDay, light, channel;
		for( timeOfDay = 0; timeOfDay < 4; timeOfDay++ )
		{
			for( channel = 0; channel < 3; channel++ )
				w.writeReal( theTerrainAmbient[channel] );
			for( channel = 0; channel < 3; channel++ )
				w.writeReal( theTerrainDiffuse[channel] );
			for( channel = 0; channel < 3; channel++ )
				w.writeReal( theSunDirection[channel] );

			for( channel = 0; channel < 3; channel++ )
				w.writeReal( theObjectAmbient[channel] );
			for( channel = 0; channel < 3; channel++ )
				w.writeReal( theObjectDiffuse[channel] );
			for( channel = 0; channel < 3; channel++ )
				w.writeReal( theSunDirection[channel] );

			// The two extra lights of version 3, dark but pointing somewhere valid: one sun is
			// what this map wants, and a light with no direction at all upsets the shaders.
			for( light = 1; light < MAX_GLOBAL_LIGHTS; light++ )
			{
				for( channel = 0; channel < 6; channel++ )
					w.writeReal( 0.0f );
				w.writeReal( 0.0f );
				w.writeReal( 0.0f );
				w.writeReal( -1.0f );
			}
			for( light = 1; light < MAX_GLOBAL_LIGHTS; light++ )
			{
				for( channel = 0; channel < 6; channel++ )
					w.writeReal( 0.0f );
				w.writeReal( 0.0f );
				w.writeReal( 0.0f );
				w.writeReal( -1.0f );
			}
		}
	w.closeChunk();
}

//-----------------------------------------------------------------------------
// RandomMapGenerator
//-----------------------------------------------------------------------------

void RandomMapGenerator::clampSettings( RandomMapSettings& settings )
{
	if( settings.m_playableCells < MIN_CELLS ) settings.m_playableCells = MIN_CELLS;
	if( settings.m_playableCells > MAX_CELLS ) settings.m_playableCells = MAX_CELLS;
	if( settings.m_numPlayers < MIN_PLAYERS ) settings.m_numPlayers = MIN_PLAYERS;
	if( settings.m_numPlayers > MAX_PLAYERS ) settings.m_numPlayers = MAX_PLAYERS;
}

void RandomMapGenerator::generate( const RandomMapSettings& settings, std::vector<char>& mapBytes )
{
	RMGLayout layout;
	layout.build( settings );

	Int width = layout.m_width;
	Int height = layout.m_height;
	Int dataSize = width * height;

	MapChunkWriter w;

	/***************HEIGHT MAP DATA ***************/
	w.openChunk( "HeightMapData", K_HEIGHT_MAP_VERSION_4 );
		w.writeInt( width );
		w.writeInt( height );
		w.writeInt( RMG_BORDER_CELLS );
		w.writeInt( 1 );					// one boundary
		w.writeInt( layout.m_settings.m_playableCells );
		w.writeInt( layout.m_settings.m_playableCells );
		w.writeInt( dataSize );
		w.writeBytes( &layout.m_heights[0], dataSize );
	w.closeChunk();

	/***************BLEND TILE DATA ***************/
	// Version 6 on purpose: from version 7 on the reader expects the cliff and
	// passability bits in the file, below it works them out from the heights.
	{
		std::vector<Short> tiles( dataSize );
		std::vector<Short> zeroes( dataSize, 0 );

		for( Int y = 0; y < height; y++ )
		{
			for( Int x = 0; x < width; x++ )
			{
				Int terrainClass = layout.m_terrain[layout.cellIndex( x, y )];
				tiles[layout.cellIndex( x, y )] = tileIndexForCell( x, y,
					terrainClass * RMG_TILES_PER_CLASS, RMG_TILE_SHEET_WIDTH );
			}
		}

		w.openChunk( "BlendTileData", K_BLEND_TILE_VERSION_6 );
			w.writeInt( dataSize );
			w.writeBytes( &tiles[0], dataSize * sizeof(Short) );
			w.writeBytes( &zeroes[0], dataSize * sizeof(Short) );	// blend tiles
			w.writeBytes( &zeroes[0], dataSize * sizeof(Short) );	// extra blend tiles
			w.writeBytes( &zeroes[0], dataSize * sizeof(Short) );	// cliff info

			w.writeInt( RMG_TERRAIN_COUNT * RMG_TILES_PER_CLASS );	// bitmap tiles
			w.writeInt( 1 );					// blended tiles: entry 0 is the default
			w.writeInt( 1 );					// cliff infos:   entry 0 is the default

			w.writeInt( RMG_TERRAIN_COUNT );
			for( Int terrainClass = 0; terrainClass < RMG_TERRAIN_COUNT; terrainClass++ )
			{
				w.writeInt( terrainClass * RMG_TILES_PER_CLASS );	// first tile
				w.writeInt( RMG_TILES_PER_CLASS );
				w.writeInt( RMG_TILE_SHEET_WIDTH );
				w.writeInt( 0 );				// legacy field
				w.writeAsciiString( theTerrainClassNames[terrainClass] );
			}

			w.writeInt( 0 );					// no edge tiles
			w.writeInt( 0 );					// no edge texture classes
		w.closeChunk();
	}

	/***************WORLD DATA ***************/
	// Must come before the sides chunk.
	w.openChunk( "WorldInfo", K_WORLDDICT_VERSION_1 );
		w.beginDict( 2 );
		w.dictInt( "weather", 0 );
		w.dictInt( "compression", 0 );
	w.closeChunk();

	/***************PLAYER DATA ***************/
	// Must come before the object list.
	writeSides( w );

	/***************OBJECTS DATA ***************/
	w.openChunk( "ObjectsList", K_OBJECTS_VERSION_3 );
		for( UnsignedInt i = 0; i < layout.m_objects.size(); i++ )
		{
			if( layout.m_objects[i].m_waypointID > 0 )
				writeWaypoint( w, layout.m_objects[i] );
			else
				writeNeutralObject( w, layout.m_objects[i] );
		}
	w.closeChunk();

	/***************WATER ***************/
	if( !layout.m_lakes.empty() )
		writeWaterAreas( w, layout );

	/***************GLOBAL LIGHTING DATA ***************/
	writeGlobalLighting( w );

	w.finish( mapBytes );
}

UnsignedInt RandomMapGenerator::fingerprint( const RandomMapSettings& settings )
{
	std::vector<char> mapBytes;
	generate( settings, mapBytes );

	CRC crc;
	Int version = RANDOM_MAP_GENERATOR_VERSION;
	crc.computeCRC( &version, sizeof(version) );
	crc.computeCRC( &mapBytes[0], (Int)mapBytes.size() );

	return crc.get();
}

//-----------------------------------------------------------------------------
// Preview
//-----------------------------------------------------------------------------

/// Uncompressed 24-bit bottom-up tga, the shape every loader in the tree reads.
static void writeTgaHeader( std::vector<char>& tga, Int pixels )
{
	const Int headerBytes = 18;
	char header[headerBytes];
	memset( header, 0, headerBytes );

	header[2] = 2;								// uncompressed true colour
	header[12] = (char)(pixels & 0xff);
	header[13] = (char)((pixels >> 8) & 0xff);
	header[14] = (char)(pixels & 0xff);
	header[15] = (char)((pixels >> 8) & 0xff);
	header[16] = 24;							// bits per pixel

	tga.insert( tga.end(), header, header + headerBytes );
}

void RandomMapGenerator::generatePreview( const RandomMapSettings& settings,
																					std::vector<char>& tgaBytes )
{
	RMGLayout layout;
	layout.build( settings );

	const Int pixels = PREVIEW_PIXELS;

	tgaBytes.clear();
	writeTgaHeader( tgaBytes, pixels );

	// Ground colours, in the RMGTerrainClass order, plus water and the marks.
	static const UnsignedByte theGroundColours[RMG_TERRAIN_COUNT][3] =
	{
		{  86, 122,  62 },		// grass
		{ 132, 112,  74 },		// dirt
		{ 118, 118, 112 },		// rock
		{ 190, 176, 130 }		// sand
	};
	static const UnsignedByte theWaterColour[3] = { 48, 86, 130 };
	static const UnsignedByte theStartColour[3] = { 240, 240, 240 };
	static const UnsignedByte theCliffColour[3] = { 58, 54, 50 };

	Real playable = (Real)layout.m_settings.m_playableCells;
	Real cellsPerPixel = playable / (Real)pixels;

	for( Int row = 0; row < pixels; row++ )
	{
		// Bottom-up file order, and the map's y grows the way the image's does.
		Int previewY = pixels - 1 - row;

		for( Int column = 0; column < pixels; column++ )
		{
			Int cellX = (Int)((Real)column * cellsPerPixel) + RMG_BORDER_CELLS;
			Int cellY = (Int)((Real)previewY * cellsPerPixel) + RMG_BORDER_CELLS;

			if( cellX >= layout.m_width ) cellX = layout.m_width - 1;
			if( cellY >= layout.m_height ) cellY = layout.m_height - 1;

			Real height = (Real)layout.heightAtCell( cellX, cellY );
			UnsignedByte terrainClass = layout.terrainAtCell( cellX, cellY );

			const UnsignedByte *colour = theGroundColours[terrainClass];

			Real px = (Real)(cellX - RMG_BORDER_CELLS);
			Real py = (Real)(cellY - RMG_BORDER_CELLS);
			if( layout.insideLake( px, py, NULL ) && height < layout.m_waterHeight )
				colour = theWaterColour;
			else if( !layout.passableAtCell( cellX, cellY ) )
				colour = theCliffColour;

			// Cheap relief: the higher the ground, the lighter the pixel.
			Real shade = 0.70f + 0.60f * (height - RMG_BASE_HEIGHT) / (RMG_AMPLITUDE * 2.0f);
			if( shade < 0.45f ) shade = 0.45f;
			if( shade > 1.35f ) shade = 1.35f;

			Real blue = (Real)colour[2] * shade;
			Real green = (Real)colour[1] * shade;
			Real red = (Real)colour[0] * shade;

			// The start positions, so the list entry says how many players it is for.
			for( Int i = 0; i < layout.m_settings.m_numPlayers; i++ )
			{
				Real dx = px - layout.m_starts[i].m_cellX;
				Real dy = py - layout.m_starts[i].m_cellY;
				if( dx * dx + dy * dy < 9.0f * cellsPerPixel * cellsPerPixel )
				{
					red = (Real)theStartColour[0];
					green = (Real)theStartColour[1];
					blue = (Real)theStartColour[2];
				}
			}

			if( red > 255.0f ) red = 255.0f;
			if( green > 255.0f ) green = 255.0f;
			if( blue > 255.0f ) blue = 255.0f;

			tgaBytes.push_back( (char)(UnsignedByte)blue );
			tgaBytes.push_back( (char)(UnsignedByte)green );
			tgaBytes.push_back( (char)(UnsignedByte)red );
		}
	}
}

//-----------------------------------------------------------------------------
// Writing a generated map where the map cache will find it
//-----------------------------------------------------------------------------

static Bool writeWholeFile( const AsciiString& path, const std::vector<char>& bytes )
{
	FILE *fp = fopen( path.str(), "wb" );
	if( fp == NULL )
	{
		DEBUG_LOG(("random map: could not write '%s'\n", path.str()));
		return FALSE;
	}

	fwrite( &bytes[0], 1, bytes.size(), fp );
	fclose( fp );
	return TRUE;
}

Bool writeRandomMap( const RandomMapSettings& settings, AsciiString& mapPathOut )
{
	RandomMapSettings clamped = settings;
	RandomMapGenerator::clampSettings( clamped );

	std::vector<char> mapBytes;
	std::vector<char> previewBytes;
	RandomMapGenerator::generate( clamped, mapBytes );
	RandomMapGenerator::generatePreview( clamped, previewBytes );

	// The map cache expects "<user maps>\<name>\<name>.map" - the directory
	// carries the name - and getMapPreviewImage wants "<name>.tga" beside it.
	AsciiString name;
	name.format( "RMG_v%d_%d_%dp_%dc", RANDOM_MAP_GENERATOR_VERSION, clamped.m_seed,
							 clamped.m_numPlayers, clamped.m_playableCells );

	AsciiString mapsDir, dir, previewPath;
	mapsDir.format( "%sMaps", TheGlobalData->getPath_UserData().str() );
	dir.format( "%s\\%s", mapsDir.str(), name.str() );
	mapPathOut.format( "%s\\%s.map", dir.str(), name.str() );
	previewPath.format( "%s\\%s.tga", dir.str(), name.str() );

	TheFileSystem->createDirectory( mapsDir );		// createDirectory is one level at a time
	TheFileSystem->createDirectory( dir );

	if( !writeWholeFile( mapPathOut, mapBytes ) )
		return FALSE;

	writeWholeFile( previewPath, previewBytes );

	DEBUG_LOG(("random map: wrote '%s' - %d players, %d cells, %d bytes, fingerprint %X\n",
		mapPathOut.str(), clamped.m_numPlayers, clamped.m_playableCells, mapBytes.size(),
		RandomMapGenerator::fingerprint( clamped )));

	return TRUE;
}

//-----------------------------------------------------------------------------
// MemoryChunkInputStream
//-----------------------------------------------------------------------------

MemoryChunkInputStream::MemoryChunkInputStream( const char *data, Int size ) :
	m_data(data), m_size(size), m_pos(0)
{
}

Int MemoryChunkInputStream::read( void *pData, Int numBytes )
{
	if( numBytes > m_size - m_pos )
		numBytes = m_size - m_pos;

	if( pData )
		memcpy( pData, m_data + m_pos, numBytes );

	m_pos += numBytes;
	return numBytes;
}

UnsignedInt MemoryChunkInputStream::tell( void )
{
	return m_pos;
}

Bool MemoryChunkInputStream::absoluteSeek( UnsignedInt pos )
{
	if( (Int)pos > m_size )
		pos = m_size;

	m_pos = pos;
	return TRUE;
}

Bool MemoryChunkInputStream::eof( void )
{
	return m_pos >= m_size;
}
