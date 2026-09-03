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

// LookAtXlat.cpp
// Translate raw input events into camera movement commands
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "windows.h"

#include "Common/GameType.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/StatsCollector.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/GameClient.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Keyboard.h"		// for the ctrl+wheel building rotation
#include "GameClient/View.h"
#include "GameClient/Drawable.h"
#include "GameClient/LookAtXlat.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/GameLogic.h"

#include "Common/GlobalData.h"			// for camera pitch angle only

LookAtTranslator *TheLookAtTranslator = NULL;

static enum
{
	DIR_UP = 0,
	DIR_DOWN,
	DIR_LEFT,
	DIR_RIGHT
};

static Bool scrollDir[4] = { false, false, false, false };

Int SCROLL_AMT = 100;

static const Int edgeScrollSize = 3;

static Mouse::MouseCursor prevCursor = Mouse::ARROW;

//-----------------------------------------------------------------------------
void LookAtTranslator::setScrolling(Int x)
{
	if (!TheInGameUI->getInputEnabled())
		return;

	prevCursor = TheMouse->getMouseCursor();
	m_isScrolling = true;
	TheInGameUI->setScrolling( TRUE );
	TheTacticalView->setMouseLock( TRUE );
	m_scrollType = x;
	if(TheStatsCollector)
		TheStatsCollector->startScrollTime();
}

//-----------------------------------------------------------------------------
void LookAtTranslator::stopScrolling( void )
{
	m_isScrolling = false;
	TheInGameUI->setScrolling( FALSE );
	TheTacticalView->setMouseLock( FALSE );
	TheMouse->setCursor(prevCursor);
	m_scrollType = SCROLL_NONE;
		
	// if we have a stats collectore increment the stats
	if(TheStatsCollector)
		TheStatsCollector->endScrollTime();

}

//-----------------------------------------------------------------------------
LookAtTranslator::LookAtTranslator() :
	m_isScrolling(false),
	m_isRotating(false),
	m_freeRotateAngle(0.0f),
	m_isPitching(false),
	m_isChangingFOV(false),
	m_timestamp(0),
	m_lastPlaneID(INVALID_DRAWABLE_ID),
	m_lastMouseMoveFrame(0),
	m_scrollType(SCROLL_NONE),
	m_zoomAnchorValid(FALSE),
	m_zoomAnchorZoom(0.0f),
	m_zoomAnchorTicks(0)
{
	m_zoomAnchorPixel.x = m_zoomAnchorPixel.y = 0;
	m_zoomAnchorWorld.zero();

	//Added By Sadullah Nader
	//Initializations misssing and needed
	m_anchor.x = m_anchor.y = 0;
	m_currentPos.x = m_currentPos.y = 0;
	m_originalAnchor.x = m_originalAnchor.y = 0;
	//

	DEBUG_ASSERTCRASH(!TheLookAtTranslator, ("Already have a LookAtTranslator - why do you need two?"));
	TheLookAtTranslator = this;
}

//-----------------------------------------------------------------------------
LookAtTranslator::~LookAtTranslator()
{
	if (TheLookAtTranslator == this)
		TheLookAtTranslator = NULL;
}

const ICoord2D* LookAtTranslator::getScrollAnchor(void)
{
	if (m_isScrolling && m_scrollType == SCROLL_MMB)
	{
		return &m_anchor;
	}
	return NULL;
}

Bool LookAtTranslator::hasMouseMovedRecently( void )
{
	if (m_lastMouseMoveFrame > TheGameLogic->getFrame())
		m_lastMouseMoveFrame = 0; // reset for new game

	if (m_lastMouseMoveFrame + LOGICFRAMES_PER_SECOND < TheGameLogic->getFrame())
		return false;

	return true;
}

void LookAtTranslator::setCurrentPos( const ICoord2D& pos )
{
	m_currentPos = pos;
}

//-----------------------------------------------------------------------------
/**
 * Keep the terrain point the wheel was spun over sitting under that same pixel while the camera
 * eases towards the zoom it was asked for. Re-measuring rather than projecting means the pin is
 * right at any pitch or field of view, and it converges: each frame's leftover error is the next
 * frame's correction. The pin ends when the zoom stops moving, when something else takes the
 * camera, or when its tick budget runs out.
 */
