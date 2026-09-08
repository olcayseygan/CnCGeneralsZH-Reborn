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
// Nothing here is laid out by hand. The ground is warped fractal noise with
// mesas cut into it; the start positions, the money, the water and the scenery
// are all found in that ground by looking for the places that suit them. Two
// seeds are two different maps rather than one map turned round.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <math.h>
#include <float.h>
#include <algorithm>
#include <map>

#include "Common/RandomMapGenerator.h"
#include "Lib/Trig.h"
#include "Common/crc.h"
#include "Common/DataChunk.h"
#include "Common/Dict.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "GameLogic/FPUControl.h"

/** The sentinel WorldHeightMap::ParseBlendTileData asserts on after every blend entry. It is
	FLAG_VAL in WorldHeightMap.h, which lives in GameEngineDevice and so cannot be included from
	here; the number is the file format's, not that header's. */
static const Int RMG_BLEND_FLAG_VALUE = 0x7ADA0000;

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

// Each texture class is read as the four tiles of a 2x2 sheet.
#define RMG_TILES_PER_CLASS		4
#define RMG_TILE_SHEET_WIDTH	2

// How big a map is: a floor everybody gets, plus a share for each player. A normal two-player map
// is 248 cells across, which is 2480 world units - about what a shipped duel map measures - and an
// eight-player one is 416.
#define RMG_SMALL_FLOOR			144
#define RMG_SMALL_PER_PLAYER	20
#define RMG_NORMAL_FLOOR		192
#define RMG_NORMAL_PER_PLAYER	28
#define RMG_LARGE_FLOOR			240
#define RMG_LARGE_PER_PLAYER	36

// Height field, in height map bytes (a byte is MAP_HEIGHT_SCALE world units).
#define RMG_BASE_HEIGHT			60.0f
// Five octaves of Perlin average out to about a third of their nominal range, so this is roughly
// three times the relief it looks like: about 80 height bytes from valley to hilltop, which is
// three or four terraces of the step below.
#define RMG_AMPLITUDE			120.0f
#define RMG_OCTAVES				5
#define RMG_FEATURES_PER_MAP	3.0f
#define RMG_WARP_STRENGTH		0.45f	///< how far the noise drags its own coordinates

/* The high ground, in layers. The height field is quantised to terraces: the plateau is dead flat
	and the step between two of them is one cell wide, which puts its corner span at the whole
	terrace height and so past PATHFIND_CLIFF_SLOPE_LIMIT_F, where the pathfinder calls it a cliff.
	Everything a player drives over is either a terrace or a ramp cut between two of them. */
#define RMG_TERRACE_STEP		24.0f	///< height bytes per layer; a cliff needs more than 15.7
#define RMG_TERRACE_DETAIL		1.5f	///< bytes of roll left on a plateau so it is not a table
#define RMG_DETAIL_FEATURES		14.0f

// Water. Lakes are carved into the lowest ground the map has, then written out
// as water areas the engine reads as impassable to anything that cannot swim.
// Below the terrace the base height sits on, so the water is in the bottom layer of the map.
#define RMG_WATER_DROP			26.0f	///< height bytes: water surface below the base height
#define RMG_LAKE_DEPTH			13.0f	///< and how far the bed sits below that surface
// Wide, because the shore is what the renderer's soft water edge is drawn on: it looks for cells
// whose corners straddle the water plane, and a bank that drops in one step gives it nothing.
#define RMG_LAKE_SHORE			18.0f	///< cells the basin eases out over
/* Dry land has to be dry. A lake is a polygon, not a flood, so ground outside one that sits below
	the water surface is not filled in - it is drawn as land with the lake standing above it like a
	puddle on a table, which is exactly what the noise leaves behind when it puts a lake in the
	lowest ground it can find. */
#define RMG_LAKE_BANK			4.0f	///< height bytes the land outside a lake is kept above it
#define RMG_WAVE_SPACING		16.0f	///< cells of shoreline between two ambient wave emitters
#define RMG_LAKE_RADIUS			0.055f	///< fraction of the playable size
#define RMG_LAKE_MIN_CELLS		96
#define RMG_LAKE_WOBBLE			0.45f	///< how far the outline wanders from a circle
#define RMG_LAKE_POINTS			32		///< sides of the polygon the water area is written as

// A start position gets a flat disc to build on, easing back into the terrain.
#define RMG_FLAT_RADIUS			13.0f
#define RMG_BLEND_RADIUS		26.0f
#define RMG_START_MARGIN		10.0f	///< cells of playable area kept outside the blend disc
#define RMG_START_EDGE_FRACTION	0.14f	///< and this much of the map besides, so a base has ground behind it
#define RMG_START_STRIDE		3		///< cells between the spots the search looks at
#define RMG_START_ROUGHNESS		2.6f	///< height bytes a base site may vary by, on average

// Money. One dock beside each base, one more out where it has to be fought
// over, and two oil derricks a player somewhere in between.
#define RMG_HOME_SUPPLY_MIN		18.0f	///< cells from the start
#define RMG_HOME_SUPPLY_MAX		30.0f
#define RMG_FAR_SUPPLY_SEPARATION	0.22f	///< fraction of the playable size
#define RMG_DERRICKS_PER_PLAYER	2
#define RMG_SITE_CLEARANCE		7.0f	///< cells kept clear around anything placed

/* Anything that stands on the ground gets the ground levelled under it first. A supply dock on a
	terrace edge is a dock with one corner in the air, and the game will not let a player build
	beside it either. The pad is flat to its radius and eases back into the terrain over the blend. */
#define RMG_PAD_RADIUS			5.0f
#define RMG_PAD_BLEND			10.0f
#define RMG_PAD_SHORE_KEEP		2.0f	///< cells of beach a pad never touches

/* A town. Nothing here is a fixed number: each town rolls how many streets it has each way, how far
	apart they run, how deep the plots are, and which way the whole grid is turned. Two towns on one
	map are two different towns. */
#define RMG_TOWN_MIN_STREETS	3
#define RMG_TOWN_MAX_STREETS	6
#define RMG_TOWN_BLOCK_MIN		18.0f	///< cells between two street centre lines, at the closest
#define RMG_TOWN_BLOCK_MAX		34.0f	///< and at the widest
#define RMG_TOWN_PLOT_MIN		9.0f	///< cells of frontage a building takes along a street
#define RMG_TOWN_PLOT_MAX		14.0f
#define RMG_TOWN_SET_BACK_MIN	5.0f	///< cells from the street centre to a building front
#define RMG_TOWN_SET_BACK_MAX	8.0f
#define RMG_TOWN_GAP_IN_100		22		///< plots left empty, so a street has yards and corners
#define RMG_TOWN_JITTER			2.0f	///< cells a building sits off its own frontage line
#define RMG_TOWN_DRY_MARGIN		4.0f	///< cells a building or a street keeps off the water
#define RMG_TOWN_ROAD			"TwoLane"

// A street is laid in dry runs: what crosses water is dropped rather than driven into the lake.
#define RMG_ROAD_STEP			2.0f	///< cells between two wet-or-dry samples along a segment
#define RMG_ROAD_MIN_RUN		8.0f	///< shorter than this and the run is not worth a road

// Bunkers go on the ramps, which is the ground worth holding.
#define RMG_BUNKER_CLEARANCE	10.0f

// Scenery. Counted against the ground rather than the players: a map twice the size wants four
// times the trees, or a wood is a hedge with a field around it.
#define RMG_CELLS_PER_TREE		150.0f
#define RMG_CELLS_PER_ROCK		2600.0f
#define RMG_PROP_STRIDE			2
#define RMG_FOREST_FEATURES		7.0f
#define RMG_FOREST_THRESHOLD	0.10f	///< of the forest field; under it, open ground

