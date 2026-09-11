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

// FILE: ControlInput.cpp ////////////////////////////////////////////////////////////////////////
//
// -control's hands: the mouse, the keyboard and the camera.  Nothing here reaches the game in a way
// a person could not.  A mouse command is posted to the game window as the Win32 messages a real
// mouse sends, so it goes through Win32Mouse, the window manager, the click and drag tests and the
// command translator the way a hand on the mouse does.  A key goes into Keyboard's own queue beside
// the device's, so held modifiers are really held and the mouse messages pick them up.  Neither
// needs the window in front: the mouse is window messages with no focus check, and injected keys
// never touch DirectInput, which reads only the foreground window.
//
// A command becomes a list of steps released one per engine pass.  A press and a release that land
// on the same pass are not a click to the translators, and a shift has to be down before the click
// that reads it.  The reply goes out after the last step has had a pass of its own.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ControlCommands.h"
#include "GameClient/GameWindow.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Keyboard.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"

#include <deque>

extern HWND ApplicationHWnd;

static const Int DEFAULT_DRAG_STEPS = 8;
static const Int MAX_DRAG_STEPS = 120;
static const Int CAMERA_SETTLE_PASSES = 2;			///< lookAt only marks the camera for recalculation; it moves on the next update
static const Int WHEEL_NOTCH_DELTA = 120;
static const UINT MESSAGE_MOUSE_WHEEL = 0x020A;	///< WM_MOUSEWHEEL, which this header set does not define

// ------------------------------------------------------------------------------------------------
// steps
// ------------------------------------------------------------------------------------------------

enum InputStepKind
{
	STEP_IDLE = 0,		///< a pass for the previous step to be processed in
	STEP_MOUSE,
	STEP_KEY
};

/** Where a mouse message lands: a client pixel, or a world point turned into one when it is sent. */
struct MouseTarget
{
	Bool isWorld;
	ICoord2D pixel;
	Coord3D world;
};

struct InputStep
{
	InputStepKind kind;
	UINT message;
	WPARAM buttons;
	MouseTarget target;
	UnsignedByte key;
	UnsignedShort keyState;
};

struct MouseButton
{
	const char *name;
	UINT downMessage;
	UINT upMessage;
	UINT doubleClickMessage;
	WPARAM heldFlag;
};

static const MouseButton MOUSE_BUTTONS[] =
{
	{ "left",		WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK, MK_LBUTTON },
	{ "right",	WM_RBUTTONDOWN, WM_RBUTTONUP, WM_RBUTTONDBLCLK, MK_RBUTTON },
	{ "middle", WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MBUTTONDBLCLK, MK_MBUTTON },
};
static const Int MOUSE_BUTTON_COUNT = sizeof( MOUSE_BUTTONS ) / sizeof( MOUSE_BUTTONS[ 0 ] );

static std::deque<InputStep> theSteps;
static Bool theStepsArePending = FALSE;
static std::string theStepError;
static Int thePassesToWait = 0;
static Int theFramesToWait = 0;

static void pushIdle( void )
{
	InputStep step = InputStep();
	step.kind = STEP_IDLE;
	theSteps.push_back( step );
}

static void pushKey( UnsignedByte key, UnsignedShort state )
{
	InputStep step = InputStep();
	step.kind = STEP_KEY;
	step.key = key;
	step.keyState = state;
	theSteps.push_back( step );
}

static void pushMouse( UINT message, WPARAM buttons, const MouseTarget &target )
{
	InputStep step = InputStep();
	step.kind = STEP_MOUSE;
	step.message = message;
	step.buttons = buttons;
	step.target = target;
	theSteps.push_back( step );
}

static void pushModifiers( const std::vector<UnsignedByte> &modifiers, UnsignedShort state )
{
	if (state == KEY_STATE_DOWN)
	{
		for( size_t i = 0; i < modifiers.size(); ++i )
			pushKey( modifiers[ i ], KEY_STATE_DOWN );
	}
	else
	{
		for( size_t i = modifiers.size(); i > 0; --i )
			pushKey( modifiers[ i - 1 ], KEY_STATE_UP );
	}
}

