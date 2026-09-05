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

// FILE: W3DInGameUI.cpp //////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2001
// Desct:	 In game user interface implementation for W3D
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>

#include "Common/GlobalData.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/ControlBar.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DGUICallbacks.h"
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/Common/W3DConvert.h"
#include "WW3D2/WW3D.h"
#include "WW3D2/HAnim.h"
#include "WW3D2/DX8Wrapper.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/vertmaterial.h"
#include "WW3D2/shader.h"

#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.		 

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


#ifdef _DEBUG
#include "W3DDevice/GameClient/HeightMap.h"
#include "WW3D2/DX8IndexBuffer.h"
#include "WW3D2/DX8VertexBuffer.h"
#include "WW3D2/VertMaterial.h"
class DebugHintObject : public RenderObjClass
{	

public:

	DebugHintObject(void);
	DebugHintObject(const DebugHintObject & src);
	DebugHintObject & operator = (const DebugHintObject &);
	~DebugHintObject(void);

	virtual RenderObjClass *	Clone(void) const;
	virtual int						Class_ID(void) const;
	virtual void					Render(RenderInfoClass & rinfo);
	virtual Bool					Cast_Ray(RayCollisionTestClass & raytest);

	virtual void					Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const;
  virtual void					Get_Obj_Space_Bounding_Box(AABoxClass & aabox) const;

	int updateBlock(void);
	void freeMapResources(void);
	void setLocAndColorAndSize(const Coord3D *loc, Int argb, Int size);

protected:

	Coord3D m_myLoc;
	Int m_myColor;	// argb
	Int m_mySize;

	DX8IndexBufferClass				*m_indexBuffer;
	ShaderClass								m_shaderClass; //shader or rendering state for heightmap
	VertexMaterialClass	  	  *m_vertexMaterialClass;
	DX8VertexBufferClass			*m_vertexBufferTile;	//First vertex buffer.

	void initData(void);
};

// Texturing, no zbuffer, disabled zbuffer write, primary gradient, alpha blending
#define SC_ALPHA ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )


DebugHintObject::~DebugHintObject(void)
{
	freeMapResources();
}

DebugHintObject::DebugHintObject(void) :
	m_indexBuffer(NULL),
	m_vertexMaterialClass(NULL),
	m_vertexBufferTile(NULL),
	m_myColor(0),
	m_mySize(0)
{
	initData();
}

Bool DebugHintObject::Cast_Ray(RayCollisionTestClass & raytest)
{
	return false;	
}

DebugHintObject::DebugHintObject(const DebugHintObject & src)
{
	*this = src;
}

DebugHintObject & DebugHintObject::operator = (const DebugHintObject & that)
{
	DEBUG_CRASH(("oops"));
	return *this;
}

void DebugHintObject::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	Vector3	ObjSpaceCenter((float)1000*0.5f,(float)1000*0.5f,(float)0);
	float length = ObjSpaceCenter.Length();
	sphere.Init(ObjSpaceCenter, length);
}

void DebugHintObject::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	Vector3	minPt(0,0,0);
	Vector3	maxPt((float)1000,(float)1000,(float)1000);
	box.Init(minPt,maxPt);
}

Int DebugHintObject::Class_ID(void) const
{
	return RenderObjClass::CLASSID_UNKNOWN;
}

RenderObjClass * DebugHintObject::Clone(void) const
{
	DEBUG_CRASH(("oops"));
	return NEW DebugHintObject(*this);
}


void DebugHintObject::freeMapResources(void)
{
	REF_PTR_RELEASE(m_indexBuffer);
	REF_PTR_RELEASE(m_vertexBufferTile);
	REF_PTR_RELEASE(m_vertexMaterialClass);
}

//Allocate a heightmap of x by y vertices.
//data must be an array matching this size.
void DebugHintObject::initData(void)
{	
	freeMapResources();	//free old data and ib/vb

	m_indexBuffer = NEW_REF(DX8IndexBufferClass,(3));

	// Fill up the IB
	{
		DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
		ib[0]=0;
		ib[1]=1;
		ib[2]=2;
	}

	m_vertexBufferTile = NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZDUV1,3,DX8VertexBufferClass::USAGE_DEFAULT));

	//go with a preset material for now.
	m_vertexMaterialClass = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);

	//use a multi-texture shader: (text1*diffuse)*text2.
	m_shaderClass = ShaderClass::ShaderClass(SC_ALPHA);
}