// What counts as a cliff. WorldHeightMap marks a cell impassable when its four
// corners span more than this many world units (PATHFIND_CLIFF_SLOPE_LIMIT_F).
#define RMG_CLIFF_WORLD_SPAN	9.8f
// And the most a carved pass may climb from one cell to the next, in height
// bytes, which has to leave room under that span for the noise either side.
#define RMG_PASS_MAX_STEP		8.0f
#define RMG_PASS_HALF_WIDTH		2		///< cells either side of the route that get cut
#define RMG_PASS_ATTEMPTS		12

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
// Noise
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
static Real fractalNoise( const UnsignedByte perm[512], Real x, Real y, Int octaves )
{
	Real total = 0.0f;
	Real amplitude = 1.0f;
	Real frequency = 1.0f;
	Real range = 0.0f;

	for( Int octave = 0; octave < octaves; octave++ )
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

/** Ordered by which one bleeds into which. A cell only ever takes a blend from
	a neighbour above it in this list, so two neighbouring cells never both try
	to blend into each other and leave a seam down the middle. */
enum RMGTerrainClass
{
	RMG_TERRAIN_GRASS = 0,
	RMG_TERRAIN_SAND,
	RMG_TERRAIN_DIRT,
	RMG_TERRAIN_ROCK,

	RMG_TERRAIN_COUNT
};

/// Index order matches RMGTerrainClass; the names are Terrain.ini's.
static const char *theTerrainClassNames[RMG_TERRAIN_COUNT] =
{
	"GrassType1",
	"SandLargeType1",
	"DirtType1",
	"RocksType1"
};

struct RMGPoint
{
	Real m_cellX;
	Real m_cellY;
};

struct RMGObject
{
	AsciiString m_templateName;
	AsciiString m_uniqueID;
	Real m_worldX;
	Real m_worldY;
	Real m_angle;
	Int m_waypointID;		///< 0 for anything that is not a waypoint
	Int m_flags;			///< MapObject's own flags; the road ones are the only ones used here
};

/** A road is two objects in a row carrying MapObject's road flags, which W3DRoadBuffer pairs up as
	it walks the list. The values are FLAG_ROAD_POINT1 and FLAG_ROAD_POINT2 from MapObject.h, which
	lives in GameEngine and could be included - but the flags are the file format's, and the object
	list here is written, never read back through MapObject. */
#define RMG_FLAG_ROAD_POINT1	0x00000002
#define RMG_FLAG_ROAD_POINT2	0x00000004

/** The shape one town was rolled to be: how many streets it has each way, where each one runs in
	the town's own coordinates, how deep its plots are and which way the whole grid is turned. It is
	rolled before a site is looked for, because the search needs the radius to keep the streets
	inside the map. */
struct RMGTownPlan
{
	Real m_rotation;		///< radians the grid is turned by, so no two towns face the same way
	Int m_streetsAcross;
	Int m_streetsDown;
	Real m_acrossAt[RMG_TOWN_MAX_STREETS];	///< where each across-street runs, town coordinates
	Real m_downAt[RMG_TOWN_MAX_STREETS];
	Real m_plotLength;
	Real m_setBack;
	Real m_radius;			///< from the middle to the far side of the outermost frontage
};

/// A hash read as a fraction of one, which is how every choice a town makes is made.
static Real hashUnit( Int seed, Int a, Int b )
{
	return (Real)(hashCell( seed, a, b ) % 4096U) / 4096.0f;
}

/// Where one direction's streets run, spaced unevenly and centred on the town. Returns the span.
static Real rollStreetLines( Int seed, Int town, Int streets, Real *out )
{
	Real at = 0.0f;
	Int line;

	for( line = 0; line < streets; line++ )
	{
		out[line] = at;
		at += lerpReal( RMG_TOWN_BLOCK_MIN, RMG_TOWN_BLOCK_MAX, hashUnit( seed, town, line ) );
	}

	Real span = out[streets - 1];
	for( line = 0; line < streets; line++ )
		out[line] -= span * 0.5f;

	return span;
}

static void rollTownPlan( Int seed, Int town, RMGTownPlan *plan )
{
	// A square grid repeats every quarter turn, so a quarter turn is the whole choice.
	plan->m_rotation = hashUnit( seed + 613, town, 0 ) * PI * 0.5f;

	const UnsignedInt streetChoices = (UnsignedInt)(RMG_TOWN_MAX_STREETS - RMG_TOWN_MIN_STREETS + 1);
	plan->m_streetsAcross = RMG_TOWN_MIN_STREETS +
			(Int)(hashCell( seed + 613, town, 1 ) % streetChoices);
	plan->m_streetsDown = RMG_TOWN_MIN_STREETS +
			(Int)(hashCell( seed + 613, town, 2 ) % streetChoices);

	plan->m_plotLength = lerpReal( RMG_TOWN_PLOT_MIN, RMG_TOWN_PLOT_MAX,
																 hashUnit( seed + 613, town, 3 ) );
	plan->m_setBack = lerpReal( RMG_TOWN_SET_BACK_MIN, RMG_TOWN_SET_BACK_MAX,
															hashUnit( seed + 613, town, 4 ) );

	Real spanAcross = rollStreetLines( seed + 6131, town, plan->m_streetsAcross, plan->m_acrossAt );
	Real spanDown = rollStreetLines( seed + 6133, town, plan->m_streetsDown, plan->m_downAt );
	Real span = (spanAcross > spanDown) ? spanAcross : spanDown;

	plan->m_radius = span * 0.5f + plan->m_setBack + 4.0f;
}

/// A point in the town's own coordinates, put where the map has it.
static void townToWorld( Real centreX, Real centreY, const RMGTownPlan& plan, Real across, Real down,
												 Real *outX, Real *outY )
{
	Real cosine = Cos( plan.m_rotation );
	Real sine = Sin( plan.m_rotation );

	*outX = centreX + across * cosine - down * sine;
	*outY = centreY + across * sine + down * cosine;
}

/// How far a plot is from the nearest street crossing it, so junctions are left as junctions.
static Real nearestStreetDistance( const Real *streetAt, Int streets, Real value )
{
	Real nearest = 1.0e9f;

	for( Int line = 0; line < streets; line++ )
	{
		Real distance = fabsf( value - streetAt[line] );
		if( distance < nearest )
			nearest = distance;
	}

	return nearest;
}

/** A lake is a circle whose radius is a noise field of its own, sampled once per outline point and
	interpolated in between, so the shore wanders the way a shore does and the polygon the engine
	gets is the same shape as the basin that was carved. */
struct RMGLake
{
	Real m_cellX;
	Real m_cellY;
	Real m_radius;
	Real m_outline[RMG_LAKE_POINTS];	///< radius at each of the outline's angles
};

/// The lake's radius in the direction of a point, interpolated between the two nearest outline points.
static Real lakeRadiusTowards( const RMGLake& lake, Real dx, Real dy )
{
	Real angle = ATan2( dy, dx );
	if( angle < 0.0f )
		angle += 2.0f * PI;

	Real position = angle * (Real)RMG_LAKE_POINTS / (2.0f * PI);
	Int first = (Int)position;
	Real fraction = position - (Real)first;

	first = first % RMG_LAKE_POINTS;
	Int second = (first + 1) % RMG_LAKE_POINTS;

	return lerpReal( lake.m_outline[first], lake.m_outline[second], fraction );
}

/// A place something has been put, and how much room it wants around it.
struct RMGSite
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
	Bool underwaterAtCell( Int x, Int y ) const;

	RandomMapSettings m_settings;
	Int m_width;						///< map cells per side, border included
	Int m_height;
	Real m_waterHeight;					///< height bytes; the surface of every lake
	std::vector<RMGPoint> m_starts;
	std::vector<UnsignedByte> m_heights;
	std::vector<UnsignedByte> m_terrain;
	std::vector<Short> m_blendIndex;	///< into m_blends, 0 for a cell with no blend
	std::vector<char> m_passable;
	std::vector<RMGLake> m_lakes;
	std::vector<RMGObject> m_objects;

	/// The blend table the BlendTileData chunk carries; entry 0 is the "no blend" default.
	struct RMGBlend
	{
		Int m_blendTileIndex;
		UnsignedByte m_horizontal;
		UnsignedByte m_vertical;
		UnsignedByte m_rightDiagonal;
		UnsignedByte m_leftDiagonal;
		UnsignedByte m_inverted;
		UnsignedByte m_longDiagonal;
	};
	std::vector<RMGBlend> m_blends;

private:
	void buildHeights( const UnsignedByte perm[512] );
	void placeLakes( const UnsignedByte perm[512] );
	void carveLakeBasins( void );
	void buildLakeMask( void );
	void chooseStarts( void );
	void flattenBases( void );
	void buildPassability( void );
	void connectStarts( void );
	Bool carvePass( Int fromStart, Int toStart );
	void buildTerrainClasses( const UnsignedByte perm[512] );
	void buildBlends( void );
	void buildObjects( const UnsignedByte perm[512] );
	void placeSupplyAndDerricks( void );
	void placeTowns( void );
	void placeBunkers( void );
	void placeShoreWaves( void );
	void placeScenery( const UnsignedByte perm[512] );
	void flattenPad( Real cellX, Real cellY, Real radius, Real blend );
	void addRoad( Real fromX, Real fromY, Real toX, Real toY );
	void addRoadClipped( Real fromX, Real fromY, Real toX, Real toY );
	Bool dryAt( Real cellX, Real cellY, Real margin ) const;

	Real cellSpanWorld( Int x, Int y ) const;
	Real roughnessAt( Int cellX, Int cellY, Int radius ) const;
	Bool insideLake( Real cellX, Real cellY, Real *distanceOut ) const;
	Real distanceToNearestStart( Real cellX, Real cellY ) const;
	Bool siteIsClear( Real cellX, Real cellY, Real radius ) const;
	Bool findSiteNear( Real centreX, Real centreY, Real minRadius, Real maxRadius, Real clearance,
										 RMGPoint *out ) const;
	void reserveSite( Real cellX, Real cellY, Real radius );
	void addObject( const char *templateName, const char *uniqueID, Real cellX, Real cellY,
									Real angle );
	Short blendEntryFor( Int blendTileIndex, Int cornerMask );

	std::map<Int, Short> m_blendLookup;	///< tile and shape to the table entry that holds them
	std::vector<RMGSite> m_sites;		///< everything placed so far, with its elbow room
	std::vector<RMGPoint> m_ramps;		///< where a carved route changed layer, for the bunkers
	std::vector<char> m_inLake;			///< cells inside a lake outline, whatever the ground does
	std::vector<char> m_visited;		///< scratch for the flood fill
	Int m_startSearchStride;
};

//-----------------------------------------------------------------------------
// Height field
//-----------------------------------------------------------------------------

/** Warped fractal noise, cut into terraces. The warp is what stops the terrain reading as a bowl
	of dents: it drags the noise's own coordinates around with a second field, so ridges bend and
	valleys wander. The terracing is what turns a smooth field into a map with layers - a plateau a
	player can build on, and a one-cell step down to the next one, which is steep enough that the
	pathfinder calls it a cliff. Ramps between the layers are cut later, where they are needed. */
void RMGLayout::buildHeights( const UnsignedByte perm[512] )
{
	Real playable = (Real)m_settings.m_playableCells;
	Real scale = RMG_FEATURES_PER_MAP / playable;
	Real detailScale = RMG_DETAIL_FEATURES / playable;

	m_heights.resize( m_width * m_height );

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real px = (Real)(x - RMG_BORDER_CELLS) * scale;
			Real py = (Real)(y - RMG_BORDER_CELLS) * scale;

			Real warpX = px + RMG_WARP_STRENGTH * fractalNoise( perm, px + 5.2f, py + 1.3f, 3 );
			Real warpY = py + RMG_WARP_STRENGTH * fractalNoise( perm, px - 3.7f, py + 8.1f, 3 );

			Real raw = RMG_BASE_HEIGHT
				+ RMG_AMPLITUDE * fractalNoise( perm, warpX, warpY, RMG_OCTAVES );

			// The layer this cell sits on, and then a little roll across the top of it so the
			// plateau is ground rather than a table.
			Real layer = floorf( raw / RMG_TERRACE_STEP );
			Real h = layer * RMG_TERRACE_STEP;

			h += RMG_TERRACE_DETAIL * fractalNoise( perm, (Real)x * detailScale + 61.0f,
																						 (Real)y * detailScale - 29.0f, 2 );

			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;

			m_heights[cellIndex( x, y )] = (UnsignedByte)(h + 0.5f);
		}
	}
}

Real RMGLayout::cellSpanWorld( Int x, Int y ) const
{
	if( x < 0 || y < 0 || x + 1 >= m_width || y + 1 >= m_height )
		return 0.0f;

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

/// Mean height difference from the middle of a disc, in height bytes: how flat a site is.
Real RMGLayout::roughnessAt( Int cellX, Int cellY, Int radius ) const
{
	if( cellX - radius < 0 || cellY - radius < 0 ||
			cellX + radius >= m_width || cellY + radius >= m_height )
		return 1000.0f;

	Real centre = (Real)m_heights[cellIndex( cellX, cellY )];
	Real total = 0.0f;
	Int samples = 0;

	for( Int dy = -radius; dy <= radius; dy += 2 )
	{
		for( Int dx = -radius; dx <= radius; dx += 2 )
		{
			if( dx * dx + dy * dy > radius * radius )
				continue;

			total += fabsf( (Real)m_heights[cellIndex( cellX + dx, cellY + dy )] - centre );
			samples++;
		}
	}

	if( samples == 0 )
		return 1000.0f;

	return total / (Real)samples;
}

//-----------------------------------------------------------------------------
// Water
//-----------------------------------------------------------------------------

Bool RMGLayout::insideLake( Real cellX, Real cellY, Real *distanceOut ) const
{
	Bool inside = FALSE;
	Real nearest = 1.0e9f;

	for( UnsignedInt i = 0; i < m_lakes.size(); i++ )
	{
		Real dx = cellX - m_lakes[i].m_cellX;
		Real dy = cellY - m_lakes[i].m_cellY;
		Real dist = sqrtf( dx * dx + dy * dy ) - lakeRadiusTowards( m_lakes[i], dx, dy );
		if( dist < nearest )
			nearest = dist;
		if( dist < 0.0f )
			inside = TRUE;
	}

	if( distanceOut )
		*distanceOut = nearest;

	return inside;
}

/** Which cells the lake outlines cover. The outlines never move once the lakes are placed, and the
	point-in-lake test costs an ATan2 per lake, so it is answered once here rather than a few
	million times over the passability passes and the route searches. */
void RMGLayout::buildLakeMask( void )
{
	m_inLake.assign( m_width * m_height, 0 );

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real px = (Real)(x - RMG_BORDER_CELLS);
			Real py = (Real)(y - RMG_BORDER_CELLS);
			m_inLake[cellIndex( x, y )] = insideLake( px, py, NULL ) ? 1 : 0;
		}
	}
}

Bool RMGLayout::underwaterAtCell( Int x, Int y ) const
{
	return m_inLake[cellIndex( x, y )] != 0 &&
		(Real)m_heights[cellIndex( x, y )] < m_waterHeight;
}

/** Lakes go where the map is already lowest, so the water sits in the hollows
	the noise made rather than in holes punched through a hillside. */
void RMGLayout::placeLakes( const UnsignedByte perm[512] )
{
	m_lakes.clear();

	if( m_settings.m_playableCells < RMG_LAKE_MIN_CELLS )
		return;

	Real playable = (Real)m_settings.m_playableCells;
	Real radius = playable * RMG_LAKE_RADIUS;

	// Far enough apart to be separate lakes rather than one marsh with islands in it.
	Real separation = playable * 0.22f;
	Int wanted = (m_settings.m_numPlayers + 2) / 2;

	while( (Int)m_lakes.size() < wanted )
	{
		Real lowest = 1000.0f;
		RMGPoint best;
		best.m_cellX = -1.0f;
		best.m_cellY = -1.0f;

		for( Int y = (Int)(radius + RMG_LAKE_SHORE);
				 y < m_settings.m_playableCells - (Int)(radius + RMG_LAKE_SHORE); y += 3 )
		{
			for( Int x = (Int)(radius + RMG_LAKE_SHORE);
					 x < m_settings.m_playableCells - (Int)(radius + RMG_LAKE_SHORE); x += 3 )
			{
				Int mapX = x + RMG_BORDER_CELLS;
				Int mapY = y + RMG_BORDER_CELLS;

				// The middle of a lake is the average of the ground it drowns, not one cell of it.
				Real total = 0.0f;
				Int samples = 0;
				for( Int dy = -3; dy <= 3; dy += 3 )
				{
					for( Int dx = -3; dx <= 3; dx += 3 )
					{
						total += (Real)m_heights[cellIndex( mapX + dx, mapY + dy )];
						samples++;
					}
				}

				Real average = total / (Real)samples;
				if( average >= lowest )
					continue;

				Bool clear = TRUE;
				for( UnsignedInt i = 0; i < m_lakes.size(); i++ )
				{
					Real dx = (Real)x - m_lakes[i].m_cellX;
					Real dy = (Real)y - m_lakes[i].m_cellY;
					if( sqrtf( dx * dx + dy * dy ) < separation )
						clear = FALSE;
				}
				if( !clear )
					continue;

				lowest = average;
				best.m_cellX = (Real)x;
				best.m_cellY = (Real)y;
			}
		}

		if( best.m_cellX < 0.0f )
			break;

		RMGLake lake;
		lake.m_cellX = best.m_cellX;
		lake.m_cellY = best.m_cellY;

		// A little variety in size, from the seed rather than from a constant.
		UnsignedInt hash = hashCell( m_settings.m_seed, (Int)lake.m_cellX, (Int)lake.m_cellY );
		lake.m_radius = radius * (0.75f + 0.5f * (Real)(hash % 1000U) / 1000.0f);

		/* The outline is the same noise field the ground is made of, walked round a circle in it,
			so one lake is a long inlet and the next is nearly round and neither was chosen. */
		Real ringRadius = 2.0f + (Real)(hash % 97U) * 0.05f;
		for( Int point = 0; point < RMG_LAKE_POINTS; point++ )
		{
			Real angle = 2.0f * PI * (Real)point / (Real)RMG_LAKE_POINTS;
			Real sampleX = lake.m_cellX * 0.05f + ringRadius * Cos( angle );
			Real sampleY = lake.m_cellY * 0.05f + ringRadius * Sin( angle );

			Real wobble = fractalNoise( perm, sampleX, sampleY, 3 );
			lake.m_outline[point] = lake.m_radius * (1.0f + RMG_LAKE_WOBBLE * wobble);
		}

		m_lakes.push_back( lake );
	}
}