static void pushClick( const MouseButton &button, const MouseTarget &target,
											 const std::vector<UnsignedByte> &modifiers, Bool isDoubleClick )
{
	pushModifiers( modifiers, KEY_STATE_DOWN );
	pushMouse( WM_MOUSEMOVE, 0, target );
	pushMouse( button.downMessage, button.heldFlag, target );
	pushMouse( button.upMessage, 0, target );
	if (isDoubleClick)
	{
		pushMouse( button.doubleClickMessage, button.heldFlag, target );
		pushMouse( button.upMessage, 0, target );
	}
	pushModifiers( modifiers, KEY_STATE_UP );
	pushIdle();
}

static MouseTarget getTargetBetween( const MouseTarget &from, const MouseTarget &to, Real fraction )
{
	MouseTarget between = from;
	between.pixel.x = from.pixel.x + (Int)((Real)(to.pixel.x - from.pixel.x) * fraction);
	between.pixel.y = from.pixel.y + (Int)((Real)(to.pixel.y - from.pixel.y) * fraction);
	if (from.isWorld)
	{
		between.world.x = from.world.x + (to.world.x - from.world.x) * fraction;
		between.world.y = from.world.y + (to.world.y - from.world.y) * fraction;
		between.world.z = TheTerrainLogic->getGroundHeight( between.world.x, between.world.y );
	}
	return between;
}

static void pushDrag( const MouseButton &button, const MouseTarget &from, const MouseTarget &to,
										  Int steps, const std::vector<UnsignedByte> &modifiers )
{
	pushModifiers( modifiers, KEY_STATE_DOWN );
	pushMouse( WM_MOUSEMOVE, 0, from );
	pushMouse( button.downMessage, button.heldFlag, from );
	for( Int i = 1; i <= steps; ++i )
		pushMouse( WM_MOUSEMOVE, button.heldFlag, getTargetBetween( from, to, (Real)i / (Real)steps ) );
	pushMouse( button.upMessage, 0, to );
	pushModifiers( modifiers, KEY_STATE_UP );
	pushIdle();
}

/** Bring a world point onto the screen before a command aims at it. */
static void aimCameraAt( const Coord3D &world )
{
	ICoord2D pixel;
	if (TheTacticalView->worldToScreenTriReturn( &world, &pixel ) == View::WTS_INSIDE_FRUSTUM)
		return;

	TheTacticalView->lookAt( &world );
	for( Int i = 0; i < CAMERA_SETTLE_PASSES; ++i )
		pushIdle();
}

static ControlOutcome runSteps( void )
{
	theStepError.clear();
	theStepsArePending = TRUE;
	return CONTROL_LATER;
}

/** A world point that is still off the screen once the camera is on it cannot be clicked.  The mouse
		steps go and the key releases stay, so no modifier is left held down behind the failure. */
static void dropMouseSteps( void )
{
	std::deque<InputStep> keysOnly;
	for( std::deque<InputStep>::const_iterator it = theSteps.begin(); it != theSteps.end(); ++it )
	{
		if (it->kind == STEP_KEY)
			keysOnly.push_back( *it );
	}
	theSteps.swap( keysOnly );
}

static void releaseStep( const InputStep &step )
{
	if (step.kind == STEP_KEY)
	{
		TheKeyboard->injectKey( step.key, step.keyState );
		return;
	}
	if (step.kind != STEP_MOUSE)
		return;

	ICoord2D pixel = step.target.pixel;
	if (step.target.isWorld
			&& TheTacticalView->worldToScreenTriReturn( &step.target.world, &pixel ) != View::WTS_INSIDE_FRUSTUM)
	{
		theStepError = "the world point is off the screen even with the camera moved onto it";
		dropMouseSteps();
		return;
	}

	if (step.message == MESSAGE_MOUSE_WHEEL)
	{
		// the wheel is the one mouse message that carries screen coordinates rather than client ones
		POINT screen;
		screen.x = pixel.x;
		screen.y = pixel.y;
		ClientToScreen( ApplicationHWnd, &screen );
		PostMessage( ApplicationHWnd, step.message, step.buttons, MAKELPARAM( screen.x, screen.y ) );
		return;
	}

	PostMessage( ApplicationHWnd, step.message, step.buttons, MAKELPARAM( pixel.x, pixel.y ) );
}