void LookAtTranslator::updateZoomToCursor( void )
{
	if (!m_zoomAnchorValid)
		return;

	// anything that moves the camera on purpose wins over the pin
	if (m_isScrolling || !TheInGameUI->getInputEnabled())
	{
		m_zoomAnchorValid = FALSE;
		return;
	}

	const Real zoom = TheTacticalView->getZoom();
	const Bool stillEasing = fabs( zoom - m_zoomAnchorZoom ) > 0.00001f;
	m_zoomAnchorZoom = zoom;

	Coord3D world;
	world.zero();
	TheTacticalView->screenToTerrain( &m_zoomAnchorPixel, &world );

	//
	// Move the camera by the world-space residual itself.  scrollBy() is not the way to do it:
	// its delta is a *screen* delta that it pushes through Device_To_World_Space with a fixed
	// 250 unit resolution and then scales by frame time, so a world offset handed to it comes
	// out as some other distance entirely and the anchor never lands.  lookAt is the exact move:
	// given a z on the ground it is a plain setPosition plus setCameraTransform, so the camera is
	// still clamped to the map the same way scrolling is.
	//
	const Real shiftX = m_zoomAnchorWorld.x - world.x;
	const Real shiftY = m_zoomAnchorWorld.y - world.y;
	if (shiftX != 0.0f || shiftY != 0.0f)
	{
		Coord3D pos;
		TheTacticalView->getPosition( &pos );
		pos.x += shiftX;
		pos.y += shiftY;
		pos.z = 0.0f;
		TheTacticalView->lookAt( &pos );
	}

	//
	// The first few ticks are a grace period: the wheel is handled in the message stream, which
	// runs before the view updates, so a zoom that has not started moving yet is not a zoom that
	// has finished.
	//
	const Bool grace = (m_zoomAnchorTicks > ZOOM_ANCHOR_MAX_TICKS - ZOOM_ANCHOR_GRACE_TICKS);
	if (--m_zoomAnchorTicks <= 0 || (!stillEasing && !grace))
		m_zoomAnchorValid = FALSE;

}  // end updateZoomToCursor

//-----------------------------------------------------------------------------
/**
 * The LookAt Translator is responsible for camera movements. It is directly responsible for
 * right mouse button scrolling, and CTRL-<F key> bookmarking. It also responds to certain
 * LOOKAT message on the message stream.
 */