/** Cut the basin so that the water is shallow at the edge and deep in the middle, with a beach
	rising out of it on the land side. The depth at the waterline is what the renderer's soft water
	edge is drawn from - it looks for cells whose corners straddle the water plane and fades the
	terrain into it over the first few feet of depth - so a basin dug to its full depth right up to
	the outline gets a hard blue line round it instead of a shore. */
void RMGLayout::carveLakeBasins( void )
{
	if( m_lakes.empty() )
		return;

	Real bed = RMG_BASE_HEIGHT - RMG_WATER_DROP - RMG_LAKE_DEPTH;
	Real atTheWaterline = m_waterHeight - 1.0f;

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real px = (Real)(x - RMG_BORDER_CELLS);
			Real py = (Real)(y - RMG_BORDER_CELLS);

			Real distanceToShore;
			insideLake( px, py, &distanceToShore );
			if( distanceToShore >= RMG_LAKE_SHORE )
				continue;

			Real h;
			if( distanceToShore < 0.0f )
			{
				// Inside the water: just under the surface at the rim, on the bed by the middle.
				Real t = -distanceToShore / RMG_LAKE_SHORE;
				if( t > 1.0f )
					t = 1.0f;

				h = lerpReal( atTheWaterline, bed, fadeCurve( t ) );
			}
			else
			{
				// The beach: out of the water at the rim, back into whatever the land was doing.
				Real t = distanceToShore / RMG_LAKE_SHORE;
				h = lerpReal( atTheWaterline + 2.0f, (Real)m_heights[cellIndex( x, y )],
											fadeCurve( t ) );
			}

			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;

			m_heights[cellIndex( x, y )] = (UnsignedByte)(h + 0.5f);
		}
	}

	// And everything that is not a lake comes up out of the water.
	Real bank = m_waterHeight + RMG_LAKE_BANK;

	for( Int landY = 0; landY < m_height; landY++ )
	{
		for( Int landX = 0; landX < m_width; landX++ )
		{
			/* Measured rather than read off the lake mask, and a cell short of the rim counts as
				land: the water area the map ships is a 32-sided polygon through the outline, so the
				cells between a chord and the arc it cuts are outside the water the game draws and
				have to be dry ground like any other. */
			Real distanceToShore;
			insideLake( (Real)(landX - RMG_BORDER_CELLS), (Real)(landY - RMG_BORDER_CELLS),
									&distanceToShore );
			if( distanceToShore < -1.0f )
				continue;

			Int index = cellIndex( landX, landY );
			if( (Real)m_heights[index] < bank )
				m_heights[index] = (UnsignedByte)(bank + 0.5f);
		}
	}
}

//-----------------------------------------------------------------------------
// Start positions
//-----------------------------------------------------------------------------

Real RMGLayout::distanceToNearestStart( Real cellX, Real cellY ) const
{
	Real nearest = 1.0e9f;

	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		Real dx = cellX - m_starts[i].m_cellX;
		Real dy = cellY - m_starts[i].m_cellY;
		Real distance = sqrtf( dx * dx + dy * dy );
		if( distance < nearest )
			nearest = distance;
	}

	return nearest;
}

/** The starts are found, not placed: every spot the search looks at is scored
	for how flat and how dry it is, the flattest one starts the list, and each
	one after that is whichever good site is furthest from the sites already
	taken. Nothing about the map is turned round or mirrored to make them fair -
	they are all standing on ground the same noise field made. */
void RMGLayout::chooseStarts( void )
{
	m_starts.clear();

	Int playable = m_settings.m_playableCells;

	/* The flat disc has to be inside the playable area, and a good deal more than that: the search
		below takes whichever site is furthest from the ones already picked, which on its own walks
		every base into a corner. A share of the map is kept clear of the edge as well, so there is
		ground behind a base to retreat into and to be attacked through. A map too small to give
		that up keeps the disc's own margin and nothing more. */
	Int margin = (Int)(RMG_FLAT_RADIUS + RMG_START_MARGIN);
	Int inset = (Int)((Real)playable * RMG_START_EDGE_FRACTION);
	if( inset > margin && playable - 2 * inset >= (Int)(4.0f * RMG_FLAT_RADIUS) )
		margin = inset;

	struct RMGCandidate { Real m_cellX, m_cellY, m_roughness; };
	std::vector<RMGCandidate> candidates;

	for( Int y = margin; y < playable - margin; y += m_startSearchStride )
	{
		for( Int x = margin; x < playable - margin; x += m_startSearchStride )
		{
			Int mapX = x + RMG_BORDER_CELLS;
			Int mapY = y + RMG_BORDER_CELLS;

			// Far enough that flattening the base does not leave a puddle in the middle of it.
			Real distanceToShore;
			insideLake( (Real)x, (Real)y, &distanceToShore );
			if( distanceToShore < RMG_BLEND_RADIUS )
				continue;

			Real roughness = roughnessAt( mapX, mapY, (Int)RMG_FLAT_RADIUS );
			if( roughness > RMG_START_ROUGHNESS * 3.0f )
				continue;

			RMGCandidate candidate;
			candidate.m_cellX = (Real)x;
			candidate.m_cellY = (Real)y;
			candidate.m_roughness = roughness;
			candidates.push_back( candidate );
		}
	}

	if( candidates.empty() )
	{
		// Nowhere is flat: fall back to a ring, which is always somewhere.
		Real centre = (Real)playable * 0.5f;
		for( Int i = 0; i < m_settings.m_numPlayers; i++ )
		{
			Real angle = 2.0f * PI * (Real)i / (Real)m_settings.m_numPlayers;
			RMGPoint start;
			start.m_cellX = centre + centre * 0.62f * Cos( angle );
			start.m_cellY = centre + centre * 0.62f * Sin( angle );
			m_starts.push_back( start );
		}
		return;
	}

	// The flattest candidate opens the list.
	UnsignedInt flattest = 0;
	for( UnsignedInt i = 1; i < candidates.size(); i++ )
	{
		if( candidates[i].m_roughness < candidates[flattest].m_roughness )
			flattest = i;
	}

	RMGPoint start;
	start.m_cellX = candidates[flattest].m_cellX;
	start.m_cellY = candidates[flattest].m_cellY;
	m_starts.push_back( start );

	/* Two base discs may not touch, whatever the map looks like. A candidate inside this is not
		scored against the others at all; only a map with nowhere else to go falls back to it. */
	Real hardFloor = 2.0f * RMG_FLAT_RADIUS + 6.0f;

	while( (Int)m_starts.size() < m_settings.m_numPlayers )
	{
		Real bestScore = -1.0e9f;
		UnsignedInt bestCandidate = 0;
		Bool foundClearOne = FALSE;

		for( UnsignedInt i = 0; i < candidates.size(); i++ )
		{
			Real distance = distanceToNearestStart( candidates[i].m_cellX, candidates[i].m_cellY );

			Bool clearOfTheFloor = distance >= hardFloor;
			if( foundClearOne && !clearOfTheFloor )
				continue;

			/* Distance first, flatness second, and distance is never traded away: a base that
				gives up ten cells of separation for slightly better ground is a base sharing its
				half of the map with a neighbour, and then both of them are fighting over one
				supply dock while the far side of the map stands empty. */
			Real score = distance - candidates[i].m_roughness * 2.0f;
			if( score > bestScore || (clearOfTheFloor && !foundClearOne) )
			{
				bestScore = score;
				bestCandidate = i;
				foundClearOne = clearOfTheFloor;
			}
		}

		start.m_cellX = candidates[bestCandidate].m_cellX;
		start.m_cellY = candidates[bestCandidate].m_cellY;
		m_starts.push_back( start );
	}
}

/// Flatten a disc under each start so a base can be laid out on it.
void RMGLayout::flattenBases( void )
{
	std::vector<Real> startHeights;
	std::vector<Real> flatRadii;
	std::vector<Real> blendRadii;

	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		Int mapX = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		Int mapY = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		startHeights.push_back( (Real)m_heights[cellIndex( mapX, mapY )] );

		/* Two discs at different heights meeting in the middle is a cliff between two bases, so
			on a map too cramped to hold everybody at the full radius each disc gives way rather
			than overlapping its neighbour. */
		Real nearestOther = 1.0e9f;
		for( UnsignedInt j = 0; j < m_starts.size(); j++ )
		{
			if( j == i )
				continue;

			Real dx = m_starts[i].m_cellX - m_starts[j].m_cellX;
			Real dy = m_starts[i].m_cellY - m_starts[j].m_cellY;
			Real distance = sqrtf( dx * dx + dy * dy );
			if( distance < nearestOther )
				nearestOther = distance;
		}

		Real flat = RMG_FLAT_RADIUS;
		if( flat > nearestOther * 0.45f )
			flat = nearestOther * 0.45f;

		Real blend = RMG_BLEND_RADIUS;
		if( blend > nearestOther * 0.9f )
			blend = nearestOther * 0.9f;
		if( blend < flat + 4.0f )
			blend = flat + 4.0f;

		flatRadii.push_back( flat );
		blendRadii.push_back( blend );
	}

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real px = (Real)(x - RMG_BORDER_CELLS);
			Real py = (Real)(y - RMG_BORDER_CELLS);

			// The nearest start only. Blending against every start in turn lets two
			// overlapping discs flatten ground that belongs to neither.
			Real nearest = 1.0e9f;
			UnsignedInt nearestIndex = 0;
			for( UnsignedInt i = 0; i < m_starts.size(); i++ )
			{
				Real dx = px - m_starts[i].m_cellX;
				Real dy = py - m_starts[i].m_cellY;
				Real distance = sqrtf( dx * dx + dy * dy );
				if( distance < nearest )
				{
					nearest = distance;
					nearestIndex = i;
				}
			}

			if( nearest >= blendRadii[nearestIndex] )
				continue;

			Real t = 0.0f;
			if( nearest > flatRadii[nearestIndex] )
				t = (nearest - flatRadii[nearestIndex])
					/ (blendRadii[nearestIndex] - flatRadii[nearestIndex]);

			Real h = lerpReal( startHeights[nearestIndex], (Real)m_heights[cellIndex( x, y )],
												 fadeCurve( t ) );
			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;

			m_heights[cellIndex( x, y )] = (UnsignedByte)(h + 0.5f);
		}
	}
}

//-----------------------------------------------------------------------------
// Passability, and cutting a pass where there is none
//-----------------------------------------------------------------------------

void RMGLayout::buildPassability( void )
{
	m_passable.assign( m_width * m_height, 0 );

	for( Int y = 0; y < m_height - 1; y++ )
	{
		for( Int x = 0; x < m_width - 1; x++ )
		{
			Bool passable = cellSpanWorld( x, y ) <= RMG_CLIFF_WORLD_SPAN;
			if( passable && underwaterAtCell( x, y ) )
				passable = FALSE;

			m_passable[cellIndex( x, y )] = passable ? 1 : 0;
		}
	}
}