void ControlInput_tick( void )
{
	if (thePassesToWait > 0)
	{
		if (--thePassesToWait == 0)
			ControlServer_finish( CONTROL_DONE );
		return;
	}

	if (!theStepsArePending)
		return;

	if (theSteps.empty())
	{
		theStepsArePending = FALSE;
		if (theStepError.empty())
		{
			ControlServer_finish( CONTROL_DONE );
			return;
		}
		ControlServer_current().error = theStepError;
		ControlServer_finish( CONTROL_FAILED );
		return;
	}

	const InputStep step = theSteps.front();
	theSteps.pop_front();
	releaseStep( step );
}

void ControlInput_logicFrame( void )
{
	if (theFramesToWait > 0 && --theFramesToWait == 0)
		ControlServer_finish( CONTROL_DONE );
}

// ------------------------------------------------------------------------------------------------
// reading the command's words
// ------------------------------------------------------------------------------------------------

static const MouseButton *findButton( const char *name )
{
	for( Int i = 0; i < MOUSE_BUTTON_COUNT; ++i )
	{
		if (strcmp( MOUSE_BUTTONS[ i ].name, name ) == 0)
			return &MOUSE_BUTTONS[ i ];
	}
	return NULL;
}

static UnsignedByte findKey( const char *name )
{
	for( const LookupListRec *key = KeyNames; key->name; ++key )
	{
		if (strcmp( key->name, name ) == 0)
			return (UnsignedByte)key->value;
	}
	return KEY_NONE;
}

/** SHIFT, CTRL and ALT from the given word on, as the left-hand keys a player would hold. */
static Bool readModifiers( const ControlCommand &command, size_t first, std::vector<UnsignedByte> *keys )
{
	for( size_t i = first; i < command.words.size(); ++i )
	{
		const std::string &word = command.words[ i ];
		if (word == "SHIFT")
			keys->push_back( KEY_LSHIFT );
		else if (word == "CTRL")
			keys->push_back( KEY_LCTRL );
		else if (word == "ALT")
			keys->push_back( KEY_LALT );
		else
			return FALSE;
	}
	return TRUE;
}

static Bool readPixel( const ControlCommand &command, size_t index, MouseTarget *target )
{
	*target = MouseTarget();
	return ControlCommand_getInt( command, index, &target->pixel.x )
		&& ControlCommand_getInt( command, index + 1, &target->pixel.y );
}