GameMessageDisposition LookAtTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;

	GameMessage::Type t = msg->getType();
	switch (t)
	{
		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_KEY_DOWN:
		case GameMessage::MSG_RAW_KEY_UP:
		{
			// get key and state from args
			UnsignedByte key		= msg->getArgument( 0 )->integer;
			UnsignedByte state	= msg->getArgument( 1 )->integer;
			Bool isPressed = !(BitTest( state, KEY_STATE_UP ));
			
			if (TheShell && TheShell->isShellActive())
				break;

			switch (key)
			{
			case KEY_UP:
				scrollDir[DIR_UP] = isPressed;
				break;
			case KEY_DOWN:
				scrollDir[DIR_DOWN] = isPressed;
				break;
			case KEY_LEFT:
				scrollDir[DIR_LEFT] = isPressed;
				break;
			case KEY_RIGHT:
				scrollDir[DIR_RIGHT] = isPressed;
				break;
			}

			if (TheInGameUI->isSelecting() || (m_isScrolling && m_scrollType != SCROLL_KEY))
				break;

			// see if we need to start/stop scrolling
			Int numDirs = 0;
			for (Int i=0; i<4; ++i)
			{
				if (scrollDir[i])
					numDirs++;
			}

			if (numDirs && !m_isScrolling)
			{
				setScrolling( SCROLL_KEY );
			}
			else if (!numDirs && m_isScrolling)
			{
				stopScrolling();
			}
			break;
		}

		//-----------------------------------------------------------------------------
		// The right button belongs to the order layer now - a click commands, a drag draws a
		// formation line - so nothing here touches the camera.  Both cases stay only to keep the
		// idle timer honest: a player who is right-clicking is not away from the keyboard.
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			m_anchor = msg->getArgument( 0 )->pixel;
			m_originalAnchor = msg->getArgument( 0 )->pixel;
			m_currentPos = msg->getArgument( 0 )->pixel;
			m_timestamp = TheGameClient->getFrame();

			// The middle button is the camera: drag it and the map follows the cursor, hold ctrl and
			// the drag turns the camera instead.  It used to be the other way round, with a
			// MiddleMousePans switch in Options.ini deciding; there is no switch now because the
			// right button no longer scrolls anything and the pan has to live somewhere.  The
			// click-to-reset below works either way.
			if( !TheKeyboard->isCtrl() )
			{
				m_isRotating = false;
				if (!TheInGameUI->isSelecting() && !m_isScrolling)
					setScrolling(SCROLL_MMB);
			}
			else
			{
				m_isRotating = true;
				// the drag turns this, and under SnapCameraRotateTo45 the camera stands on whichever
				// eighth it is nearest - so start it where the camera already is.
				m_freeRotateAngle = TheTacticalView->getAngle();
			}
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_MIDDLE_BUTTON_UP:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			const UnsignedInt CLICK_DURATION = 5;
			const UnsignedInt PIXEL_OFFSET = 5;

			m_isRotating = false;
			if (m_scrollType == SCROLL_MMB)
				stopScrolling();
			Int dx = m_currentPos.x-m_originalAnchor.x;
			if (dx<0) dx = -dx;
			Int dy = m_currentPos.y-m_originalAnchor.y;
			Bool didMove = dx>PIXEL_OFFSET || dy>PIXEL_OFFSET;
			// if middle button is "clicked", reset to "home" orientation
			if (!didMove && TheGameClient->getFrame() - m_timestamp < CLICK_DURATION)
			{
				TheTacticalView->setAngleAndPitchToDefault();
				TheTacticalView->setZoomToDefault();
			}
			// nothing to settle on release: under SnapCameraRotateTo45 the heading was already on an
			// eighth for the whole drag.

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_POSITION:
		{
			if (m_currentPos.x != msg->getArgument( 0 )->pixel.x || m_currentPos.y != msg->getArgument( 0 )->pixel.y)
				m_lastMouseMoveFrame = TheGameLogic->getFrame();

			m_currentPos = msg->getArgument( 0 )->pixel;
			
			UnsignedInt height = TheDisplay->getHeight();
			UnsignedInt width  = TheDisplay->getWidth();

			if (TheInGameUI->getInputEnabled() == FALSE) {
				// We don't care how we're scrolling, just stop.
				if (m_isScrolling)
					stopScrolling();
				break;
			}

			// retail disables edge scrolling entirely in a window; EdgeScrollInWindowedMode in
			// Options.ini turns it back on for people who play windowed or borderless.
			if (!TheGlobalData->m_windowed || TheGlobalData->m_edgeScrollInWindowedMode)
			{
				if (m_isScrolling)
				{
					if ( m_scrollType == SCROLL_SCREENEDGE && (m_currentPos.x >= edgeScrollSize && m_currentPos.y >= edgeScrollSize && m_currentPos.y < height-edgeScrollSize && m_currentPos.x < width-edgeScrollSize) )
					{
						stopScrolling();
					}
				}
				else
				{
					if ( m_currentPos.x < edgeScrollSize || m_currentPos.y < edgeScrollSize || m_currentPos.y >= height-edgeScrollSize || m_currentPos.x >= width-edgeScrollSize )
					{
						setScrolling(SCROLL_SCREENEDGE);
					}
				}
			}

			// rotate the view
			if (m_isRotating)
			{
				const Real FACTOR = 0.01f;

				Real angle = FACTOR * (m_currentPos.x - m_anchor.x);

				if (TheGlobalData->m_snapCameraRotateTo45)
				{
					// discrete heading: the drag turns an angle we keep to ourselves and the camera
					// jumps to the eighth it is nearest, as the mouse crosses each halfway point.
					m_freeRotateAngle += angle;
					TheTacticalView->setAngle( View_snapAngleToEighth( m_freeRotateAngle ) );
				}
				else
				{
					TheTacticalView->setAngle( TheTacticalView->getAngle() + angle );
				}
				m_anchor = msg->getArgument( 0 )->pixel;
			}

			// rotate the view up/down
			if (m_isPitching)
			{
				const Real FACTOR = 0.01f;

				Real angle = FACTOR * (m_currentPos.y - m_anchor.y);

				TheTacticalView->setPitch( TheTacticalView->getPitch() + angle );
				m_anchor = msg->getArgument( 0 )->pixel;
			}

#if defined(_DEBUG) || defined(_INTERNAL)
			// adjust the field of view
			if (m_isChangingFOV)
			{
				const Real FACTOR = 0.01f;

				Real angle = FACTOR * (m_currentPos.y - m_anchor.y);

				TheTacticalView->setFieldOfView( TheTacticalView->getFieldOfView() + angle );
				m_anchor = msg->getArgument( 0 )->pixel;
			}
#endif
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_WHEEL:
		{
			m_lastMouseMoveFrame = TheGameLogic->getFrame();

			Int spin = msg->getArgument( 1 )->integer;

			//
			// Ctrl+wheel turns the structure on the cursor by 45 degrees a notch instead of zooming.
			// The wheel is otherwise wasted while placing, and the drag-to-aim interface it replaces
			// cannot hit an exact eighth of a turn.  Eaten so the same notch does not also zoom.
			//
			if (TheKeyboard->isCtrl() && TheInGameUI->rotatePendingPlacement( spin ))
				return DESTROY_MESSAGE;

			//
			// ZoomToCursor: remember the world point under the cursor and keep it there while the
			// camera moves. Measuring it again right after the zoom call - which is what this used
			// to do - always measured a camera that had not moved yet: zoomIn()/zoomOut() only
			// change the *desired* height above ground, and W3DView::update eases the actual zoom
			// towards it over the frames that follow. The shift therefore came out zero every time
			// and the feature did nothing at all. The anchor is now followed frame by frame in
			// updateZoomToCursor() for as long as that easing lasts, which also keeps it correct
			// whatever the pitch and the field of view are.
			//
			if (TheGlobalData->m_zoomToCursor && TheInGameUI->getInputEnabled())
			{
				m_zoomAnchorPixel = msg->getArgument( 0 )->pixel;
				m_zoomAnchorWorld.zero();
				TheTacticalView->screenToTerrain( &m_zoomAnchorPixel, &m_zoomAnchorWorld );
				m_zoomAnchorZoom = TheTacticalView->getZoom();
				m_zoomAnchorTicks = ZOOM_ANCHOR_MAX_TICKS;
				m_zoomAnchorValid = TRUE;
			}

			if (spin > 0)
			{
				for ( ; spin > 0; spin--)
					TheTacticalView->zoomIn();
			}
			else
			{
				for ( ;spin < 0; spin++ )
					TheTacticalView->zoomOut();
			}

			break;	// without this the case fell into MSG_META_OPTIONS below and every wheel
					// notch called stopScrolling(), killing zoom-while-panning and leaving
					// m_isScrolling/m_scrollType torn.
		}


		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_OPTIONS:
		{
			// stop the scrolling
			stopScrolling();
			// let the message drop through, cause we need to process this message for 
			// selection as well.
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_FRAME_TICK:
		{
			Coord2D offset = {0, 0};

			// hold whatever the wheel was spun over under the cursor while the zoom eases
			updateZoomToCursor();

			// If we've been forced to stop scrolling (script action?) then stop
			if (m_isScrolling && !TheInGameUI->isScrolling())
			{
				TheInGameUI->setScrollAmount(offset);
				stopScrolling();
			}
			else
			// scroll the view
			if (m_isScrolling)
			{
				switch (m_scrollType)
				{
				case SCROLL_MMB:
					{
						// The anchor stays where the button went down and the camera runs away from it,
						// faster the further the cursor gets.  This is what the right button used to do,
						// and it is what the hand expects; a one-to-one drag of the world is not the same
						// gesture and reads as sluggish at these scroll factors.
						if (TheInGameUI->shouldMoveScrollAnchor())
						{
							Int maxX = TheDisplay->getWidth()/2;
							Int maxY = TheDisplay->getHeight()/2;

							if (m_currentPos.x + maxX < m_anchor.x)
								m_anchor.x = m_currentPos.x + maxX;
							else if (m_currentPos.x - maxX > m_anchor.x)
								m_anchor.x = m_currentPos.x - maxX;

							if (m_currentPos.y + maxY < m_anchor.y)
								m_anchor.y = m_currentPos.y + maxY;
							else if (m_currentPos.y - maxY > m_anchor.y)
								m_anchor.y = m_currentPos.y - maxY;
						}

						offset.x = TheGlobalData->m_horizontalScrollSpeedFactor * (m_currentPos.x - m_anchor.x);
						offset.y = TheGlobalData->m_verticalScrollSpeedFactor * (m_currentPos.y - m_anchor.y);
						Coord2D vec;
						vec.x = offset.x;
						vec.y = offset.y;
						vec.normalize();
						// Add in the window scroll amount as the minimum.
						offset.x += TheGlobalData->m_horizontalScrollSpeedFactor * vec.x * sqr(TheGlobalData->m_keyboardScrollFactor);
						offset.y += TheGlobalData->m_verticalScrollSpeedFactor * vec.y * sqr(TheGlobalData->m_keyboardScrollFactor);
					}
					break;
				case SCROLL_KEY:
					{
						if (scrollDir[DIR_UP])
						{
							offset.y -= TheGlobalData->m_verticalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (scrollDir[DIR_DOWN])
						{
							offset.y += TheGlobalData->m_verticalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (scrollDir[DIR_LEFT])
						{
							offset.x -= TheGlobalData->m_horizontalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (scrollDir[DIR_RIGHT])
						{
							offset.x += TheGlobalData->m_horizontalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
					}
					break;
				case SCROLL_SCREENEDGE:
					{
						UnsignedInt height = TheDisplay->getHeight();
						UnsignedInt width  = TheDisplay->getWidth();
						if (m_currentPos.y < edgeScrollSize)
						{
							offset.y -= TheGlobalData->m_verticalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (m_currentPos.y >= height-edgeScrollSize)
						{
							offset.y += TheGlobalData->m_verticalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (m_currentPos.x < edgeScrollSize)
						{
							offset.x -= TheGlobalData->m_horizontalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
						if (m_currentPos.x >= width-edgeScrollSize)
						{
							offset.x += TheGlobalData->m_horizontalScrollSpeedFactor * SCROLL_AMT * TheGlobalData->m_keyboardScrollFactor;
						}
					}
					break;
				}

				TheInGameUI->setScrollAmount(offset);
				TheTacticalView->scrollBy( &offset );
			}
			else	//not scrolling so reset amount
				TheInGameUI->setScrollAmount(offset);

			//if (TheGlobalData->m_saveCameraInReplay /*&& TheRecorder->getMode() != RECORDERMODETYPE_PLAYBACK *//**/&& (TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame())/**/)
			//if (TheGlobalData->m_saveCameraInReplay && (TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame()))
			if (TheGlobalData->m_saveCameraInReplay && (TheGameLogic->isInSinglePlayerGame() || TheGameLogic->isInSkirmishGame()))
			{
				ViewLocation currentView;
				TheTacticalView->getLocation(&currentView);
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_SET_REPLAY_CAMERA );
				msg->appendLocationArgument( currentView.m_pos );
				msg->appendRealArgument( currentView.m_angle );
				msg->appendRealArgument( currentView.m_pitch );
				msg->appendRealArgument( currentView.m_zoom );
				msg->appendIntegerArgument( (Int)TheMouse->getMouseCursor() );
				msg->appendPixelArgument( m_currentPos );
			}
			break;
		}

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_BEGIN_ADJUST_PITCH:
		{
			DEBUG_ASSERTCRASH(!m_isPitching, ("hmm, mismatched m_isPitching"));
			m_isPitching = true;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_END_ADJUST_PITCH:
		{
			DEBUG_ASSERTCRASH(m_isPitching, ("hmm, mismatched m_isPitching"));
			m_isPitching = false;
			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_DESHROUD:
		{
			ThePartitionManager->revealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		case GameMessage::MSG_CHEAT_DESHROUD: 
		{
			if (!TheGameLogic->isInMultiplayerGame())
			{
				ThePartitionManager->revealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			}
			break;
		}
#endif // #if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_ENSHROUD:
		{
			// Need to first undo the permanent Look laid down by DEMO_DESHROUD, then blast a shroud dollop.
			ThePartitionManager->undoRevealMapForPlayerPermanently( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			ThePartitionManager->shroudMapForPlayer( ThePlayerList->getLocalPlayer()->getPlayerIndex() );
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_BEGIN_ADJUST_FOV:
		{
			//DEBUG_ASSERTCRASH(!m_isChangingFOV, ("hmm, mismatched m_isChangingFOV"));
			m_isChangingFOV = true;
			m_anchor = m_currentPos;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		// ------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_END_ADJUST_FOV:
		{
		//	DEBUG_ASSERTCRASH(m_isChangingFOV, ("hmm, mismatched m_isChangingFOV"));
			m_isChangingFOV = false;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SAVE_VIEW1:
		case GameMessage::MSG_META_SAVE_VIEW2:
		case GameMessage::MSG_META_SAVE_VIEW3:
		case GameMessage::MSG_META_SAVE_VIEW4:
		case GameMessage::MSG_META_SAVE_VIEW5:
		case GameMessage::MSG_META_SAVE_VIEW6:
		case GameMessage::MSG_META_SAVE_VIEW7:
		case GameMessage::MSG_META_SAVE_VIEW8:
		{
			Int slot = t - GameMessage::MSG_META_SAVE_VIEW1 + 1;
			if ( slot > 0 && slot <= MAX_VIEW_LOCS )
			{
				TheTacticalView->getLocation( &m_viewLocation[slot-1] );
				UnicodeString msg;
				msg.format( TheGameText->fetch( "GUI:BookmarkXSet" ), slot );
				TheInGameUI->message( msg );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_VIEW_VIEW1:
		case GameMessage::MSG_META_VIEW_VIEW2:
		case GameMessage::MSG_META_VIEW_VIEW3:
		case GameMessage::MSG_META_VIEW_VIEW4:
		case GameMessage::MSG_META_VIEW_VIEW5:
		case GameMessage::MSG_META_VIEW_VIEW6:
		case GameMessage::MSG_META_VIEW_VIEW7:
		case GameMessage::MSG_META_VIEW_VIEW8:
		{
			Int slot = t - GameMessage::MSG_META_VIEW_VIEW1 + 1;
			if ( slot > 0 && slot <= MAX_VIEW_LOCS )
			{
				TheTacticalView->setLocation( &m_viewLocation[slot-1] );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
		case GameMessage::MSG_META_DEMO_LOCK_CAMERA_TO_PLANES:
		{
			Drawable *first = NULL;

			if (m_lastPlaneID)
				first = TheGameClient->findDrawableByID( m_lastPlaneID );

			if (first == NULL)
				first = TheGameClient->firstDrawable();

			if (first)
			{
				Drawable *d = first;
				Bool done = false;

				while(!done)
				{
					// get next Drawable, wrapping around to head of list if necessary
					d = d->getNextDrawable();
					if (d == NULL)
						d = TheGameClient->firstDrawable();

					// if we've found an airborne object, lock onto it
// "isAboveTerrain" only indicates that we are currently in the air, but that
// could be the case if we are a buggy jumping a hill, or a unit being paradropped.
// the right thing would be to look at the locomotors.
// so this isn't really right, but will suffice for demo purposes.
					if (d->getObject() && d->getObject()->isAboveTerrain() )
					{
						Bool doLock = true;

						// but don't lock onto projectiles
						ProjectileUpdateInterface* pui = NULL;
						for (BehaviorModule** u = d->getObject()->getBehaviorModules(); *u; ++u)
						{
							if ((pui = (*u)->getProjectileUpdateInterface()) != NULL)
							{
								doLock = false;
								break;
							}
						}

						if (doLock)
						{
							TheTacticalView->setCameraLock( d->getObject()->getID() );
							m_lastPlaneID = d->getID();
							done = true;
							break;
						}
					} // if airborne found

					// if we're back to the first, quit
					if (d == first)
						break;
				} // while
			}	// end plane lock

			disp = DESTROY_MESSAGE;
			break;
		}
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

	}  // end switch

	return disp;

}  // end LookAtTranslator

void LookAtTranslator::resetModes()
{
	m_isScrolling = FALSE;
	m_isRotating = FALSE;
	m_freeRotateAngle = 0.0f;
	m_isPitching = FALSE;
	m_isChangingFOV = FALSE;
}