/** A route between two starts, priced so that a terrace is nearly free and the step off one is
	expensive but not forbidden, then the ground along that route is cut into a ramp no steeper than
	the pathfinder will walk. What comes out is a ramp between two layers rather than a trench
	across the map, because the search stayed on the flat wherever it could. */
Bool RMGLayout::carvePass( Int fromStart, Int toStart )
{
	Int fromX = (Int)(m_starts[fromStart].m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int fromY = (Int)(m_starts[fromStart].m_cellY + 0.5f) + RMG_BORDER_CELLS;
	Int toX = (Int)(m_starts[toStart].m_cellX + 0.5f) + RMG_BORDER_CELLS;
	Int toY = (Int)(m_starts[toStart].m_cellY + 0.5f) + RMG_BORDER_CELLS;

	const Int cellCount = m_width * m_height;
	std::vector<Int> cost( cellCount, 0x7FFFFFFF );
	std::vector<Int> cameFrom( cellCount, -1 );

	const Int flatCost = 1;
	const Int cliffCost = 60;
	const Int waterCost = 250;

	/* Dijkstra over a binary heap of (cost, cell). A linear scan of the open list was fine on a
		96-cell map and is not on a 456-cell one: the open list runs to tens of thousands of cells
		and the scan is what the whole generator would then be doing. The cell index breaks ties so
		the route does not depend on how the heap happened to order two equal costs. */
	struct RMGOpenCell
	{
		Int m_cost;
		Int m_index;

		Bool operator<( const RMGOpenCell& other ) const
		{
			if( m_cost != other.m_cost )
				return m_cost > other.m_cost;		// std::push_heap wants the cheapest last
			return m_index > other.m_index;
		}
	};

	std::vector<RMGOpenCell> open;
	RMGOpenCell first;
	first.m_cost = 0;
	first.m_index = fromY * m_width + fromX;
	open.push_back( first );
	cost[first.m_index] = 0;

	while( !open.empty() )
	{
		std::pop_heap( open.begin(), open.end() );
		RMGOpenCell cheapest = open.back();
		open.pop_back();

		Int index = cheapest.m_index;
		if( cheapest.m_cost > cost[index] )
			continue;								// a cheaper way here was found after this was queued

		if( index == toY * m_width + toX )
			break;

		Int x = index % m_width;
		Int y = index / m_width;

		static const Int offsetX[4] = { 1, -1, 0, 0 };
		static const Int offsetY[4] = { 0, 0, 1, -1 };

		for( Int i = 0; i < 4; i++ )
		{
			Int nx = x + offsetX[i];
			Int ny = y + offsetY[i];
			if( nx < 1 || ny < 1 || nx >= m_width - 2 || ny >= m_height - 2 )
				continue;

			Int step = flatCost;
			if( m_passable[cellIndex( nx, ny )] == 0 )
				step = underwaterAtCell( nx, ny ) ? waterCost : cliffCost;

			Int next = ny * m_width + nx;
			if( cost[index] + step >= cost[next] )
				continue;

			cost[next] = cost[index] + step;
			cameFrom[next] = index;

			RMGOpenCell reached;
			reached.m_cost = cost[next];
			reached.m_index = next;
			open.push_back( reached );
			std::push_heap( open.begin(), open.end() );
		}
	}

	if( cameFrom[toY * m_width + toX] < 0 )
		return FALSE;

	// Walk the route back, then cut it as a ramp: each cell of the route may
	// only be so much higher or lower than the one before it.
	std::vector<Int> route;
	for( Int index = toY * m_width + toX; index >= 0; index = cameFrom[index] )
	{
		route.push_back( index );
		if( index == fromY * m_width + fromX )
			break;
	}

	std::vector<Real> profile( route.size() );
	for( UnsignedInt i = 0; i < route.size(); i++ )
		profile[i] = (Real)m_heights[route[i]];

	Real waterFloor = m_waterHeight + 2.0f;
	for( UnsignedInt i = 0; i < profile.size(); i++ )
	{
		if( profile[i] < waterFloor )
			profile[i] = waterFloor;
	}

	for( Int pass = 0; pass < 2; pass++ )
	{
		for( UnsignedInt i = 1; i < profile.size(); i++ )
		{
			Real limit = profile[i - 1] + RMG_PASS_MAX_STEP;
			if( profile[i] > limit )
				profile[i] = limit;

			limit = profile[i - 1] - RMG_PASS_MAX_STEP;
			if( profile[i] < limit )
				profile[i] = limit;
		}

		for( Int i = (Int)profile.size() - 2; i >= 0; i-- )
		{
			Real limit = profile[i + 1] + RMG_PASS_MAX_STEP;
			if( profile[i] > limit )
				profile[i] = limit;

			limit = profile[i + 1] - RMG_PASS_MAX_STEP;
			if( profile[i] < limit )
				profile[i] = limit;
		}
	}

	/* Where the cut is deepest is where the route came off one terrace and onto another: that is
		the ramp, and it is the ground worth standing a bunker on. One per route, so a map has as
		many of these as it has carved routes. */
	Real deepestCut = 6.0f;
	Int rampAt = -1;

	for( UnsignedInt i = 0; i < route.size(); i++ )
	{
		Real cut = fabsf( profile[i] - (Real)m_heights[route[i]] );
		if( cut > deepestCut )
		{
			deepestCut = cut;
			rampAt = (Int)i;
		}
	}

	if( rampAt >= 0 )
	{
		RMGPoint ramp;
		ramp.m_cellX = (Real)(route[rampAt] % m_width - RMG_BORDER_CELLS);
		ramp.m_cellY = (Real)(route[rampAt] / m_width - RMG_BORDER_CELLS);
		m_ramps.push_back( ramp );
	}

	for( UnsignedInt i = 0; i < route.size(); i++ )
	{
		Int x = route[i] % m_width;
		Int y = route[i] / m_width;

		for( Int dy = -RMG_PASS_HALF_WIDTH - 2; dy <= RMG_PASS_HALF_WIDTH + 2; dy++ )
		{
			for( Int dx = -RMG_PASS_HALF_WIDTH - 2; dx <= RMG_PASS_HALF_WIDTH + 2; dx++ )
			{
				Int nx = x + dx;
				Int ny = y + dy;
				if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
					continue;

				Real distance = sqrtf( (Real)(dx * dx + dy * dy) );
				Real t = (distance - (Real)RMG_PASS_HALF_WIDTH) / 2.0f;
				if( t < 0.0f ) t = 0.0f;
				if( t > 1.0f ) continue;

				Real h = lerpReal( profile[i], (Real)m_heights[cellIndex( nx, ny )], fadeCurve( t ) );
				if( h < 1.0f ) h = 1.0f;
				if( h > 254.0f ) h = 254.0f;

				m_heights[cellIndex( nx, ny )] = (UnsignedByte)(h + 0.5f);
			}
		}
	}

	return TRUE;
}

/** The ramp network. Terraced ground is a stack of plateaus with cliffs between them, so a map
	that cuts nothing is a map where half the players cannot reach the other half. Every start is
	joined to the next one round the list, which puts a ramp wherever a route has to change layer;
	then the flood fill says who is still cut off, and a pass is cut to each of those in turn.
	The ring is what makes a map rather than a corridor: the shortest way between two players is
	usually not the ramp either of them would use to reach a third. */
void RMGLayout::connectStarts( void )
{
	buildPassability();

	if( m_starts.size() > 2 )
	{
		for( UnsignedInt i = 0; i < m_starts.size(); i++ )
		{
			carvePass( (Int)i, (Int)((i + 1) % m_starts.size()) );
			flattenBases();
			buildPassability();
		}
	}

	for( Int attempt = 0; attempt < RMG_PASS_ATTEMPTS; attempt++ )
	{
		buildPassability();

		m_visited.assign( m_width * m_height, 0 );

		Int startX = (Int)(m_starts[0].m_cellX + 0.5f) + RMG_BORDER_CELLS;
		Int startY = (Int)(m_starts[0].m_cellY + 0.5f) + RMG_BORDER_CELLS;
		Int startIndex = startY * m_width + startX;

		std::vector<Int> stack;
		m_visited[startIndex] = 1;
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

				Int next = ny * m_width + nx;
				if( m_visited[next] || m_passable[next] == 0 )
					continue;

				m_visited[next] = 1;
				stack.push_back( next );
			}
		}

		Int unreachable = -1;
		for( UnsignedInt i = 1; i < m_starts.size(); i++ )
		{
			Int x = (Int)(m_starts[i].m_cellX + 0.5f) + RMG_BORDER_CELLS;
			Int y = (Int)(m_starts[i].m_cellY + 0.5f) + RMG_BORDER_CELLS;
			if( !m_visited[y * m_width + x] )
			{
				unreachable = (Int)i;
				break;
			}
		}

		if( unreachable < 0 )
			return;

		if( !carvePass( 0, unreachable ) )
			return;

		// The route runs into the base at either end of it, so the discs are laid flat again
		// rather than left with a ramp cut across the ground somebody has to build on.
		flattenBases();
	}

	buildPassability();
}

//-----------------------------------------------------------------------------
// Texture classes and their blends
//-----------------------------------------------------------------------------

/** Height and slope decide the ground cover, with the thresholds pushed about
	by a noise field of their own so the edges wander instead of following a
	contour line. */
void RMGLayout::buildTerrainClasses( const UnsignedByte perm[512] )
{
	m_terrain.resize( m_width * m_height );

	Real scale = RMG_FEATURES_PER_MAP * 2.5f / (Real)m_settings.m_playableCells;

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Real height = (Real)m_heights[cellIndex( x, y )];
			Real span = cellSpanWorld( x, y );

			Real wobble = fractalNoise( perm, (Real)x * scale + 31.0f, (Real)y * scale - 17.0f, 3 );

			UnsignedByte terrainClass = RMG_TERRAIN_GRASS;

			// Sand runs from the water up the whole beach, which is what makes a lake read as a lake
			// with a shore rather than a hole of water cut in the grass.
			if( height < m_waterHeight + 8.0f + wobble * 3.0f )
				terrainClass = RMG_TERRAIN_SAND;
			else if( span > RMG_CLIFF_WORLD_SPAN * (0.45f + wobble * 0.12f) )
				terrainClass = RMG_TERRAIN_ROCK;
			// Dry high ground. The offset is in height bytes rather than a fraction of the
			// amplitude: five octaves only reach a third of their nominal range, so a fraction of
			// it puts the line above anything the noise ever climbs to.
			else if( height > RMG_BASE_HEIGHT + 8.0f + wobble * 5.0f )
				terrainClass = RMG_TERRAIN_DIRT;

			m_terrain[cellIndex( x, y )] = terrainClass;
		}
	}
}

/** The tile index WorldBuilder computes for a cell: four quadrants packed into
	each source tile, the source tile picked by tiling the class across the map. */
static Short tileIndexForCell( Int x, Int y, Int firstTile, Int width )
{
	Int ndx = firstTile + ((x / 2) % width) + width * ((y / 2) % width);
	ndx = (ndx << 2) + 2 * (y & 1) + (x & 1);
	return (Short)ndx;
}

/** The corner alpha the renderer can actually express, and the flags that ask
	for it. `inverted` is one field shared by every flag in an entry, so the
	combinations are the ones on one side of it or the other - which is why a
	mask is matched to the nearest of these rather than built from scratch.
	Corners: bit 0 is (x,y), 1 is (x+1,y), 2 is (x+1,y+1), 3 is (x,y+1), in the
	order getAlphaUVData fills alpha[]. */
struct RMGBlendShape
{
	Int m_cornerMask;
	UnsignedByte m_horizontal;
	UnsignedByte m_vertical;
	UnsignedByte m_rightDiagonal;
	UnsignedByte m_leftDiagonal;
	UnsignedByte m_inverted;
	UnsignedByte m_longDiagonal;
};