static Bool readWorldPoint( const ControlCommand &command, size_t index, MouseTarget *target )
{
	*target = MouseTarget();
	target->isWorld = TRUE;
	if (!ControlCommand_getReal( command, index, &target->world.x )
			|| !ControlCommand_getReal( command, index + 1, &target->world.y ))
		return FALSE;
	target->world.z = TheTerrainLogic->getGroundHeight( target->world.x, target->world.y );
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// commands
// ------------------------------------------------------------------------------------------------

/* mouse move <x> <y>
	 mouse wheel <x> <y> <notches>
	 mouse click|dblclick <button> <x> <y> [SHIFT] [CTRL] [ALT]
	 mouse down|up <button> <x> <y>
	 mouse drag <button> <x1> <y1> <x2> <y2> [steps] [SHIFT] [CTRL] [ALT] */
static ControlOutcome handleMouse( ControlCommand &command )
{
	const std::string action = ControlCommand_word( command, 1 );
	MouseTarget target;
	std::vector<UnsignedByte> modifiers;

	if (action == "move")
	{
		if (!readPixel( command, 2, &target ))
			return ControlCommand_fail( command, "mouse move wants <x> <y>" );
		pushMouse( WM_MOUSEMOVE, 0, target );
		pushIdle();
		return runSteps();
	}

	if (action == "wheel")
	{
		Int notches = 0;
		if (!readPixel( command, 2, &target ) || !ControlCommand_getInt( command, 4, &notches ))
			return ControlCommand_fail( command, "mouse wheel wants <x> <y> <notches>, positive to zoom in" );
		pushMouse( WM_MOUSEMOVE, 0, target );
		pushMouse( MESSAGE_MOUSE_WHEEL, MAKEWPARAM( 0, (WORD)(notches * WHEEL_NOTCH_DELTA) ), target );
		pushIdle();
		return runSteps();
	}

	const MouseButton *button = findButton( ControlCommand_word( command, 2 ) );
	if (button == NULL)
		return ControlCommand_fail( command, "mouse wants move, wheel, or click|dblclick|down|up|drag with left, right or middle" );

	if (action == "click" || action == "dblclick")
	{
		if (!readPixel( command, 3, &target ) || !readModifiers( command, 5, &modifiers ))
			return ControlCommand_fail( command, "mouse click wants <button> <x> <y> [SHIFT] [CTRL] [ALT]" );
		pushClick( *button, target, modifiers, action == "dblclick" );
		return runSteps();
	}

	if (action == "down" || action == "up")
	{
		if (!readPixel( command, 3, &target ))
			return ControlCommand_fail( command, "mouse down and up want <button> <x> <y>" );
		pushMouse( WM_MOUSEMOVE, action == "up" ? button->heldFlag : 0, target );
		pushMouse( action == "down" ? button->downMessage : button->upMessage,
							 action == "down" ? button->heldFlag : 0, target );
		pushIdle();
		return runSteps();
	}

	if (action == "drag")
	{
		MouseTarget to;
		if (!readPixel( command, 3, &target ) || !readPixel( command, 5, &to ))
			return ControlCommand_fail( command, "mouse drag wants <button> <x1> <y1> <x2> <y2> [steps] [SHIFT] [CTRL] [ALT]" );

		Int steps = DEFAULT_DRAG_STEPS;
		size_t firstModifier = 7;
		if (ControlCommand_getInt( command, 7, &steps ))
			firstModifier = 8;
		if (steps < 1 || steps > MAX_DRAG_STEPS || !readModifiers( command, firstModifier, &modifiers ))
			return ControlCommand_fail( command, "a drag takes 1 to 120 steps and the modifiers SHIFT, CTRL, ALT" );

		pushDrag( *button, target, to, steps, modifiers );
		return runSteps();
	}

	return ControlCommand_fail( command, "mouse wants move, wheel, click, dblclick, down, up or drag" );
}

/* worldclick <button> <x> <y> [SHIFT] [CTRL] [ALT]
	 worlddrag <button> <x1> <y1> <x2> <y2> [SHIFT] [CTRL] [ALT]
	 A world point is turned into a pixel when each message is sent, after the camera has been moved
	 onto the point if it was off the screen. */
static ControlOutcome handleWorldMouse( ControlCommand &command, Bool isDrag )
{
	const MouseButton *button = findButton( ControlCommand_word( command, 1 ) );
	MouseTarget from;
	MouseTarget to;
	std::vector<UnsignedByte> modifiers;

	if (!isDrag)
	{
		if (button == NULL || !readWorldPoint( command, 2, &from ) || !readModifiers( command, 4, &modifiers ))
			return ControlCommand_fail( command, "worldclick wants <left|right|middle> <x> <y> [SHIFT] [CTRL] [ALT]" );
		aimCameraAt( from.world );
		pushClick( *button, from, modifiers, FALSE );
		return runSteps();
	}

	if (button == NULL || !readWorldPoint( command, 2, &from ) || !readWorldPoint( command, 4, &to )
			|| !readModifiers( command, 6, &modifiers ))
		return ControlCommand_fail( command, "worlddrag wants <left|right|middle> <x1> <y1> <x2> <y2> [SHIFT] [CTRL] [ALT]" );

	aimCameraAt( getTargetBetween( from, to, 0.5f ).world );
	pushDrag( *button, from, to, DEFAULT_DRAG_STEPS, modifiers );
	return runSteps();
}

static Bool isHiddenOnScreen( GameWindow *window )
{
	for( GameWindow *ancestor = window; ancestor; ancestor = ancestor->winGetParent() )
	{
		if (ancestor->winIsHidden())
			return TRUE;
	}
	return FALSE;
}

/* window click <File.wnd:Name> [left|right]
	 A hidden window is refused: its rectangle is still there, and a click on it lands on whatever is
	 drawn in its place, which is how a menu button that has not slid in yet gets pressed by mistake. */
static ControlOutcome handleWindow( ControlCommand &command )
{
	if (strcmp( ControlCommand_word( command, 1 ), "click" ) != 0)
		return ControlCommand_fail( command, "window wants click <File.wnd:Name> [left|right]" );

	GameWindow *window = ControlQuery_findWindow( ControlCommand_word( command, 2 ) );
	if (window == NULL)
		return ControlCommand_fail( command, std::string( "no window named '" ) + ControlCommand_word( command, 2 ) + "'; the windows command lists them" );
	if (isHiddenOnScreen( window ))
		return ControlCommand_fail( command, std::string( "'" ) + ControlCommand_word( command, 2 ) + "' or a window holding it is hidden" );

	const MouseButton *button = &MOUSE_BUTTONS[ 0 ];
	if (command.words.size() > 3)
	{
		button = findButton( ControlCommand_word( command, 3 ) );
		if (button == NULL)
			return ControlCommand_fail( command, "the button is left, right or middle" );
	}

	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );

	MouseTarget target = MouseTarget();
	target.pixel.x = x + width / 2;
	target.pixel.y = y + height / 2;
	command.reply.addInt( "x", target.pixel.x );
	command.reply.addInt( "y", target.pixel.y );
	command.reply.addBool( "enabled", window->winGetEnabled() );

	pushClick( *button, target, std::vector<UnsignedByte>(), FALSE );
	return runSteps();
}

