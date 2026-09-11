"""MCP server that puts Claude in the player's chair of Command & Conquer Generals Zero Hour.

It talks to the game's -control WebSocket (GeneralsMD/Code/GameEngine/Source/Common/System/
ControlServer.cpp). game_launch starts a windowed game with the socket open and connects; every other
tool is one command down that socket. Orders go in as the local player's own messages, so they are
recorded in the replay and reach logic exactly as a mouse would send them.

The game answers one command at a time, in order, and a command that takes time (a click is several
engine passes, a picture waits for a draw) holds its reply until it has finished.
"""

import asyncio
import atexit
import io
import json
import pathlib
import shlex
import socket
import subprocess
import time

import websockets
from mcp.server.fastmcp import FastMCP, Image
from PIL import Image as PillowImage

RUN_DIRECTORY = pathlib.Path(__file__).resolve().parents[3] / "Run"
GAME_EXECUTABLE = RUN_DIRECTORY / "generals.exe"

DEFAULT_PORT = 8787
LAUNCH_TIMEOUT_SECONDS = 240
READY_POLL_PASSES = 30
MENU_TRANSITION_TIMEOUT_SECONDS = 30
CONNECT_RETRY_SECONDS = 1.0
REPLY_TIMEOUT_SECONDS = 600
QUIT_GRACE_SECONDS = 15
SCREENSHOT_MAX_WIDTH = 1280

mcp = FastMCP("generals")


class GameSession:
    """The one game process and the one socket to it."""

    def __init__(self) -> None:
        self.process: subprocess.Popen | None = None
        self.socket = None
        self.lock = asyncio.Lock()

    def is_running(self) -> bool:
        return self.process is not None and self.process.poll() is None

    async def connect(self, port: int, deadline: float) -> None:
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None:
                raise RuntimeError(f"generals.exe exited with code {self.process.returncode} before the socket opened")
            try:
                self.socket = await websockets.connect(f"ws://127.0.0.1:{port}", max_size=None, open_timeout=5)
                return
            except (OSError, websockets.exceptions.WebSocketException) as error:
                last_error = error
                await asyncio.sleep(CONNECT_RETRY_SECONDS)
        raise RuntimeError(f"no control socket on port {port}: {last_error}")

    async def send(self, line: str) -> dict:
        async with self.lock:
            if self.socket is None:
                raise RuntimeError("not connected to a game; call game_launch or game_connect first")
            await self.socket.send(line)
            reply = await asyncio.wait_for(self.socket.recv(), REPLY_TIMEOUT_SECONDS)
        return json.loads(reply)

    async def close(self) -> None:
        if self.socket is not None:
            await self.socket.close()
            self.socket = None

    def kill_process(self) -> None:
        if self.is_running():
            self.process.kill()
            self.process.wait()
        self.process = None


session = GameSession()
atexit.register(session.kill_process)


def join_words(*words) -> str:
    return " ".join(str(word) for word in words if word is not None and word != "")


# ------------------------------------------------------------------------------------------------
# the process
# ------------------------------------------------------------------------------------------------

@mcp.tool()
async def game_launch(width: int = 1024, height: int = 768, port: int = 0, extra_args: str = "") -> dict:
    """Start generals.exe windowed from GeneralsMD/Run with the control socket open, and connect.

    Returns once the main menu has animated in (-noshellmap -quickstart), or once a match is running
    if extra_args started one. extra_args is passed on as typed, for example "-seed 5" or
    "-particlecap 2500". Port 0 takes a free one: other sessions on this machine run their own copies
    with -multiInstance, and a fixed port would connect to theirs. Takes 20 to 60 seconds. Call
    game_kill when done: a game left running holds the display mode, the LAN port and the log.
    """
    if session.is_running():
        return {"ok": False, "error": "a game is already running; game_kill it first"}

    port = port or find_free_port()
    arguments = [str(GAME_EXECUTABLE), "-win", "-xres", str(width), "-yres", str(height),
                 "-noshellmap", "-quickstart", "-multiInstance",
                 "-control", str(port), "-logPrefix", f"control{port}"]
    arguments += shlex.split(extra_args)
    session.process = subprocess.Popen(arguments, cwd=RUN_DIRECTORY)
    deadline = time.monotonic() + LAUNCH_TIMEOUT_SECONDS
    await session.connect(port, deadline)
    state = await wait_until_ready(deadline)
    state["port"] = port
    return state