static const RMGBlendShape theBlendShapes[] =
{
	// one corner
	{ 0x1, 0, 0, 0, 1, 1, 0 },		// (x,y)
	{ 0x2, 0, 0, 1, 0, 1, 0 },		// (x+1,y)
	{ 0x4, 0, 0, 1, 0, 0, 0 },		// (x+1,y+1)
	{ 0x8, 0, 0, 0, 1, 0, 0 },		// (x,y+1)
	// one edge
	{ 0x6, 1, 0, 0, 0, 0, 0 },		// the +x side
	{ 0x9, 1, 0, 0, 0, 1, 0 },		// the -x side
	{ 0xC, 0, 1, 0, 0, 0, 0 },		// the +y side
	{ 0x3, 0, 1, 0, 0, 1, 0 },		// the -y side
	// three corners
	{ 0xE, 1, 1, 0, 0, 0, 0 },		// everything but (x,y)
	{ 0xB, 0, 1, 0, 1, 1, 0 },		// everything but (x+1,y+1)
	{ 0xD, 1, 0, 1, 0, 0, 0 },		// everything but (x+1,y)
	{ 0x7, 0, 1, 1, 0, 1, 0 },		// everything but (x,y+1)
};
static const Int theNumBlendShapes = sizeof(theBlendShapes) / sizeof(theBlendShapes[0]);

/// Find or add the blend table entry that paints this tile over these corners.
Short RMGLayout::blendEntryFor( Int blendTileIndex, Int cornerMask )
{
	// Nearest shape by how many corners it gets wrong, preferring one that covers
	// more rather than less: a corner blended a little early is invisible, a
	// corner left unblended is the hard edge this whole pass exists to remove.
	Int bestShape = -1;
	Int bestPenalty = 100;

	for( Int i = 0; i < theNumBlendShapes; i++ )
	{
		Int missing = cornerMask & ~theBlendShapes[i].m_cornerMask;
		Int extra = theBlendShapes[i].m_cornerMask & ~cornerMask;

		Int penalty = 0;
		for( Int bit = 0; bit < 4; bit++ )
		{
			if( missing & (1 << bit) ) penalty += 3;
			if( extra & (1 << bit) ) penalty += 1;
		}

		if( penalty < bestPenalty )
		{
			bestPenalty = penalty;
			bestShape = i;
		}
	}

	if( bestShape < 0 )
		return 0;

	/* A tile and a shape name an entry, so that pair is the key. A scan of the table instead would
		be a scan per cell of the map, and on a 456-cell map with a few thousand entries in the
		table that is most of the generator's time. */
	Int key = blendTileIndex * theNumBlendShapes + bestShape;
	std::map<Int, Short>::const_iterator found = m_blendLookup.find( key );
	if( found != m_blendLookup.end() )
		return found->second;

	const RMGBlendShape& shape = theBlendShapes[bestShape];

	RMGBlend blend;
	blend.m_blendTileIndex = blendTileIndex;
	blend.m_horizontal = shape.m_horizontal;
	blend.m_vertical = shape.m_vertical;
	blend.m_rightDiagonal = shape.m_rightDiagonal;
	blend.m_leftDiagonal = shape.m_leftDiagonal;
	blend.m_inverted = shape.m_inverted;
	blend.m_longDiagonal = shape.m_longDiagonal;
	m_blends.push_back( blend );

	Short entry = (Short)(m_blends.size() - 1);
	m_blendLookup[key] = entry;
	return entry;
}

/** Every cell whose neighbour carries a higher-priority ground gets that ground
	painted over the corners it touches, as an alpha ramp across the cell. One
	side blends and the other does not, which is what stops two neighbours
	fighting over the same edge and leaving a seam between them. */
void RMGLayout::buildBlends( void )
{
	m_blends.clear();
	m_blendLookup.clear();

	RMGBlend nothing;
	memset( &nothing, 0, sizeof(nothing) );
	m_blends.push_back( nothing );		// entry 0 is "no blend"

	m_blendIndex.assign( m_width * m_height, 0 );

	for( Int y = 0; y < m_height; y++ )
	{
		for( Int x = 0; x < m_width; x++ )
		{
			Int mine = m_terrain[cellIndex( x, y )];

			// The strongest ground among the eight neighbours, and which of this
			// cell's corners touch it.
			Int strongest = mine;
			Int cornerMask = 0;

			for( Int dy = -1; dy <= 1; dy++ )
			{
				for( Int dx = -1; dx <= 1; dx++ )
				{
					if( dx == 0 && dy == 0 )
						continue;

					Int nx = x + dx;
					Int ny = y + dy;
					if( nx < 0 || ny < 0 || nx >= m_width || ny >= m_height )
						continue;

					Int theirs = m_terrain[cellIndex( nx, ny )];
					if( theirs <= mine )
						continue;

					if( theirs > strongest )
					{
						strongest = theirs;
						cornerMask = 0;
					}
					if( theirs < strongest )
						continue;

					// A neighbour claims the corners it shares with this cell.
					if( dx >= 0 && dy >= 0 ) cornerMask |= 0x4;		// (x+1,y+1)
					if( dx >= 0 && dy <= 0 ) cornerMask |= 0x2;		// (x+1,y)
					if( dx <= 0 && dy >= 0 ) cornerMask |= 0x8;		// (x,y+1)
					if( dx <= 0 && dy <= 0 ) cornerMask |= 0x1;		// (x,y)
				}
			}

			if( strongest == mine || cornerMask == 0 )
				continue;

			Int blendTile = tileIndexForCell( x, y, strongest * RMG_TILES_PER_CLASS,
																				RMG_TILE_SHEET_WIDTH );
			m_blendIndex[cellIndex( x, y )] = blendEntryFor( blendTile, cornerMask );
		}
	}
}

//-----------------------------------------------------------------------------
// Everything that stands on the map
//-----------------------------------------------------------------------------

Bool RMGLayout::siteIsClear( Real cellX, Real cellY, Real radius ) const
{
	for( UnsignedInt i = 0; i < m_sites.size(); i++ )
	{
		Real dx = cellX - m_sites[i].m_cellX;
		Real dy = cellY - m_sites[i].m_cellY;
		if( sqrtf( dx * dx + dy * dy ) < radius + m_sites[i].m_radius )
			return FALSE;
	}

	return TRUE;
}

void RMGLayout::reserveSite( Real cellX, Real cellY, Real radius )
{
	RMGSite site;
	site.m_cellX = cellX;
	site.m_cellY = cellY;
	site.m_radius = radius;
	m_sites.push_back( site );
}

void RMGLayout::addObject( const char *templateName, const char *uniqueID, Real cellX, Real cellY,
													 Real angle )
{
	RMGObject object;
	object.m_templateName = templateName;
	object.m_uniqueID = uniqueID;
	object.m_worldX = cellX * MAP_XY_FACTOR;
	object.m_worldY = cellY * MAP_XY_FACTOR;
	object.m_angle = angle;
	object.m_waypointID = 0;
	object.m_flags = 0;
	m_objects.push_back( object );
}

/** Level the ground under something that is about to stand on it. A supply dock across a terrace
	edge has one corner in the air and nothing can be built beside it, which on a map made of
	terraces is most of the places a search would otherwise call flat enough. */
void RMGLayout::flattenPad( Real cellX, Real cellY, Real radius, Real blend )
{
	Int centreX = (Int)(cellX + 0.5f) + RMG_BORDER_CELLS;
	Int centreY = (Int)(cellY + 0.5f) + RMG_BORDER_CELLS;
	if( centreX < 1 || centreY < 1 || centreX >= m_width - 1 || centreY >= m_height - 1 )
		return;

	Real level = (Real)m_heights[cellIndex( centreX, centreY )];
	Int reach = (Int)blend + 1;

	for( Int dy = -reach; dy <= reach; dy++ )
	{
		for( Int dx = -reach; dx <= reach; dx++ )
		{
			Int x = centreX + dx;
			Int y = centreY + dy;
			if( x < 0 || y < 0 || x >= m_width || y >= m_height )
				continue;

			// Never fill a lake in to make room for a building.
			if( underwaterAtCell( x, y ) )
				continue;

			Real distance = sqrtf( (Real)(dx * dx + dy * dy) );
			if( distance >= blend )
				continue;

			Real t = 0.0f;
			if( distance > radius )
				t = (distance - radius) / (blend - radius);

			/* The beach keeps its own profile. A pad that runs to the water's edge levels the shelf
				the soft water edge is drawn on and leaves the bank standing over the lake like a
				kerb, so the pad fades out as it comes up to the shore instead. */
			Real distanceToShore;
			insideLake( (Real)(x - RMG_BORDER_CELLS), (Real)(y - RMG_BORDER_CELLS), &distanceToShore );
			if( distanceToShore < RMG_PAD_SHORE_KEEP )
				continue;

			Real shoreT = (distanceToShore - RMG_PAD_SHORE_KEEP) / RMG_LAKE_SHORE;
			if( shoreT < 1.0f && t < 1.0f - shoreT )
				t = 1.0f - shoreT;

			Real h = lerpReal( level, (Real)m_heights[cellIndex( x, y )], fadeCurve( t ) );
			if( h < 1.0f ) h = 1.0f;
			if( h > 254.0f ) h = 254.0f;

			m_heights[cellIndex( x, y )] = (UnsignedByte)(h + 0.5f);
		}
	}
}

/** One road segment, as the two objects W3DRoadBuffer pairs up. They have to stay next to each
	other in the object list, which is why this writes both and nothing goes between them. */
void RMGLayout::addRoad( Real fromX, Real fromY, Real toX, Real toY )
{
	RMGObject point;
	point.m_templateName = RMG_TOWN_ROAD;
	point.m_uniqueID = AsciiString::TheEmptyString;
	point.m_angle = 0.0f;
	point.m_waypointID = 0;

	point.m_worldX = fromX * MAP_XY_FACTOR;
	point.m_worldY = fromY * MAP_XY_FACTOR;
	point.m_flags = RMG_FLAG_ROAD_POINT1;
	m_objects.push_back( point );

	point.m_worldX = toX * MAP_XY_FACTOR;
	point.m_worldY = toY * MAP_XY_FACTOR;
	point.m_flags = RMG_FLAG_ROAD_POINT2;
	m_objects.push_back( point );
}

/// Whether a point is far enough from every lake to build or pave on.
Bool RMGLayout::dryAt( Real cellX, Real cellY, Real margin ) const
{
	Real distanceToShore;
	insideLake( cellX, cellY, &distanceToShore );

	return distanceToShore >= margin;
}

/** The dry parts of a street. A town beside a lake has streets that run at the water, and a road
	object pair does not care whether the ground under it is a lake bed - it paves it, and what the
	player sees is tarmac going into the water. This walks the line, keeps the runs that stay on dry
	ground and drops the rest, so the street stops at the bank. */
void RMGLayout::addRoadClipped( Real fromX, Real fromY, Real toX, Real toY )
{
	Real spanX = toX - fromX;
	Real spanY = toY - fromY;
	Real length = sqrtf( spanX * spanX + spanY * spanY );
	if( length < RMG_ROAD_MIN_RUN )
		return;

	Int steps = (Int)(length / RMG_ROAD_STEP) + 1;
	Real runStart = -1.0f;

	for( Int step = 0; step <= steps; step++ )
	{
		Real along = (Real)step * length / (Real)steps;
		Real x = fromX + spanX * (along / length);
		Real y = fromY + spanY * (along / length);
		Bool dry = (step < steps) && dryAt( x, y, RMG_TOWN_DRY_MARGIN );

		if( dry && runStart < 0.0f )
		{
			runStart = along;
			continue;
		}

		if( dry || runStart < 0.0f )
			continue;

		if( along - runStart >= RMG_ROAD_MIN_RUN )
		{
			addRoad( fromX + spanX * (runStart / length), fromY + spanY * (runStart / length),
							 fromX + spanX * (along / length), fromY + spanY * (along / length) );
		}

		runStart = -1.0f;
	}
}

/** The flattest buildable spot in a ring around a point, whatever else is on the map. This is how
	anything a particular player is meant to reach gets placed: search from their ground rather
	than over the whole map, or every flat acre in the middle takes all the money and the players
	on the rough side of the map start with nothing. */