/* key [press] <KEY_name> [SHIFT] [CTRL] [ALT]
	 key down|up <KEY_name> */
static ControlOutcome handleKey( ControlCommand &command )
{
	std::string action = ControlCommand_word( command, 1 );
	size_t keyWord = 2;
	if (action != "press" && action != "down" && action != "up")
	{
		action = "press";
		keyWord = 1;
	}

	const UnsignedByte key = findKey( ControlCommand_word( command, keyWord ) );
	if (key == KEY_NONE)
		return ControlCommand_fail( command, "key wants a KEY_ name from CommandMap.ini, e.g. key press KEY_Z ALT" );

	std::vector<UnsignedByte> modifiers;
	if (!readModifiers( command, keyWord + 1, &modifiers ))
		return ControlCommand_fail( command, "a modifier is SHIFT, CTRL or ALT" );

	if (action == "down")
		pushKey( key, KEY_STATE_DOWN );
	else if (action == "up")
		pushKey( key, KEY_STATE_UP );
	else
	{
		pushModifiers( modifiers, KEY_STATE_DOWN );
		pushKey( key, KEY_STATE_DOWN );
		pushKey( key, KEY_STATE_UP );
		pushModifiers( modifiers, KEY_STATE_UP );
	}
	pushIdle();
	return runSteps();
}

static void addCamera( ControlJson &reply )
{
	Coord3D position;
	TheTacticalView->getPosition( &position );
	reply.addReal( "x", position.x );
	reply.addReal( "y", position.y );
	reply.addReal( "zoom", TheTacticalView->getZoom() );
	reply.addReal( "angle", TheTacticalView->getAngle() );
	reply.addReal( "pitch", TheTacticalView->getPitch() );
	reply.addInt( "width", TheTacticalView->getWidth() );
	reply.addInt( "height", TheTacticalView->getHeight() );
}

/* camera get | lookat <x> <y> | zoom <z> | angle <radians> | pitch <radians>
	 A camera that was changed replies once it has had the passes to move. */