def find_free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


async def wait_until_ready(deadline: float) -> dict:
    """The socket opens while the EA logo is still playing; the menu is pushed after it and animates in."""
    state = await session.send("state")
    while not (state["inMatch"] or state["shellReady"]):
        if time.monotonic() > deadline:
            raise RuntimeError(f"no settled menu and no match before the deadline: {state}")
        await session.send(f"wait passes {READY_POLL_PASSES}")
        state = await session.send("state")
    return state


@mcp.tool()
async def game_connect(port: int = DEFAULT_PORT) -> dict:
    """Connect to a game that is already running with -control <port>."""
    await session.close()
    await session.connect(port, time.monotonic() + 10)
    return await session.send("state")


@mcp.tool()
async def game_kill() -> dict:
    """Quit the game (ending any match and closing its replay) and make sure the process is gone."""
    if session.socket is not None:
        try:
            await asyncio.wait_for(session.send("quit"), QUIT_GRACE_SECONDS)
        except (asyncio.TimeoutError, websockets.exceptions.WebSocketException, RuntimeError):
            pass
    await session.close()
    if session.process is not None:
        try:
            session.process.wait(QUIT_GRACE_SECONDS)
        except subprocess.TimeoutExpired:
            pass
    exit_code = session.process.returncode if session.process is not None else None
    session.kill_process()
    return {"ok": True, "exitCode": exit_code}


@mcp.tool()
async def game_command(line: str) -> dict:
    """Send any control command as typed. The grammar is in .claude/rules/commandline.md under -control."""
    return await session.send(line)


# ------------------------------------------------------------------------------------------------
# reading
# ------------------------------------------------------------------------------------------------

@mcp.tool()
async def game_state() -> dict:
    """Mode (shell, skirmish, replay...), frame, pause, map, screen size, shell screen, selection ids and armed command."""
    return await session.send("state")


@mcp.tool()
async def game_players() -> dict:
    """Every player: index, slot, name, side, money, power, rank, science points, dead, won, defeated."""
    return await session.send("players")


@mcp.tool()
async def game_objects(scope: str = "visible", near_x: float | None = None, near_y: float | None = None,
                       radius: float | None = None, template: str = "", kind: str = "",
                       object_id: int | None = None, limit: int | None = None) -> dict:
    """Objects on the map with position, health, owner, AI goal, path end and production queue.

    scope is mine, visible (what the local player can see) or all (ignores the shroud; for tests).
    kind is structure, dozer, harvester, infantry, aircraft, vehicle or other.
    """
    words = ["objects", scope]
    if near_x is not None and near_y is not None and radius is not None:
        words += ["near", near_x, near_y, radius]
    if template:
        words += ["template", template]
    if kind:
        words += ["kind", kind]
    if object_id is not None:
        words += ["id", object_id]
    if limit is not None:
        words += ["limit", limit]
    return await session.send(join_words(*words))


@mcp.tool()
async def game_windows(include_hidden: bool = False) -> dict:
    """The GUI window tree: name (File.wnd:Name), type, screen rectangle, enabled, text and gadget values."""
    return await session.send("windows all" if include_hidden else "windows")


@mcp.tool()
async def game_buttons() -> dict:
    """The command bar buttons the current selection shows: slot index, name, label, what it builds, enabled, rectangle."""
    return await session.send("buttons")


@mcp.tool()
async def game_messages() -> dict:
    """The on-screen message log."""
    return await session.send("messages")