void DebugHintObject::setLocAndColorAndSize(const Coord3D *loc, Int argb, Int size)
{
	m_myLoc = *loc;
	m_myColor = argb;
	m_mySize = size;

	if (m_myLoc.z < 0 && TheTerrainRenderObject) 
	{
		m_myLoc.z = TheTerrainRenderObject->getHeightMapHeight(m_myLoc.x, m_myLoc.y, NULL);
	}

	if (m_vertexBufferTile)
	{
		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexBufferTile);
		VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();

		Real x1 = m_mySize * 0.866;	// cos(30)
		Real y1 = m_mySize * 0.5;		// sin(30)
		
		// note, pts must go in a counterclockwise order!
		vb[0].x = 0;
		vb[0].y = m_mySize;
		vb[0].z = 0;
		vb[0].diffuse = m_myColor;
		vb[0].u1 = 0;
		vb[0].v1 = 0;

		vb[1].x = -x1;
		vb[1].y = -y1;
		vb[1].z = 0;
		vb[1].diffuse = m_myColor;
		vb[1].u1 = 0;
		vb[1].v1 = 0;

		vb[2].x = x1;
		vb[2].y = -y1;
		vb[2].z = 0;
		vb[2].diffuse = m_myColor;
		vb[2].u1 = 0;
		vb[2].v1 = 0;
	}
}

void DebugHintObject::Render(RenderInfoClass & rinfo)
{
	SphereClass bounds(Vector3(m_myLoc.x, m_myLoc.y, m_myLoc.z), m_mySize); 
	if (!rinfo.Camera.Cull_Sphere(bounds)) 
	{
		DX8Wrapper::Set_Material(m_vertexMaterialClass);
		DX8Wrapper::Set_Shader(m_shaderClass);
		DX8Wrapper::Set_Texture(0, NULL);
		DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
		DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile);

		Matrix3D tm(Transform);
		Vector3 vec(m_myLoc.x, m_myLoc.y, m_myLoc.z);
		tm.Set_Translation(vec);
		DX8Wrapper::Set_Transform(D3DTS_WORLD, tm);

		DX8Wrapper::Draw_Triangles(	0, 1, 0, 3);
	}
}
#endif // _DEBUG


///////////////////////////////////////////////////////////////////////////////////////////////////
// DEFINITIONS
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DInGameUI::W3DInGameUI()
{
	Int i;

	for( i = 0; i < MAX_MOVE_HINTS; i++ )
	{

		m_moveHintRenderObj[ i ] = NULL;
		m_moveHintAnim[ i ] = NULL;

	}  // end for i

	m_buildingPlacementAnchor = NULL;
	m_buildingPlacementArrow = NULL;

}  // end W3DInGameUI

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DInGameUI::~W3DInGameUI()
{
	Int i;

	// remove render objects for hints
	for( i = 0; i < MAX_MOVE_HINTS; i++ )
	{

		REF_PTR_RELEASE( m_moveHintRenderObj[ i ] );
		REF_PTR_RELEASE( m_moveHintAnim[ i ] );

	}  // end for i

	REF_PTR_RELEASE( m_buildingPlacementAnchor );
	REF_PTR_RELEASE( m_buildingPlacementArrow );

}  // end ~W3DInGameUI

// loadText ===================================================================
/** Load text from the file */
//=============================================================================
static void loadText( char *filename, GameWindow *listboxText )
{
	if (!listboxText)
		return;
	GadgetListBoxReset(listboxText);

	FILE *fp;

	// open the file
	fp = fopen( filename, "r" );
	if( fp == NULL )
		return;

	char buffer[ 1024 ];
	UnicodeString line;
	Color color = GameMakeColor(255, 255, 255, 255);
	while( fgets( buffer, 1024, fp ) != NULL )
	{
		line.translate(buffer);
		line.trim();
		if (line.isEmpty())
			line = UnicodeString(L" ");
		GadgetListBoxAddEntryText(listboxText, line, color, -1, -1);
	}  // end while

	// close the file
	fclose( fp );

}  // end loadText

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::init( void )
{

	// extending functionality
	InGameUI::init();
	// for the beta, they didn't want the help menu showing up, but I left this as a bock
	// comment because we'll probably want to add this back in.
/*
		// create the MOTD
		GameWindow *motd = TheWindowManager->winCreateFromScript( AsciiString("MOTD.wnd") );
		if( motd )
		{
			NameKeyType listboxTextID = TheNameKeyGenerator->nameToKey( "MOTD.wnd:ListboxMOTD" );
			GameWindow *listboxText = TheWindowManager->winGetWindowFromId(motd, listboxTextID);
	
			loadText( "HelpScreen.txt", listboxText );
	
			// hide it for now
			motd->winHide( TRUE );
	
		}  // end if*/
	
		
}  // end init