static ControlOutcome handleCamera( ControlCommand &command )
{
	const std::string action = ControlCommand_word( command, 1 );
	if (action == "get")
	{
		addCamera( command.reply );
		return CONTROL_DONE;
	}

	Real first = 0.0f;
	if (!ControlCommand_getReal( command, 2, &first ))
		return ControlCommand_fail( command, "camera wants get, lookat <x> <y>, zoom <z>, angle <radians> or pitch <radians>" );

	if (action == "lookat")
	{
		Coord3D at;
		at.x = first;
		if (!ControlCommand_getReal( command, 3, &at.y ))
			return ControlCommand_fail( command, "camera lookat wants <x> <y>" );
		at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );
		TheTacticalView->lookAt( &at );
	}
	else if (action == "zoom")
		TheTacticalView->setZoom( first );
	else if (action == "angle")
		TheTacticalView->setAngle( first );
	else if (action == "pitch")
		TheTacticalView->setPitch( first );
	else
		return ControlCommand_fail( command, "camera wants get, lookat, zoom, angle or pitch" );

	for( Int i = 0; i < CAMERA_SETTLE_PASSES; ++i )
		pushIdle();
	return runSteps();
}

/* toscreen <x> <y> [z] and toworld <px> <py> */
static ControlOutcome handleProjection( ControlCommand &command, Bool isToScreen )
{
	if (isToScreen)
	{
		Coord3D world;
		if (!ControlCommand_getReal( command, 1, &world.x ) || !ControlCommand_getReal( command, 2, &world.y ))
			return ControlCommand_fail( command, "toscreen wants <x> <y> [z]" );
		if (!ControlCommand_getReal( command, 3, &world.z ))
			world.z = TheTerrainLogic->getGroundHeight( world.x, world.y );

		ICoord2D pixel;
		const Bool isOnScreen = TheTacticalView->worldToScreenTriReturn( &world, &pixel ) == View::WTS_INSIDE_FRUSTUM;
		command.reply.addInt( "x", pixel.x );
		command.reply.addInt( "y", pixel.y );
		command.reply.addBool( "onScreen", isOnScreen );
		return CONTROL_DONE;
	}

	ICoord2D pixel;
	if (!ControlCommand_getInt( command, 1, &pixel.x ) || !ControlCommand_getInt( command, 2, &pixel.y ))
		return ControlCommand_fail( command, "toworld wants <px> <py>" );

	Coord3D world;
	const Bool isOnTerrain = TheTacticalView->screenToTerrain( &pixel, &world );
	command.reply.addReal( "x", world.x );
	command.reply.addReal( "y", world.y );
	command.reply.addReal( "z", world.z );
	command.reply.addBool( "onTerrain", isOnTerrain );
	return CONTROL_DONE;
}

/* wait passes <n> | wait frames <n> */
static ControlOutcome handleWait( ControlCommand &command )
{
	const std::string unit = ControlCommand_word( command, 1 );
	Int count = 0;
	if (!ControlCommand_getInt( command, 2, &count ) || count < 1)
		return ControlCommand_fail( command, "wait wants passes <n> or frames <n>, n at least 1" );

	if (unit == "passes")
	{
		thePassesToWait = count;
		return CONTROL_LATER;
	}
	if (unit == "frames")
	{
		if (!ControlServer_isMatchRunning() || TheGameLogic->isGamePaused())
			return ControlCommand_fail( command, "logic frames only pass in a running, unpaused match" );
		theFramesToWait = count;
		return CONTROL_LATER;
	}
	return ControlCommand_fail( command, "wait wants passes <n> or frames <n>" );
}

ControlOutcome ControlInput_handle( ControlCommand &command )
{
	const std::string &verb = command.words[ 0 ];

	if (verb == "mouse")
		return handleMouse( command );
	if (verb == "worldclick")
		return handleWorldMouse( command, FALSE );
	if (verb == "worlddrag")
		return handleWorldMouse( command, TRUE );
	if (verb == "window")
		return handleWindow( command );
	if (verb == "key")
		return handleKey( command );
	if (verb == "camera")
		return handleCamera( command );
	if (verb == "toscreen")
		return handleProjection( command, TRUE );
	if (verb == "toworld")
		return handleProjection( command, FALSE );
	if (verb == "wait")
		return handleWait( command );

	return CONTROL_NOT_MINE;
}