Bool RMGLayout::findSiteNear( Real centreX, Real centreY, Real minRadius, Real maxRadius,
															Real clearance, RMGPoint *out ) const
{
	const Int stepsRound = 96;
	Real bestRoughness = 1.0e9f;
	Bool found = FALSE;

	for( Int step = 0; step < stepsRound; step++ )
	{
		Real angle = 2.0f * PI * (Real)step / (Real)stepsRound;
		Real dirX = Cos( angle );
		Real dirY = Sin( angle );

		for( Real radius = minRadius; radius <= maxRadius; radius += 2.0f )
		{
			Real x = centreX + radius * dirX;
			Real y = centreY + radius * dirY;

			/* Inside the playable area, not merely inside the height field: the border is ground
				the camera looks across, and a supply dock out there is a dock nobody can reach.
				The clearance counts towards the edge as well, so a town whose streets are forty
				cells across is not centred four cells from the corner. */
			Real edge = 4.0f + clearance;
			if( x < edge || y < edge || x > (Real)m_settings.m_playableCells - edge ||
					y > (Real)m_settings.m_playableCells - edge )
				continue;

			Int mapX = (Int)(x + 0.5f) + RMG_BORDER_CELLS;
			Int mapY = (Int)(y + 0.5f) + RMG_BORDER_CELLS;
			if( !passableAtCell( mapX, mapY ) )
				continue;

			/* Off the water by something like what is being placed, capped: a town wants its middle
				well clear of a lake, but demanding the whole grid's radius of dry ground would mean
				no town on any map with water on it. What overhangs the bank is clipped where it is
				laid instead. */
			Real dryMargin = 4.0f + clearance * 0.5f;
			if( dryMargin > 26.0f )
				dryMargin = 26.0f;

			Real distanceToShore;
			insideLake( x, y, &distanceToShore );
			if( distanceToShore < dryMargin )
				continue;

			if( !siteIsClear( x, y, clearance ) )
				continue;

			Real roughness = roughnessAt( mapX, mapY, 5 );
			if( roughness < bestRoughness )
			{
				bestRoughness = roughness;
				out->m_cellX = x;
				out->m_cellY = y;
				found = TRUE;
			}
		}
	}

	return found;
}

/** A supply dock beside every base, one more out in the middle of the map for
	each player to fight over, and two oil derricks a player scattered between
	them. Nothing is placed relative to anything but the ground it stands on. */
void RMGLayout::placeSupplyAndDerricks( void )
{
	Int supplyID = 1;
	Int derrickID = 1;
	Real playable = (Real)m_settings.m_playableCells;

	// The dock each player opens on. Searched in an annulus round the start, so
	// it is close enough to be theirs and far enough not to be in the way.
	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		/* Full elbow room in the ring the base wants it in first; a base hemmed in by water or by
			the next base over gets its dock closer in, and then further out, rather than not at
			all.  A player short a dock is a player who has already lost. */
		RMGPoint best;
		if( !findSiteNear( m_starts[i].m_cellX, m_starts[i].m_cellY, RMG_HOME_SUPPLY_MIN,
											 RMG_HOME_SUPPLY_MAX, RMG_SITE_CLEARANCE, &best ) &&
				!findSiteNear( m_starts[i].m_cellX, m_starts[i].m_cellY, RMG_HOME_SUPPLY_MIN,
											 RMG_HOME_SUPPLY_MAX, RMG_SITE_CLEARANCE * 0.5f, &best ) &&
				!findSiteNear( m_starts[i].m_cellX, m_starts[i].m_cellY, RMG_HOME_SUPPLY_MIN,
											 RMG_HOME_SUPPLY_MAX * 2.0f, 1.0f, &best ) )
			continue;

		Real angle = ATan2( m_starts[i].m_cellY - best.m_cellY, m_starts[i].m_cellX - best.m_cellX );

		AsciiString uniqueID;
		uniqueID.format( "SupplyDock %d", supplyID++ );
		addObject( "SupplyDock", uniqueID.str(), best.m_cellX, best.m_cellY, angle );
		flattenPad( best.m_cellX, best.m_cellY, RMG_PAD_RADIUS, RMG_PAD_BLEND );
		reserveSite( best.m_cellX, best.m_cellY, RMG_SITE_CLEARANCE );
	}

	/* The second dock is the one worth fighting over: it goes out towards whoever is nearest, so
		it is a long walk from home and a short one from somebody who would rather you did not
		have it.  One per player, placed round their own start, because a search over the whole
		map hands every dock to whichever corner happens to be flattest. */
	Real farSeparation = playable * RMG_FAR_SUPPLY_SEPARATION;
	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		Real nearest = 1.0e9f;
		UnsignedInt nearestIndex = i;
		for( UnsignedInt j = 0; j < m_starts.size(); j++ )
		{
			if( j == i )
				continue;

			Real dx = m_starts[i].m_cellX - m_starts[j].m_cellX;
			Real dy = m_starts[i].m_cellY - m_starts[j].m_cellY;
			Real distance = sqrtf( dx * dx + dy * dy );
			if( distance < nearest )
			{
				nearest = distance;
				nearestIndex = j;
			}
		}

		Real towardsX = (m_starts[i].m_cellX + m_starts[nearestIndex].m_cellX) * 0.5f;
		Real towardsY = (m_starts[i].m_cellY + m_starts[nearestIndex].m_cellY) * 0.5f;

		RMGPoint site;
		if( !findSiteNear( towardsX, towardsY, 0.0f, farSeparation, RMG_SITE_CLEARANCE, &site ) &&
				!findSiteNear( towardsX, towardsY, 0.0f, farSeparation * 2.0f, 3.0f, &site ) &&
				!findSiteNear( m_starts[i].m_cellX, m_starts[i].m_cellY, RMG_HOME_SUPPLY_MAX,
											 (Real)playable * 0.5f, 3.0f, &site ) )
			continue;

		UnsignedInt hash = hashCell( m_settings.m_seed, (Int)site.m_cellX, (Int)site.m_cellY );
		Real angle = (Real)(hash % 1024U) * (2.0f * PI / 1024.0f);

		AsciiString uniqueID;
		uniqueID.format( "SupplyDock %d", supplyID++ );
		addObject( "SupplyDock", uniqueID.str(), site.m_cellX, site.m_cellY, angle );
		flattenPad( site.m_cellX, site.m_cellY, RMG_PAD_RADIUS, RMG_PAD_BLEND );
		reserveSite( site.m_cellX, site.m_cellY, RMG_SITE_CLEARANCE );
	}

	// Two derricks a player, out past the base but not as far as the fighting, and again one
	// player at a time so they end up spread over the map rather than heaped in the middle.
	// Close enough that the walk from the base is shorter than the walk from anybody else's.
	Real derrickInner = RMG_BLEND_RADIUS + 6.0f;
	Real derrickOuter = derrickInner + 18.0f;
	for( Int round = 0; round < RMG_DERRICKS_PER_PLAYER; round++ )
	{
		for( UnsignedInt i = 0; i < m_starts.size(); i++ )
		{
			RMGPoint site;
			if( !findSiteNear( m_starts[i].m_cellX, m_starts[i].m_cellY, derrickInner, derrickOuter,
												 RMG_SITE_CLEARANCE, &site ) &&
					!findSiteNear( m_starts[i].m_cellX, m_starts[i].m_cellY, derrickInner,
												 derrickOuter * 1.6f, 3.0f, &site ) )
				continue;

			UnsignedInt hash = hashCell( m_settings.m_seed + 91, (Int)site.m_cellX, (Int)site.m_cellY );
			Real angle = (Real)(hash % 1024U) * (2.0f * PI / 1024.0f);

			AsciiString uniqueID;
			uniqueID.format( "Derrick %d", derrickID++ );
			addObject( "TechOilDerrick", uniqueID.str(), site.m_cellX, site.m_cellY, angle );
			flattenPad( site.m_cellX, site.m_cellY, RMG_PAD_RADIUS, RMG_PAD_BLEND );
			reserveSite( site.m_cellX, site.m_cellY, RMG_SITE_CLEARANCE );
		}
	}
}

/** A town is generated, not stamped. Each one rolls its own grid - how many streets it has each
	way, how far apart they run, how deep its plots are, which way the whole thing faces - and the
	buildings come off a list walked in order down each street with gaps left in it. Anything that
	would land in a lake is dropped where it is laid rather than kept out by making the town smaller,
	so a town on a bank keeps its shape and loses the street that ran into the water. */
void RMGLayout::placeTowns( void )
{
	/* Two rows of frontage, because a street has two sides and a town where every building is the
		same one is a warehouse estate. The list is walked in order along a street so neighbours
		differ, and each town starts at a different place in it. */
	static const char *theStreetNames[] =
	{
		"StanApartment01", "StanSmallRetail01", "StanHotel01", "StanConvenienceStore01",
		"StanApartment02", "StanSmallRetail02", "StanRestaurant01", "StanSmallRetail03",
		"AsianRetailStore01", "AsianOffice01", "AsianHotel01", "AsianBank",
		"AsianRetailStore02", "CivilianHighrise01", "AsianArcade", "CivilianHighrise02"
	};
	const Int numStreetNames = sizeof(theStreetNames) / sizeof(theStreetNames[0]);

	Int buildingID = 1;
	// Two at the least: one town on a map is a landmark, two are a place the fight moves between.
	Int towns = m_settings.m_numPlayers / 2;
	if( towns < 2 )
		towns = 2;

	for( Int town = 0; town < towns; town++ )
	{
		RMGTownPlan plan;
		rollTownPlan( m_settings.m_seed, town, &plan );

		// Out of everybody's base, in the ground between them, which is where a town is worth
		// fighting through rather than one more thing in somebody's back garden.
		Real inner = RMG_BLEND_RADIUS + plan.m_radius + 6.0f;
		Real outer = inner + (Real)m_settings.m_playableCells * 0.22f;
		UnsignedInt which = (UnsignedInt)town % (UnsignedInt)m_starts.size();

		RMGPoint site;
		if( !findSiteNear( m_starts[which].m_cellX, m_starts[which].m_cellY, inner, outer,
											 plan.m_radius, &site ) )
			continue;

		// The whole town sits on one level: streets that run downhill through a terrace edge are
		// streets with a cliff across them.
		flattenPad( site.m_cellX, site.m_cellY, plan.m_radius, plan.m_radius + 12.0f );

		UnsignedInt hash = hashCell( m_settings.m_seed + 613, (Int)site.m_cellX, (Int)site.m_cellY );
		Int nameOffset = (Int)(hash % (UnsignedInt)numStreetNames);

		Real acrossEnd = plan.m_acrossAt[plan.m_streetsAcross - 1] + plan.m_setBack;
		Real acrossStart = plan.m_acrossAt[0] - plan.m_setBack;
		Real downEnd = plan.m_downAt[plan.m_streetsDown - 1] + plan.m_setBack;
		Real downStart = plan.m_downAt[0] - plan.m_setBack;

		Int line;
		Real fromX, fromY, toX, toY;

		for( line = 0; line < plan.m_streetsAcross; line++ )
		{
			townToWorld( site.m_cellX, site.m_cellY, plan, plan.m_acrossAt[line], downStart,
									 &fromX, &fromY );
			townToWorld( site.m_cellX, site.m_cellY, plan, plan.m_acrossAt[line], downEnd,
									 &toX, &toY );
			addRoadClipped( fromX, fromY, toX, toY );
		}

		for( line = 0; line < plan.m_streetsDown; line++ )
		{
			townToWorld( site.m_cellX, site.m_cellY, plan, acrossStart, plan.m_downAt[line],
									 &fromX, &fromY );
			townToWorld( site.m_cellX, site.m_cellY, plan, acrossEnd, plan.m_downAt[line],
									 &toX, &toY );
			addRoadClipped( fromX, fromY, toX, toY );
		}

		/* Buildings stand back from a street on both sides, facing it, down its whole length. The
			plots either side of a crossing are left empty so the junction stays a junction, and a
			fifth of the rest are left empty too, which is what stops a street reading as a wall of
			frontage with no yards, corners or car parks in it. */
		for( Int direction = 0; direction < 2; direction++ )
		{
			const Real *streetAt = (direction == 0) ? plan.m_acrossAt : plan.m_downAt;
			const Real *crossingAt = (direction == 0) ? plan.m_downAt : plan.m_acrossAt;
			Int streets = (direction == 0) ? plan.m_streetsAcross : plan.m_streetsDown;
			Int crossings = (direction == 0) ? plan.m_streetsDown : plan.m_streetsAcross;
			Real alongStart = (direction == 0) ? downStart : acrossStart;
			Real alongEnd = (direction == 0) ? downEnd : acrossEnd;

			for( line = 0; line < streets; line++ )
			{
				Int plot = 0;

				for( Real along = alongStart; along <= alongEnd + 0.5f; along += plan.m_plotLength )
				{
					plot++;

					if( nearestStreetDistance( crossingAt, crossings, along ) < plan.m_plotLength * 0.5f )
						continue;

					for( Int side = 0; side < 2; side++ )
					{
						UnsignedInt plotHash = hashCell( m_settings.m_seed + 6139,
																						 town * 64 + line * 4 + side, plot );
						if( plotHash % 100U < RMG_TOWN_GAP_IN_100 )
							continue;

						Real jitter = (hashUnit( m_settings.m_seed + 6141, town * 64 + line * 4 + side,
																		 plot ) - 0.5f) * 2.0f * RMG_TOWN_JITTER;
						Real back = (side == 0) ? -plan.m_setBack - jitter : plan.m_setBack + jitter;
						Real facing = (side == 0) ? 0.0f : PI;

						Real buildingX, buildingY;
						if( direction == 0 )
						{
							townToWorld( site.m_cellX, site.m_cellY, plan, streetAt[line] + back, along,
													 &buildingX, &buildingY );
						}
						else
						{
							townToWorld( site.m_cellX, site.m_cellY, plan, along, streetAt[line] + back,
													 &buildingX, &buildingY );
							facing += PI * 0.5f;
						}

						if( !dryAt( buildingX, buildingY, RMG_TOWN_DRY_MARGIN ) )
							continue;

						AsciiString uniqueID;
						uniqueID.format( "Civilian %d", buildingID++ );
						addObject( theStreetNames[(nameOffset + buildingID) % numStreetNames],
											 uniqueID.str(), buildingX, buildingY, plan.m_rotation + facing );
					}
				}
			}
		}

		reserveSite( site.m_cellX, site.m_cellY, plan.m_radius + 4.0f );
	}
}