@mcp.tool()
async def game_screenshot(max_width: int = SCREENSHOT_MAX_WIDTH) -> Image:
    """A picture of the game window as it was drawn on the next frame."""
    reply = await session.send("screenshot")
    if not reply.get("ok"):
        raise RuntimeError(reply.get("error", "screenshot failed"))

    bitmap_path = pathlib.Path(reply["path"])
    with PillowImage.open(bitmap_path) as picture:
        picture = picture.convert("RGB")
        if picture.width > max_width:
            picture = picture.resize((max_width, round(picture.height * max_width / picture.width)))
        encoded = io.BytesIO()
        picture.save(encoded, format="PNG")
    bitmap_path.unlink()
    return Image(data=encoded.getvalue(), format="png")


# ------------------------------------------------------------------------------------------------
# hands
# ------------------------------------------------------------------------------------------------

@mcp.tool()
async def game_click(x: int, y: int, button: str = "left", modifiers: str = "", double: bool = False) -> dict:
    """Click a pixel of the game window. modifiers is any of "SHIFT CTRL ALT", held for the click."""
    return await session.send(join_words("mouse", "dblclick" if double else "click", button, x, y, modifiers))


@mcp.tool()
async def game_drag(x1: int, y1: int, x2: int, y2: int, button: str = "left", steps: int = 8, modifiers: str = "") -> dict:
    """Drag between two pixels with a button held: a selection box with left, a formation line with right."""
    return await session.send(join_words("mouse", "drag", button, x1, y1, x2, y2, steps, modifiers))


@mcp.tool()
async def game_mouse(action: str, x: int, y: int, button: str = "left", notches: int = 1) -> dict:
    """Low-level mouse: action is move, down, up or wheel (notches positive zooms in)."""
    if action == "move":
        return await session.send(join_words("mouse", "move", x, y))
    if action == "wheel":
        return await session.send(join_words("mouse", "wheel", x, y, notches))
    return await session.send(join_words("mouse", action, button, x, y))


@mcp.tool()
async def game_world_click(x: float, y: float, button: str = "right", modifiers: str = "") -> dict:
    """Click a map position. The camera moves there first if it is off screen. Right click is the context order."""
    return await session.send(join_words("worldclick", button, x, y, modifiers))


@mcp.tool()
async def game_world_drag(x1: float, y1: float, x2: float, y2: float, button: str = "left", modifiers: str = "") -> dict:
    """Drag between two map positions, for example a selection box over part of the map."""
    return await session.send(join_words("worlddrag", button, x1, y1, x2, y2, modifiers))


@mcp.tool()
async def game_click_window(name: str, button: str = "left") -> dict:
    """Click the middle of a visible GUI window by its name from game_windows, for example
    MainMenu.wnd:ButtonSinglePlayer.

    On a menu it returns once the menu has stopped sliding: the buttons a click reveals stay hidden
    until the transition ends, and a button still moving ignores the next click.
    """
    reply = await session.send(join_words("window", "click", name, button))
    if not reply.get("ok"):
        return reply
    state = await wait_until_ready(time.monotonic() + MENU_TRANSITION_TIMEOUT_SECONDS)
    reply["shellScreen"] = state["shellScreen"]
    return reply


@mcp.tool()
async def game_key(key: str, action: str = "press", modifiers: str = "") -> dict:
    """Press, hold (down) or release (up) a key by its CommandMap.ini name, for example KEY_A. modifiers applies to press."""
    return await session.send(join_words("key", action, key, modifiers))


@mcp.tool()
async def game_camera(action: str = "get", x: float | None = None, y: float | None = None, value: float | None = None) -> dict:
    """Camera: get, lookat (x, y), zoom (value), angle (value, radians) or pitch (value, radians)."""
    if action == "get":
        return await session.send("camera get")
    if action == "lookat":
        return await session.send(join_words("camera", "lookat", x, y))
    return await session.send(join_words("camera", action, value))


@mcp.tool()
async def game_to_screen(x: float, y: float, z: float | None = None) -> dict:
    """The pixel a map position is drawn at, and whether it is on screen."""
    return await session.send(join_words("toscreen", x, y, z))