//-------------------------------------------------------------------------------------------------
/** Update in game UI */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::update( void )
{

	// call base
	InGameUI::update();

}  // end update

//-------------------------------------------------------------------------------------------------
/** Reset the in game ui */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::reset( void )
{

	// call base
	InGameUI::reset();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Draw member for the W3D implemenation of the game user interface */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::draw( void )
{
	preDraw();

	// draw selection region if drag selecting
	if( m_isDragSelecting )
		drawSelectionRegion();

	// for each view draw hints
	/// @todo should the UI be iterating through views like this?
	if( TheDisplay )
	{
		View *view;

		for( view = TheDisplay->getFirstView();
				 view;
				 view = TheDisplay->getNextView( view ) )
		{

			// draw move hints
			drawMoveHints( view );

			// draw attack hints
			drawAttackHints( view );

			// draw placement angle selection if needed
			drawPlaceAngle( view );

		}  // end for view

	}  // end if

	// repaint all our windows

#ifdef EXTENDED_STATS
	if (!DX8Wrapper::stats.m_disableConsole) {
#endif

#ifdef DO_UNIT_TIMINGS	 
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
	extern Bool g_UT_startTiming;
	if (!g_UT_startTiming)
#endif

#ifdef DEBUG_LOGGING
	//
	// The interface's own half of a frame, split again: the overlays and strips this class draws
	// over the world, and the window system's repaint of the control bar and every dialog.
	//
	extern Real TheUIPostDrawMS;
	extern Real TheWindowRepaintMS;
	Int64 tPostStart, tPostEnd, tWinEnd, freq;
	QueryPerformanceCounter( (LARGE_INTEGER *)&tPostStart );
#endif

	postDraw();

#ifdef DEBUG_LOGGING
	QueryPerformanceCounter( (LARGE_INTEGER *)&tPostEnd );
#endif

	TheWindowManager->winRepaint();

#ifdef DEBUG_LOGGING
	QueryPerformanceCounter( (LARGE_INTEGER *)&tWinEnd );
	QueryPerformanceFrequency( (LARGE_INTEGER *)&freq );
	if( freq > 0 )
	{
		TheUIPostDrawMS = (Real)((double)(tPostEnd - tPostStart) * 1000.0 / (double)freq);
		TheWindowRepaintMS = (Real)((double)(tWinEnd - tPostEnd) * 1000.0 / (double)freq);
	}
#endif

	//
	// The clock/rate plate goes on last, after every window has painted. In postDraw with the rest
	// of the world overlays it was drawn before the window manager, so anything the manager put on
	// top of it - a dialog, the menu, the control bar's own art at the top of the screen - covered
	// the one reading you want visible exactly when something is going wrong.
	//
	// the peace time countdown first: it owns the top of the corner and the clock plate sits under it
	drawPeaceTimer();
	drawHudOverlay();
	
#ifdef EXTENDED_STATS
	}
#endif

}  // end draw

//-------------------------------------------------------------------------------------------------
// The build grid's own render state.  Alpha blended, untextured, depth tested but never depth
// written, and PASS_ALWAYS: the grid is a sheet lying exactly on the terrain, so anything that
// compared depth against the terrain would z-fight with it.  Drawing it in the terrain pass and
// never writing depth means everything drawn after the terrain - buildings, units, trees, the
// placement ghost itself - covers it, which is what makes it read as paint on the ground.
// This is the bibs' shader with texturing off (see W3DBibBuffer).
#define SC_BUILD_GRID ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_DISABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

/** rgb plus an alpha given as a float 0..255, clamped - the grid's colours are all one colour at
	* a per-vertex strength. */
static UnsignedInt gridColor( UnsignedInt rgb, Real alpha )
{
	Int a = REAL_TO_INT( alpha );
	if( a < 0 )
		a = 0;
	else if( a > 255 )
		a = 255;
	return rgb | ((UnsignedInt)a << 24);
}

/** Batches the grid's quads through the dynamic vertex buffer.  The patch is ~1700 line quads plus
	* a fill for each blocked cell, which is more than one dynamic lock wants to hold, so it flushes
	* in fixed chunks - the draw state is set once by the caller and holds across the flushes. */
class BuildGridQuads
{
public:
	BuildGridQuads( void ) : m_quads( 0 ) { }

	void add( const Vector3 &p0, const Vector3 &p1, const Vector3 &p2, const Vector3 &p3,
						UnsignedInt c0, UnsignedInt c1, UnsignedInt c2, UnsignedInt c3 )
	{
		if( m_quads >= MAX_QUADS )
			flush();

		Vert *v = &m_verts[ m_quads * 4 ];
		v[ 0 ].pos = p0;  v[ 0 ].diffuse = c0;
		v[ 1 ].pos = p1;  v[ 1 ].diffuse = c1;
		v[ 2 ].pos = p2;  v[ 2 ].diffuse = c2;
		v[ 3 ].pos = p3;  v[ 3 ].diffuse = c3;
		++m_quads;
	}

	void flush( void );

private:
	enum { MAX_QUADS = 512 };
	struct Vert
	{
		Vector3 pos;
		UnsignedInt diffuse;
	};
	Vert m_verts[ MAX_QUADS * 4 ];
	Int m_quads;
};

void BuildGridQuads::flush( void )
{
	if( m_quads == 0 )
		return;

	const Int quads = m_quads;
	m_quads = 0;		// whatever happens below, this batch is spent

	DynamicVBAccessClass vbAccess( BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, quads * 4 );
	DynamicIBAccessClass ibAccess( BUFFER_TYPE_DYNAMIC_DX8, quads * 6 );
	{
		DynamicVBAccessClass::WriteLockClass vbLock( &vbAccess );
		DynamicIBAccessClass::WriteLockClass ibLock( &ibAccess );
		VertexFormatXYZNDUV2 *vb = vbLock.Get_Formatted_Vertex_Array();
		UnsignedShort *ib = ibLock.Get_Index_Array();
		if( vb == NULL || ib == NULL )
			return;

		for( Int q = 0; q < quads; ++q )
		{
			for( Int i = 0; i < 4; ++i, ++vb )
			{
				const Vert &src = m_verts[ q * 4 + i ];
				vb->x = src.pos.X;
				vb->y = src.pos.Y;
				vb->z = src.pos.Z;
				vb->nx = 0.0f;
				vb->ny = 0.0f;
				vb->nz = 1.0f;
				vb->diffuse = src.diffuse;
				vb->u1 = 0.0f;
				vb->v1 = 0.0f;
				vb->u2 = 0.0f;
				vb->v2 = 0.0f;
			}

			*ib++ = (UnsignedShort)(q * 4);
			*ib++ = (UnsignedShort)(q * 4 + 1);
			*ib++ = (UnsignedShort)(q * 4 + 2);
			*ib++ = (UnsignedShort)(q * 4);
			*ib++ = (UnsignedShort)(q * 4 + 2);
			*ib++ = (UnsignedShort)(q * 4 + 3);
		}
	}

	DX8Wrapper::Set_Index_Buffer( ibAccess, 0 );
	DX8Wrapper::Set_Vertex_Buffer( vbAccess );
	DX8Wrapper::Draw_Triangles( 0, quads * 2, 0, quads * 4 );
}

//-------------------------------------------------------------------------------------------------
/** Draw the pathfinder's own cell grid under the structure sitting on the cursor, and cross out
	* the cells it cannot go on.  GridBuildPlacement snaps a footprint's edges to these very lines
	* (see snapPlacementToGrid), so being able to see them is the difference between guessing at a
	* flush row of buildings and laying one out.
	*
	* Only the cells around the cursor are drawn.  The whole map's worth would be a wall of lines,
	* and the ones being aimed at are the ones worth seeing - so the lines also fade out towards the
	* edge of the patch instead of ending on a hard square.
	*
	* It is drawn as quads lying on the terrain, from inside the terrain pass
	* (HeightMapRenderObjClass::Render calls it right after the bibs), not as a screen space
	* overlay: painted on the ground it is read at a glance, and everything drawn after the terrain
	* covers it, so a building never has grid lines crawling over its roof.
	*
	* "Cannot build" here is the pathfinder cell's own type: water, a cliff, rubble, an existing
	* structure, plain impassable.  It deliberately does not run the full isLocationLegalToBuild for
	* every cell - that is a per-structure query (build radius, shroud, supply proximity) and a
	* couple of hundred of them a frame is not worth it.  The ghost's own red tint already answers
	* that question for the one spot the cursor is actually on. */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawBuildGrid( void )
{
	// the grid you see is the grid you snap to: with the snap off it would mean nothing
	if( m_pendingPlaceType == NULL || TheGlobalData->m_gridBuildPlacement == FALSE )
		return;
	if( m_placeIcon == NULL || m_placeIcon[ 0 ] == NULL )
		return;
	if( TheTerrainLogic == NULL || TheAI == NULL )
		return;

	Pathfinder *pathfinder = TheAI->pathfinder();
	if( pathfinder == NULL )
		return;

	const Coord3D *center = m_placeIcon[ 0 ]->getPosition();
	if( center == NULL )
		return;

	// how many cells each way around the cursor we light up
	enum { GRID_RADIUS = 28 };
	enum { GRID_CELLS = GRID_RADIUS * 2 + 1, GRID_POINTS = GRID_CELLS + 1 };

	// half the width of a painted line, in world units - a cell is PLACEMENT_CELL (10) across
	const Real LINE_HALF_WIDTH = 0.45f;
	// the ground is sampled at the line, so a line running across a slope would sink into the hill
	// on one side; lifting the whole sheet a hair keeps it out of the dirt without floating
	const Real GRID_LIFT = 0.35f;

	// the pathfinder's own cell indexing, so the lines drawn are the lines it reasons about
	const Int cellX = REAL_TO_INT_FLOOR( (center->x + 0.5f) / PATHFIND_CELL_SIZE_F ) - GRID_RADIUS;
	const Int cellY = REAL_TO_INT_FLOOR( (center->y + 0.5f) / PATHFIND_CELL_SIZE_F ) - GRID_RADIUS;

	// sample the terrain once per grid corner, rather than once per quad corner.  Static, not
	// automatic: at this radius the two square tables are ~27k together, which is more than a
	// render-path stack frame should be carrying (the quad batch below is static for the same
	// reason).
	Real gx[ GRID_POINTS ], gy[ GRID_POINTS ];
	static Real gz[ GRID_POINTS ][ GRID_POINTS ];
	static Real fade[ GRID_POINTS ][ GRID_POINTS ];
	Int ix, iy;

	for( ix = 0; ix < GRID_POINTS; ++ix )
	{
		gx[ ix ] = placementGridLine( cellX + ix );
		gy[ ix ] = placementGridLine( cellY + ix );
	}

	// the patch fades out to nothing at its edge, measured from the cursor's own cell, so it ends
	// on a soft edge instead of a hard square
	const Real mid = (Real)GRID_RADIUS + 0.5f;
	for( iy = 0; iy < GRID_POINTS; ++iy )
	{
		for( ix = 0; ix < GRID_POINTS; ++ix )
		{
			gz[ iy ][ ix ] = TheTerrainLogic->getGroundHeight( gx[ ix ], gy[ iy ] ) + GRID_LIFT;

			const Real dx = (Real)fabs( ix - mid );
			const Real dy = (Real)fabs( iy - mid );
			const Real d = ( dx > dy ) ? dx : dy;
			const Real f = 1.0f - d / mid;
			fade[ iy ][ ix ] = ( f > 0.0f ) ? f : 0.0f;
		}
	}

	// the state the quads are drawn with: prelit (the colour is all in the vertices), untextured,
	// alpha blended, depth tested but never depth written, and PASS_ALWAYS so a sheet lying on the
	// terrain cannot z-fight with the terrain triangles underneath it.  This is the bibs' own
	// shader, and the bibs are the proof it reads as paint rather than as decal geometry.
	static ShaderClass gridShader( SC_BUILD_GRID );
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset( VertexMaterialClass::PRELIT_DIFFUSE );
	DX8Wrapper::Set_Material( material );
	REF_PTR_RELEASE( material );
	DX8Wrapper::Set_Texture( 0, NULL );
	DX8Wrapper::Set_Shader( gridShader );
	DX8Wrapper::Apply_Render_State_Changes();

	static BuildGridQuads quads;		// 32k of vertices; static so it is not a stack frame

	// the lines themselves, one quad per cell edge so they follow the ground over every bump
	const Real LINE_ALPHA = 0x58;
	for( iy = 0; iy < GRID_POINTS; ++iy )
	{
		for( ix = 0; ix < GRID_POINTS; ++ix )
		{
			if( ix + 1 < GRID_POINTS && ( fade[ iy ][ ix ] > 0.0f || fade[ iy ][ ix + 1 ] > 0.0f ) )
			{
				const UnsignedInt c0 = gridColor( 0x00FFFFFF, LINE_ALPHA * fade[ iy ][ ix ] );
				const UnsignedInt c1 = gridColor( 0x00FFFFFF, LINE_ALPHA * fade[ iy ][ ix + 1 ] );
				quads.add( Vector3( gx[ ix ],     gy[ iy ] - LINE_HALF_WIDTH, gz[ iy ][ ix ] ),
									 Vector3( gx[ ix + 1 ], gy[ iy ] - LINE_HALF_WIDTH, gz[ iy ][ ix + 1 ] ),
									 Vector3( gx[ ix + 1 ], gy[ iy ] + LINE_HALF_WIDTH, gz[ iy ][ ix + 1 ] ),
									 Vector3( gx[ ix ],     gy[ iy ] + LINE_HALF_WIDTH, gz[ iy ][ ix ] ),
									 c0, c1, c1, c0 );
			}

			if( iy + 1 < GRID_POINTS && ( fade[ iy ][ ix ] > 0.0f || fade[ iy + 1 ][ ix ] > 0.0f ) )
			{
				const UnsignedInt c0 = gridColor( 0x00FFFFFF, LINE_ALPHA * fade[ iy ][ ix ] );
				const UnsignedInt c1 = gridColor( 0x00FFFFFF, LINE_ALPHA * fade[ iy + 1 ][ ix ] );
				quads.add( Vector3( gx[ ix ] - LINE_HALF_WIDTH, gy[ iy ],     gz[ iy ][ ix ] ),
									 Vector3( gx[ ix ] + LINE_HALF_WIDTH, gy[ iy ],     gz[ iy ][ ix ] ),
									 Vector3( gx[ ix ] + LINE_HALF_WIDTH, gy[ iy + 1 ], gz[ iy + 1 ][ ix ] ),
									 Vector3( gx[ ix ] - LINE_HALF_WIDTH, gy[ iy + 1 ], gz[ iy + 1 ][ ix ] ),
									 c0, c0, c1, c1 );
			}
		}
	}

	// and a red wash over every cell a structure cannot stand on.  Filling the cell reads at a
	// glance where an X drawn in thin lines did not.
	const Real BLOCKED_ALPHA = 0x44;
	for( iy = 0; iy < GRID_CELLS; ++iy )
	{
		for( ix = 0; ix < GRID_CELLS; ++ix )
		{
			PathfindCell *cell = pathfinder->getCell( LAYER_GROUND, cellX + ix, cellY + iy );
			if( cell == NULL || cell->getType() == PathfindCell::CELL_CLEAR )
				continue;

			if( fade[ iy ][ ix ] <= 0.0f && fade[ iy + 1 ][ ix + 1 ] <= 0.0f )
				continue;

			quads.add( Vector3( gx[ ix ],     gy[ iy ],     gz[ iy ][ ix ] ),
								 Vector3( gx[ ix + 1 ], gy[ iy ],     gz[ iy ][ ix + 1 ] ),
								 Vector3( gx[ ix + 1 ], gy[ iy + 1 ], gz[ iy + 1 ][ ix + 1 ] ),
								 Vector3( gx[ ix ],     gy[ iy + 1 ], gz[ iy + 1 ][ ix ] ),
								 gridColor( 0x00FF3030, BLOCKED_ALPHA * fade[ iy ][ ix ] ),
								 gridColor( 0x00FF3030, BLOCKED_ALPHA * fade[ iy ][ ix + 1 ] ),
								 gridColor( 0x00FF3030, BLOCKED_ALPHA * fade[ iy + 1 ][ ix + 1 ] ),
								 gridColor( 0x00FF3030, BLOCKED_ALPHA * fade[ iy + 1 ][ ix ] ) );
		}
	}

	quads.flush();

}  // end drawBuildGrid

//-------------------------------------------------------------------------------------------------
/** draw 2d selection region on screen */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawSelectionRegion( void )
{
	Real width = 2.0f;
	UnsignedInt color = 0x9933FF33;  //0xAARRGGBB

	TheDisplay->drawOpenRect( m_dragSelectRegion.lo.x,
														m_dragSelectRegion.lo.y,
														m_dragSelectRegion.hi.x - m_dragSelectRegion.lo.x,
														m_dragSelectRegion.hi.y - m_dragSelectRegion.lo.y,
														width,
														color );

}  // end drawSelectionRegion

//-------------------------------------------------------------------------------------------------
/** Draw the visual feedback for clicking in the world and telling units
	* to move there */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawMoveHints( View *view )
{
	Int i;
//	Real width = 1.0f;
//	UnsignedInt color = 0x9933FF33;  //0xAARRGGBB

	for( i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		Int elapsed = TheGameClient->getFrame() - m_moveHint[i].frame;

		if( elapsed <= 40 )
		{
			RectClass rect;

			// if this hint is not in this view ignore it
			/// @todo write this to check if point is visible in view
//			if( view->pointInView( &m_moveHint[ i ].pos == FALSE )
//				continue;

			// create render object and add to scene of needed
			if( m_moveHintRenderObj[ i ] == NULL )
			{
				RenderObjClass *hint;
				HAnimClass *anim;

				// create hint object
				hint = W3DDisplay::m_assetManager->Create_Render_Obj(TheGlobalData->m_moveHintName.str());

				AsciiString animName;
				animName.format("%s.%s", TheGlobalData->m_moveHintName.str(), TheGlobalData->m_moveHintName.str());
				anim = W3DDisplay::m_assetManager->Get_HAnim(animName.str());
	
				// sanity
				if( hint == NULL )
				{

					DEBUG_CRASH(("unable to create hint"));
					return;

				}  // end if

				// asign render objects to GUI data
				m_moveHintRenderObj[ i ] = hint;
				
				// note that 'anim' is returned from Get_HAnim with an AddRef, so we don't need to addref it again.
				// however, we do need to release the contents of moveHintAnim (if any)
				REF_PTR_RELEASE(m_moveHintAnim[i]);
				m_moveHintAnim[i] = anim;
								
			}  // end if, create render objects

			// show the render object if hidden
			if( m_moveHintRenderObj[ i ]->Is_Hidden() == 1 ) {
				m_moveHintRenderObj[ i ]->Set_Hidden( 0 );
				// add to scene
				W3DDisplay::m_3DScene->Add_Render_Object( m_moveHintRenderObj[ i ] );
				if (m_moveHintAnim[i])
					m_moveHintRenderObj[i]->Set_Animation(m_moveHintAnim[i], 0, RenderObjClass::ANIM_MODE_ONCE);
			}

			// move this hint render object to the position and align with terrain
			Matrix3D transform;
			PathfindLayerEnum layer = TheTerrainLogic->alignOnTerrain( 0, m_moveHint[ i ].pos, true, transform );
			
			Real waterZ;
			if (layer == LAYER_GROUND && TheTerrainLogic->isUnderwater(m_moveHint[ i ].pos.x, m_moveHint[ i ].pos.y, &waterZ)) 
			{
				Coord3D tmp = m_moveHint[ i ].pos;
				tmp.z = waterZ;
				Coord3D normal;
				normal.x = 0;
				normal.y = 0;
				normal.z = 1;
				makeAlignToNormalMatrix(0, tmp, normal, transform);
			}

			m_moveHintRenderObj[ i ]->Set_Transform( transform );

#if 0
			// if there is a source then draw line from source to destination
			Object *obj = TheGameLogic->getObject( m_moveHint[ i ].sourceID );
			if( obj )
			{
				Drawable *source = obj->getDrawable();

				if( source )
				{
					Coord3D pos;
					ICoord2D start, end;

					// project start and end point to screen point
					source->getPosition( &pos );
					view->worldToScreen( &pos, &start );
					view->worldToScreen( &hintPos, &end );

					// draw the line
					TheDisplay->drawLine( start.x, start.y, end.x, end.y, width, color );

				}  // end if
			}  // end if
#endif

		}
		else
		{

			// hide hint marker
			if( m_moveHintRenderObj[ i ] )
				if( m_moveHintRenderObj[ i ]->Is_Hidden() == 0 ) {
					m_moveHintRenderObj[ i ]->Set_Hidden( 1 );
					W3DDisplay::m_3DScene->Remove_Render_Object( m_moveHintRenderObj[ i ] );
				}

		}  // end else

	}  // end for i

}  // end drawMoveHints

//-------------------------------------------------------------------------------------------------
/** Draw visual back for clicking to attack a unit in the world */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawAttackHints( View *view )
{

}  // end drawAttackHints

//-------------------------------------------------------------------------------------------------
/** Draw the angle selection for placing building if needed */
//-------------------------------------------------------------------------------------------------
void W3DInGameUI::drawPlaceAngle( View *view )
{
//	Coord2D v, p, o;
	//Real size = 15.0f;

	//Create the anchor & arrow if not already created!
	if( !m_buildingPlacementAnchor )
	{
		m_buildingPlacementAnchor = W3DDisplay::m_assetManager->Create_Render_Obj( "Locater01" );

		// sanity
		if( !m_buildingPlacementAnchor )
		{
			DEBUG_CRASH( ("Unable to create BuildingPlacementAnchor (Locator01.w3d) -- cursor for placing buildings") );
			return;
		}
	}
	if( !m_buildingPlacementArrow )
	{
		m_buildingPlacementArrow = W3DDisplay::m_assetManager->Create_Render_Obj( "Locater02" );

		// sanity
		if( !m_buildingPlacementArrow )
		{
			DEBUG_CRASH( ("Unable to create BuildingPlacementArrow (Locator02.w3d) -- cursor for placing buildings") );
			return;
		}
	}

	Bool anchorInScene = m_buildingPlacementAnchor->Peek_Scene() != NULL;
	Bool arrowInScene	 = m_buildingPlacementArrow->Peek_Scene() != NULL;

	// get out of here if this display isn't up anyway
	if( isPlacementAnchored() == FALSE )
	{
		if( anchorInScene )
		{
			//If our anchor is in the scene, remove it from the scene but don't delete it.
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementAnchor );
		}
		if( arrowInScene )
		{
			//If our arrow is in the scene, remove it from the scene but don't delete it.
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementArrow );
		}
		return;
	}

	// get the anchor points
	ICoord2D start, end;
	getPlacementPoints( &start, &end );




	Coord3D vector;
	vector.x = end.x - start.x;
	vector.y = end.y - start.y;
	vector.z = 0.0f;
	Real length = vector.length();

	Bool showArrow = length >= 5.0f;

	if( showArrow )
	{
		if( anchorInScene )
		{
			//We're switching to the arrow!
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementAnchor );
		}
		if( !arrowInScene )
		{
			W3DDisplay::m_3DScene->Add_Render_Object( m_buildingPlacementArrow );
			arrowInScene = TRUE;
		}
	}
	else
	{
		if( arrowInScene )
		{
			//We're switching to the anchor!
			W3DDisplay::m_3DScene->Remove_Render_Object( m_buildingPlacementArrow );
		}
		if( !anchorInScene )
		{
			W3DDisplay::m_3DScene->Add_Render_Object( m_buildingPlacementAnchor );
			anchorInScene = TRUE;
		}
	}

	//The proper way to orient the placement arrow is to copy the matrix from the m_placeIcon[0]!
	if( anchorInScene )
	{
		if ( m_placeIcon[ 0 ] )
			m_buildingPlacementAnchor->Set_Transform( *m_placeIcon[ 0 ]->getTransformMatrix() );
	}
	else if( arrowInScene )
	{
		if ( m_placeIcon[ 0 ] )
			m_buildingPlacementArrow->Set_Transform( *m_placeIcon[ 0 ]->getTransformMatrix() );
	}

	
	//m_buildingPlacementArrow->Set_Transform(

	// draw a little box at the start to show the "anchor" point
	//Real rectSize = 4.0f;
	//TheDisplay->drawFillRect( start.x - rectSize / 2, start.y - rectSize / 2,
	//													rectSize, rectSize, color );

	// compute vector for line
	//v.x = end.x - start.x;
	//v.y = end.y - start.y;
	//v.normalize();

	// compute opposite vector
	//o.x = -v.x;
	//o.y = -v.y;

	// compute perpendicular vector one way
	//p.x = -v.y;
	//p.y = v.x;

	// draw the line
	//start.x = o.x * size + p.x * (size/2.0f) + end.x;
	//start.y = o.y * size + p.y * (size/2.0f) + end.y;
	//TheDisplay->drawLine( start.x, start.y, end.x, end.y, width, color );

	// compute perpendicular vector other way
	//p.x = v.y;
	//p.y = -v.x;

	// draw the line
	//start.x = o.x * size + p.x * (size/2.0f) + end.x;
	//start.y = o.y * size + p.y * (size/2.0f) + end.y;
	//TheDisplay->drawLine( start.x, start.y, end.x, end.y, width, color );

}  // end drawPlaceAngle