/** Bunkers go where the carved routes changed layer. A ramp is the one place on a terraced map
	that has to be walked through rather than round, so a garrisoned bunker looking down one is
	worth taking, and nothing else on the map is worth putting there. */
void RMGLayout::placeBunkers( void )
{
	Int bunkerID = 1;

	for( UnsignedInt i = 0; i < m_ramps.size(); i++ )
	{
		/* A ramp is a gap in a cliff, so the ground beside one is the ground the search likes least.
			Rather than leave the ramp unwatched, the ask drops to less elbow room and then to a
			wider ring before giving up on it. */
		RMGPoint site;
		if( !findSiteNear( m_ramps[i].m_cellX, m_ramps[i].m_cellY, 6.0f, 18.0f,
											 RMG_BUNKER_CLEARANCE, &site ) &&
				!findSiteNear( m_ramps[i].m_cellX, m_ramps[i].m_cellY, 6.0f, 18.0f, 5.0f, &site ) &&
				!findSiteNear( m_ramps[i].m_cellX, m_ramps[i].m_cellY, 6.0f, 30.0f, 5.0f, &site ) )
			continue;

		if( distanceToNearestStart( site.m_cellX, site.m_cellY ) < RMG_BLEND_RADIUS + 8.0f )
			continue;

		UnsignedInt hash = hashCell( m_settings.m_seed + 5309, (Int)site.m_cellX, (Int)site.m_cellY );
		Real angle = (Real)(hash % 1024U) * (2.0f * PI / 1024.0f);

		AsciiString uniqueID;
		uniqueID.format( "Bunker %d", bunkerID++ );
		addObject( "CivilianBunker01", uniqueID.str(), site.m_cellX, site.m_cellY, angle );
		flattenPad( site.m_cellX, site.m_cellY, 3.0f, 7.0f );
		reserveSite( site.m_cellX, site.m_cellY, RMG_BUNKER_CLEARANCE );
	}
}

/** The sound of water on a shore, spaced round every lake. These are ambient emitters rather than
	anything drawn - what is drawn at the water's edge is the renderer's own soft edge, which is why
	the basin eases out over ten cells instead of dropping like a kerb. */
void RMGLayout::placeShoreWaves( void )
{
	Int waveID = 1;

	for( UnsignedInt i = 0; i < m_lakes.size(); i++ )
	{
		Real walked = RMG_WAVE_SPACING;		// so the first point of the outline gets one

		for( Int point = 0; point < RMG_LAKE_POINTS; point++ )
		{
			Real angle = 2.0f * PI * (Real)point / (Real)RMG_LAKE_POINTS;
			Real radius = m_lakes[i].m_outline[point];

			// Arc length from the last emitter, at this lake's own radius.
			walked += radius * (2.0f * PI / (Real)RMG_LAKE_POINTS);
			if( walked < RMG_WAVE_SPACING )
				continue;

			walked = 0.0f;

			// Just inside the waterline, where the wash would be.
			Real x = m_lakes[i].m_cellX + (radius - 1.5f) * Cos( angle );
			Real y = m_lakes[i].m_cellY + (radius - 1.5f) * Sin( angle );

			Int mapX = (Int)(x + 0.5f) + RMG_BORDER_CELLS;
			Int mapY = (Int)(y + 0.5f) + RMG_BORDER_CELLS;
			if( mapX < 1 || mapY < 1 || mapX >= m_width - 1 || mapY >= m_height - 1 )
				continue;

			AsciiString uniqueID;
			uniqueID.format( "Waves %d", waveID++ );
			addObject( "AmbientWavesLake", uniqueID.str(), x, y, angle + PI );
		}
	}
}

/** Trees stand in woods rather than in a sprinkle: a slow noise field decides
	where a wood is, a per-cell hash decides which cells of it are used, and the
	species is chosen per wood so one stand does not hold four kinds of tree.
	Rocks go where the ground is already rock. */
void RMGLayout::placeScenery( const UnsignedByte perm[512] )
{
	static const char *theTreeNames[] = { "TreeDogwood1", "TreeFir01B", "TreeCherryBlossom01",
																				"TreeCherryBlossom02" };
	static const char *theRockNames[] = { "Rocks1", "Rocks2", "Rocks3", "RockClusterMedium01",
																				"RockClusterSmall01" };
	const Int numTreeNames = sizeof(theTreeNames) / sizeof(theTreeNames[0]);
	const Int numRockNames = sizeof(theRockNames) / sizeof(theRockNames[0]);

	Real playable = (Real)m_settings.m_playableCells;
	Real forestScale = RMG_FOREST_FEATURES / playable;

	/* Budgets are a safety net, not the count: the forest field decides how many trees there are,
		and it scales with the ground because it is sampled per cell. A budget spent in scan order
		would put every tree in the top of the map, so it sits high enough that an ordinary map
		never reaches it. */
	Real area = playable * playable;
	Int treeBudget = (Int)(area / RMG_CELLS_PER_TREE);
	Int rockBudget = (Int)(area / RMG_CELLS_PER_ROCK);
	Int propID = 1;

	for( Int y = 2; y < m_settings.m_playableCells - 2; y += RMG_PROP_STRIDE )
	{
		for( Int x = 2; x < m_settings.m_playableCells - 2; x += RMG_PROP_STRIDE )
		{
			if( treeBudget <= 0 && rockBudget <= 0 )
				return;

			Int mapX = x + RMG_BORDER_CELLS;
			Int mapY = y + RMG_BORDER_CELLS;

			if( !passableAtCell( mapX, mapY ) )
				continue;

			Real distanceToShore;
			insideLake( (Real)x, (Real)y, &distanceToShore );
			if( distanceToShore < 3.0f )
				continue;

			// Bases and everything already placed keep their elbow room.
			if( distanceToNearestStart( (Real)x, (Real)y ) < RMG_BLEND_RADIUS )
				continue;
			if( !siteIsClear( (Real)x, (Real)y, 3.0f ) )
				continue;

			UnsignedInt hash = hashCell( m_settings.m_seed + 7717, x, y );
			Real jitterX = (Real)(hash % 100U) / 100.0f - 0.5f;
			Real jitterY = (Real)((hash >> 7) % 100U) / 100.0f - 0.5f;
			Real angle = (Real)((hash >> 14) % 1024U) * (2.0f * PI / 1024.0f);

			/* Steep ground, by the same rule that paints it as rock. Asking the texture classes
				would be reading a field that is not built yet: they are worked out after the
				objects, because every pad an object levels moves the slopes they come from. */
			Bool rocky = cellSpanWorld( mapX, mapY ) > RMG_CLIFF_WORLD_SPAN * 0.45f;
			if( rocky )
			{
				if( rockBudget <= 0 )
					continue;
				if( (hash >> 24) % 100U > 25U )
					continue;

				AsciiString uniqueID;
				uniqueID.format( "Prop %d", propID++ );
				addObject( theRockNames[(hash >> 5) % (UnsignedInt)numRockNames], uniqueID.str(),
									 (Real)x + jitterX, (Real)y + jitterY, angle );
				rockBudget--;
				continue;
			}

			if( treeBudget <= 0 )
				continue;

			/* Where a wood is, and how deep into it this cell is, both come out of the same field,
				warped by a second one so the edge of a wood is ragged rather than a contour line.
				The chance climbs towards the middle of a wood, which is what makes a stand thick in
				the centre and thin at the edges instead of an even sprinkle with a hard border. */
			Real forestX = (Real)x * forestScale + 100.0f;
			Real forestY = (Real)y * forestScale - 100.0f;
			Real warpX = forestX + 0.6f * fractalNoise( perm, forestX * 2.3f, forestY * 2.3f, 2 );
			Real warpY = forestY + 0.6f * fractalNoise( perm, forestX * 2.3f + 9.0f,
																									forestY * 2.3f - 4.0f, 2 );

			Real density = fractalNoise( perm, warpX, warpY, 4 );
			if( density < RMG_FOREST_THRESHOLD )
				continue;

			Real chance = (density - RMG_FOREST_THRESHOLD) * 0.55f;
			if( chance > 0.16f )
				chance = 0.16f;
			if( (Real)((hash >> 24) % 1000U) / 1000.0f > chance )
				continue;

			// The wood, not the tree, picks the species.
			UnsignedInt woodHash = hashCell( m_settings.m_seed + 40009, x / 16, y / 16 );

			AsciiString uniqueID;
			uniqueID.format( "Prop %d", propID++ );
			addObject( theTreeNames[woodHash % (UnsignedInt)numTreeNames], uniqueID.str(),
								 (Real)x + jitterX, (Real)y + jitterY, angle );
			treeBudget--;
		}
	}
}

void RMGLayout::buildObjects( const UnsignedByte perm[512] )
{
	m_objects.clear();
	m_sites.clear();

	Int waypointID = 1;
	for( UnsignedInt i = 0; i < m_starts.size(); i++ )
	{
		RMGObject waypoint;
		waypoint.m_templateName = "*Waypoints/Waypoint";
		waypoint.m_uniqueID.format( "Player_%d_Start", i + 1 );
		waypoint.m_worldX = m_starts[i].m_cellX * MAP_XY_FACTOR;
		waypoint.m_worldY = m_starts[i].m_cellY * MAP_XY_FACTOR;
		waypoint.m_angle = 0.0f;
		waypoint.m_waypointID = waypointID++;
		waypoint.m_flags = 0;
		m_objects.push_back( waypoint );

		reserveSite( m_starts[i].m_cellX, m_starts[i].m_cellY, RMG_FLAT_RADIUS );
	}

	placeSupplyAndDerricks();
	placeTowns();
	placeBunkers();
	placeShoreWaves();
	placeScenery( perm );
}

//-----------------------------------------------------------------------------
// The whole layout
//-----------------------------------------------------------------------------

void RMGLayout::build( const RandomMapSettings& settings )
{
	/* The height field is x87 arithmetic, so it comes out differently at 53-bit
		precision than at the 24 bits the simulation runs in - the same trap
		GameLogic::update re-asserts the control word for at the top of every logic
		frame. A map generated in whatever mode the last DLL left behind is a map
		the other machine does not have, so put the FPU where the simulation keeps
		it and give the caller back what it had. */
	UnsignedInt callersFPMode = getFPMode();
	setFPMode();

	m_settings = settings;
	RandomMapGenerator::clampSettings( m_settings );

	m_width = m_settings.m_playableCells + 2 * RMG_BORDER_CELLS;
	m_height = m_width;
	m_waterHeight = RMG_BASE_HEIGHT - RMG_WATER_DROP;
	m_startSearchStride = RMG_START_STRIDE;

	UnsignedByte perm[512];
	seedPermutation( m_settings.m_seed, perm );

	buildHeights( perm );
	placeLakes( perm );
	buildLakeMask();
	carveLakeBasins();

	chooseStarts();
	flattenBases();

	m_ramps.clear();
	connectStarts();

	/* Objects before textures, because placing them changes the ground: every dock, derrick,
		bunker and town levels a pad under itself, and a pad moves the cliffs and the shore lines
		that the passability and the texture classes are read from. */
	buildObjects( perm );
	buildPassability();

	buildTerrainClasses( perm );
	buildBlends();

	_controlfp( callersFPMode, _MCW_PC | _MCW_RC );
}