@mcp.tool()
async def game_to_world(px: int, py: int) -> dict:
    """The map position under a pixel, and whether the pixel is on the terrain."""
    return await session.send(join_words("toworld", px, py))


@mcp.tool()
async def game_wait(passes: int | None = None, frames: int | None = None) -> dict:
    """Wait a number of engine passes, or of logic frames (30 a second at normal speed, only in a running match)."""
    if frames is not None:
        return await session.send(join_words("wait", "frames", frames))
    return await session.send(join_words("wait", "passes", passes or 1))


# ------------------------------------------------------------------------------------------------
# orders as the local player
# ------------------------------------------------------------------------------------------------

@mcp.tool()
async def game_select(object_ids: list[int], add: bool = False) -> dict:
    """Select the local player's objects by id. An empty list clears the selection; add keeps what is selected."""
    if not object_ids:
        return await session.send("select none")
    return await session.send(join_words("select", "add" if add else None, *object_ids))


@mcp.tool()
async def game_order(order: str, x: float | None = None, y: float | None = None,
                     target_id: int | None = None, producer_id: int | None = None) -> dict:
    """Order the selection.

    At a point (x, y): move, forcemove, attackmove, waypoint, guard, attackground.
    At an object (target_id): attack, forceattack, enter, dock, repair, getrepaired, gethealed.
    Nothing to aim: stop, scatter, sell, evacuate, hold.
    rally sets producer_id's rally point to (x, y) and needs no selection.
    """
    if order == "rally":
        return await session.send(join_words("order", "rally", producer_id, x, y))
    if target_id is not None:
        return await session.send(join_words("order", order, target_id))
    return await session.send(join_words("order", order, x, y))


@mcp.tool()
async def game_press_button(index: int | None = None, name: str = "", builds: str = "") -> dict:
    """Click a command bar button by slot index, by button name, or by the template it builds.

    A structure button puts the building on the cursor (the reply's placing) and a targeted power arms
    a command (armedCommand); finish either with game_world_click using the left button, or cancel it
    with a right click.
    """
    if index is None:
        buttons = await session.send("buttons")
        for candidate in buttons.get("buttons", []):
            if (name and candidate["name"] == name) or (builds and candidate.get("builds") == builds):
                index = candidate["index"]
                break
        if index is None:
            return {"ok": False, "error": "no visible button matches", "buttons": buttons.get("buttons", [])}
    return await session.send(join_words("button", index))


@mcp.tool()
async def game_build(dozer_id: int, structure: str, x: float, y: float) -> dict:
    """Have a dozer or worker build a structure at a map position: select it, arm the button, place it."""
    selected = await session.send(join_words("select", dozer_id))
    if not selected.get("ok"):
        return selected
    await session.send("wait passes 2")
    armed = await game_press_button(builds=structure)
    if not armed.get("ok"):
        return armed
    return await session.send(join_words("worldclick", "left", x, y))


@mcp.tool()
async def game_produce(producer_id: int, unit: str, count: int = 1) -> dict:
    """Queue units at a production building: select it and click the button that builds the template."""
    selected = await session.send(join_words("select", producer_id))
    if not selected.get("ok"):
        return selected
    await session.send("wait passes 2")
    reply: dict = {}
    for _ in range(count):
        reply = await game_press_button(builds=unit)
        if not reply.get("ok"):
            break
    return reply


@mcp.tool()
async def game_skirmish(players: int, seed: int, map_name: str) -> dict:
    """Start a skirmish with the local player in slot 0 and AI in the rest. map_name as the map list names it."""
    return await session.send(join_words("skirmish", players, seed, map_name))


@mcp.tool()
async def game_spawn(slot: int, template: str, count: int, x: float, y: float, spacing: float | None = None) -> dict:
    """Test setup: create units for a seat for free. Not a player action and not in the replay."""
    return await session.send(join_words("spawn", slot, template, count, x, y, spacing))


if __name__ == "__main__":
    mcp.run()