//-----------------------------------------------------------------------------
// Map pieces
//-----------------------------------------------------------------------------

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

/** A road point. It is not an object the game builds: the renderer walks the map object list, and
	a pair of these with the road flags on them becomes a road segment of whatever type the name
	says. The dictionary is what every map object carries, minus everything that only means
	something to a thing that exists in the world. */
static void writeRoadPoint( MapChunkWriter& w, const RMGObject& object )
{
	w.openChunk( "Object", K_OBJECTS_VERSION_3 );
		w.writeReal( object.m_worldX );
		w.writeReal( object.m_worldY );
		w.writeReal( 0.0f );
		w.writeReal( 0.0f );
		w.writeInt( object.m_flags );
		w.writeAsciiString( object.m_templateName.str() );

		w.beginDict( 1 );
		w.dictAsciiString( "objectLayer", "" );
	w.closeChunk();
}

static void writeNeutralObject( MapChunkWriter& w, const RMGObject& object )
{
	w.openChunk( "Object", K_OBJECTS_VERSION_3 );
		w.writeReal( object.m_worldX );
		w.writeReal( object.m_worldY );
		w.writeReal( 0.0f );
		w.writeReal( object.m_angle );
		w.writeInt( object.m_flags );
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
	// One point per outline sample, so the polygon the engine floods is exactly the basin that was
	// carved into the ground rather than a circle drawn over it.
	const Int numSides = RMG_LAKE_POINTS;

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
				Real radius = layout.m_lakes[i].m_outline[point];
				Real x = layout.m_lakes[i].m_cellX + radius * Cos( angle );
				Real y = layout.m_lakes[i].m_cellY + radius * Sin( angle );

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

Int RandomMapGenerator::cellsFor( RandomMapSize size, Int numPlayers )
{
	if( numPlayers < MIN_PLAYERS ) numPlayers = MIN_PLAYERS;
	if( numPlayers > MAX_PLAYERS ) numPlayers = MAX_PLAYERS;

	Int cells;
	switch( size )
	{
		case RANDOM_MAP_SIZE_SMALL:
			cells = RMG_SMALL_FLOOR + RMG_SMALL_PER_PLAYER * numPlayers;
			break;
		case RANDOM_MAP_SIZE_LARGE:
			cells = RMG_LARGE_FLOOR + RMG_LARGE_PER_PLAYER * numPlayers;
			break;
		default:
			cells = RMG_NORMAL_FLOOR + RMG_NORMAL_PER_PLAYER * numPlayers;
			break;
	}

	if( cells < MIN_CELLS ) cells = MIN_CELLS;
	if( cells > MAX_CELLS ) cells = MAX_CELLS;
	return cells;
}

void RandomMapGenerator::clampSettings( RandomMapSettings& settings )
{
	if( settings.m_numPlayers < MIN_PLAYERS ) settings.m_numPlayers = MIN_PLAYERS;
	if( settings.m_numPlayers > MAX_PLAYERS ) settings.m_numPlayers = MAX_PLAYERS;

	// Nought means "whatever this many players need".
	if( settings.m_playableCells <= 0 )
		settings.m_playableCells = cellsFor( RANDOM_MAP_SIZE_NORMAL, settings.m_numPlayers );

	if( settings.m_playableCells < MIN_CELLS ) settings.m_playableCells = MIN_CELLS;
	if( settings.m_playableCells > MAX_CELLS ) settings.m_playableCells = MAX_CELLS;
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
			w.writeBytes( &layout.m_blendIndex[0], dataSize * sizeof(Short) );
			w.writeBytes( &zeroes[0], dataSize * sizeof(Short) );	// extra blend tiles
			w.writeBytes( &zeroes[0], dataSize * sizeof(Short) );	// cliff info

			w.writeInt( RMG_TERRAIN_COUNT * RMG_TILES_PER_CLASS );	// bitmap tiles
			w.writeInt( (Int)layout.m_blends.size() );
			w.writeInt( 1 );					// cliff infos: entry 0 is the default

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

			// The blend table, entry 0 excepted: it is the "no blend" default and
			// is never written.
			for( UnsignedInt i = 1; i < layout.m_blends.size(); i++ )
			{
				w.writeInt( layout.m_blends[i].m_blendTileIndex );
				w.writeByte( layout.m_blends[i].m_horizontal );
				w.writeByte( layout.m_blends[i].m_vertical );
				w.writeByte( layout.m_blends[i].m_rightDiagonal );
				w.writeByte( layout.m_blends[i].m_leftDiagonal );
				w.writeByte( layout.m_blends[i].m_inverted );
				w.writeByte( layout.m_blends[i].m_longDiagonal );
				w.writeInt( -1 );				// no custom blend edge class: use the alpha
				w.writeInt( RMG_BLEND_FLAG_VALUE );
			}
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
			else if( layout.m_objects[i].m_flags != 0 )
				writeRoadPoint( w, layout.m_objects[i] );
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
		{ 190, 176, 130 },		// sand
		{ 132, 112,  74 },		// dirt
		{ 118, 118, 112 }		// rock
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

			if( layout.underwaterAtCell( cellX, cellY ) )
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
			Real px = (Real)(cellX - RMG_BORDER_CELLS);
			Real py = (Real)(cellY - RMG_BORDER_CELLS);
			for( UnsignedInt i = 0; i < layout.m_starts.size(); i++ )
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
// Generated maps, kept in memory where the map cache will find them
//-----------------------------------------------------------------------------

/** A generated map lives here and nowhere else. The seed, the player count and the size are in the
	path, so anything that names the map - a replay, a save, a lobby telling the other machines what
	is being played - names everything the generator needs to build the same bytes again. That is
	what lets the map stay out of the file system entirely: a machine that has never seen this seed
	rebuilds it from the name the moment something opens it.

	Three maps are kept. Rerolling in the menu walks through them, and a map that falls off the end
	is not lost, only forgotten: the next open of that path builds it again. */
enum { RMG_MAPS_KEPT = 3 };

struct RMGStagedMap
{
	RandomMapSettings m_settings;		///< what it was built from, which is what a slot is looked up by
	AsciiString m_mapPath;					///< lowercase, the path the rest of the game names it by
	std::vector<char> m_mapBytes;
	std::vector<char> m_previewBytes;
	UnsignedInt m_stagedAt;					///< which staging this was, so the oldest can go first
};

static RMGStagedMap theStagedMaps[ RMG_MAPS_KEPT ];
static UnsignedInt theStagingCount = 0;

/** Where a generated map's bytes would live, given its settings. The map cache expects
	"<user maps>\<name>\<name>.map" - the directory carries the name - and getMapPreviewImage wants
	"<name>.tga" beside it. */
static void generatedMapPathsFor( const RandomMapSettings& clamped, AsciiString& mapPath,
																	AsciiString& previewPath )
{
	AsciiString name;
	name.format( "RMG_v%d_%d_%dp_%dc", RANDOM_MAP_GENERATOR_VERSION, clamped.m_seed,
							 clamped.m_numPlayers, clamped.m_playableCells );

	AsciiString dir;
	dir.format( "%sMaps\\%s", TheGlobalData->getPath_UserData().str(), name.str() );
	mapPath.format( "%s\\%s.map", dir.str(), name.str() );
	previewPath.format( "%s\\%s.tga", dir.str(), name.str() );

	mapPath.toLower();
	previewPath.toLower();
}

/** Read the settings back out of a generated map's file name. FALSE for anything else, including a
	name written by another generator version - those bytes cannot be rebuilt here. */
static Bool settingsFromGeneratedPath( const AsciiString& path, RandomMapSettings& settingsOut )
{
	const char *leafStart = path.reverseFind( '\\' );
	AsciiString leaf = leafStart ? leafStart + 1 : path.str();

	// every path the game hands around has been through toLower somewhere, and a name is a name
	// whichever case it arrives in
	leaf.toLower();

	Int version = 0, seed = 0, players = 0, cells = 0;
	if( sscanf( leaf.str(), "rmg_v%d_%d_%dp_%dc", &version, &seed, &players, &cells ) != 4 )
		return FALSE;

	if( version != RANDOM_MAP_GENERATOR_VERSION )
		return FALSE;

	settingsOut.m_seed = seed;
	settingsOut.m_numPlayers = players;
	settingsOut.m_playableCells = cells;

	// a name carrying settings the generator would clamp names bytes it never produced
	RandomMapSettings clamped = settingsOut;
	RandomMapGenerator::clampSettings( clamped );
	return clamped.m_seed == settingsOut.m_seed
			&& clamped.m_numPlayers == settingsOut.m_numPlayers
			&& clamped.m_playableCells == settingsOut.m_playableCells;
}

Bool isGeneratedMapPath( const AsciiString& path )
{
	RandomMapSettings settings;
	return settingsFromGeneratedPath( path, settings );
}

/** The slot holding this map, or NULL.  Slots are looked up by what they were built from rather
	than by the path that asked for them: the same map is named several ways over a run - the switch
	that made it, the lobby, the loader, the preview - and two spellings of one map would otherwise
	each build their own copy. */
static RMGStagedMap *findStagedMap( const RandomMapSettings& settings )
{
	for( Int i = 0; i < RMG_MAPS_KEPT; i++ )
	{
		if( theStagedMaps[i].m_mapBytes.empty() )
			continue;

		if( theStagedMaps[i].m_settings.m_seed == settings.m_seed
				&& theStagedMaps[i].m_settings.m_numPlayers == settings.m_numPlayers
				&& theStagedMaps[i].m_settings.m_playableCells == settings.m_playableCells )
			return &theStagedMaps[i];
	}

	return NULL;
}

/// Build a map into the slot that has been unused longest.
static RMGStagedMap *stageMap( const RandomMapSettings& clamped )
{
	RMGStagedMap *slot = &theStagedMaps[0];
	for( Int i = 1; i < RMG_MAPS_KEPT; i++ )
	{
		if( theStagedMaps[i].m_stagedAt < slot->m_stagedAt )
			slot = &theStagedMaps[i];
	}

	AsciiString previewPath;
	generatedMapPathsFor( clamped, slot->m_mapPath, previewPath );
	slot->m_settings = clamped;
	slot->m_mapBytes.clear();
	slot->m_previewBytes.clear();
	RandomMapGenerator::generate( clamped, slot->m_mapBytes );
	RandomMapGenerator::generatePreview( clamped, slot->m_previewBytes );
	slot->m_stagedAt = ++theStagingCount;

	DEBUG_LOG(("random map: built '%s' - %d players, %d cells, %d bytes, fingerprint %X\n",
		slot->m_mapPath.str(), clamped.m_numPlayers, clamped.m_playableCells, slot->m_mapBytes.size(),
		RandomMapGenerator::fingerprint( clamped )));

	return slot;
}

Bool stageRandomMap( const RandomMapSettings& settings, AsciiString& mapPathOut )
{
	RandomMapSettings clamped = settings;
	RandomMapGenerator::clampSettings( clamped );

	AsciiString mapPath, previewPath;
	generatedMapPathsFor( clamped, mapPath, previewPath );

	if( findStagedMap( clamped ) == NULL )
		stageMap( clamped );

	mapPathOut = mapPath;
	return TRUE;
}

Bool generatedMapBytes( const AsciiString& path, const char **bytesOut, Int *sizeOut )
{
	RandomMapSettings settings;
	if( !settingsFromGeneratedPath( path, settings ) )
		return FALSE;

	RandomMapSettings clamped = settings;
	RandomMapGenerator::clampSettings( clamped );

	RMGStagedMap *slot = findStagedMap( clamped );
	if( slot == NULL )
	{
		// nothing has this seed in hand - a replay, a save or a joined game naming a map this
		// machine has never built.  The name says how to build it, so build it
		slot = stageMap( clamped );
	}

	AsciiString lower = path;
	lower.toLower();

	const std::vector<char>& bytes = lower.endsWith( ".tga" ) ? slot->m_previewBytes
																														: slot->m_mapBytes;
	if( bytes.empty() )
		return FALSE;

	if( bytesOut )
		*bytesOut = &bytes[0];
	if( sizeOut )
		*sizeOut = (Int)bytes.size();

	return TRUE;
}

void generatedMapPaths( std::vector<AsciiString>& pathsOut )
{
	for( Int i = 0; i < RMG_MAPS_KEPT; i++ )
	{
		if( !theStagedMaps[i].m_mapBytes.empty() )
			pathsOut.push_back( theStagedMaps[i].m_mapPath );
	}
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
