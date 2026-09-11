"""One pass over everything game_mcp.py can do, against a real window.

    python smoke_test.py

Launches the game, clicks from the main menu into the skirmish screen, starts a match, plays it a
little as the local player through the same tool functions Claude calls, and quits. Every step that
does not come back as expected stops the run with what it got. The game is killed on the way out
whatever happened.
"""

import asyncio

import game_mcp

MAP_NAME = "Maps\\Tournament Desert\\Tournament Desert.map"
SEED = 5
TEST_UNIT = "AmericaVehicleHumvee"
TEST_UNIT_COUNT = 8
MOVE_DISTANCE = 250.0
MATCH_START_POLLS = 120
SCREEN_CHANGE_POLLS = 40
SINGLE_PLAYER_BUTTON = "MainMenu.wnd:ButtonSinglePlayer"
SKIRMISH_BUTTON = "MainMenu.wnd:ButtonSkirmish"
ORDER_SETTLE_FRAMES = 30
CONTROL_GROUP_KEY = "KEY_1"


def expect(reply: dict, what: str) -> dict:
    if not reply.get("ok"):
        raise AssertionError(f"{what}: {reply}")
    print(f"ok  {what}")
    return reply


def find_window(tree: list[dict], name: str) -> dict | None:
    for window in tree:
        if window["name"] == name:
            return window
        found = find_window(window.get("children", []), name)
        if found is not None:
            return found
    return None


async def wait_for_match() -> dict:
    for _ in range(MATCH_START_POLLS):
        state = await game_mcp.game_state()
        if state["inMatch"] and state["frame"] > 1:
            return state
        await game_mcp.game_wait(passes=30)
    raise AssertionError(f"the match never started: {state}")


async def check_shell() -> None:
    state = expect(await game_mcp.game_launch(), "launch")
    assert state["shellActive"], state

    windows = expect(await game_mcp.game_windows(), "windows")
    assert find_window(windows["windows"], SINGLE_PLAYER_BUTTON) is not None, f"no {SINGLE_PLAYER_BUTTON} on the main menu"

    # Skirmish sits in the single player drop-down, which stays hidden until its button is pressed
    expect(await game_mcp.game_click_window(SINGLE_PLAYER_BUTTON), "open the single player drop-down")
    windows = await game_mcp.game_windows()
    assert find_window(windows["windows"], SKIRMISH_BUTTON) is not None, "the drop-down did not show the skirmish button"
    expect(await game_mcp.game_click_window(SKIRMISH_BUTTON), "click the skirmish button")
    screen = state["shellScreen"]
    for _ in range(SCREEN_CHANGE_POLLS):
        await game_mcp.game_wait(passes=30)
        screen = (await game_mcp.game_state())["shellScreen"]
        if screen != state["shellScreen"]:
            break
    assert screen != state["shellScreen"], f"still on {screen}"
    print(f"    shell went from {state['shellScreen']} to {screen}")


async def check_group_move(command_center: dict) -> list[int]:
    origin = command_center["position"]
    expect(await game_mcp.game_spawn(0, TEST_UNIT, TEST_UNIT_COUNT, origin["x"] + 150, origin["y"] + 150), "spawn")
    await game_mcp.game_wait(frames=2)

    units = (await game_mcp.game_objects("mine", template=TEST_UNIT))["objects"]
    unit_ids = [unit["id"] for unit in units]
    assert len(unit_ids) == TEST_UNIT_COUNT, units

    expect(await game_mcp.game_select(unit_ids), "select")
    selection = (await game_mcp.game_state())["selection"]
    assert sorted(selection) == sorted(unit_ids), selection

    destination_x = origin["x"] + 150 + MOVE_DISTANCE
    destination_y = origin["y"] + 150
    expect(await game_mcp.game_world_click(destination_x, destination_y, "right"), "right click on the ground")
    await game_mcp.game_wait(frames=ORDER_SETTLE_FRAMES)

    moved = (await game_mcp.game_objects("mine", template=TEST_UNIT))["objects"]
    goals = {(round(unit["ai"]["goal"]["x"]), round(unit["ai"]["goal"]["y"])) for unit in moved}
    assert len(goals) > 1, f"every unit was sent to one point: {goals}"
    print(f"    {len(moved)} units, {len(goals)} different goals")
    return unit_ids


async def check_keys_and_modifiers(unit_ids: list[int]) -> None:
    expect(await game_mcp.game_select(unit_ids[:1]), "select one")
    second = (await game_mcp.game_objects("mine", object_id=unit_ids[1]))["objects"][0]["position"]
    expect(await game_mcp.game_world_click(second["x"], second["y"], "left", "SHIFT"), "shift click a second unit")
    selection = (await game_mcp.game_state())["selection"]
    assert len(selection) == 2, f"shift click did not add: {selection}"

    expect(await game_mcp.game_key(CONTROL_GROUP_KEY, "press", "CTRL"), "ctrl+1 makes a group")
    expect(await game_mcp.game_select([]), "select none")
    expect(await game_mcp.game_key(CONTROL_GROUP_KEY), "1 recalls it")
    await game_mcp.game_wait(passes=4)
    recalled = (await game_mcp.game_state())["selection"]
    assert sorted(recalled) == sorted(selection), f"group recall gave {recalled}, wanted {selection}"


async def check_command_bar(mine: list[dict]) -> None:
    dozer = next(unit for unit in mine if unit["kind"] == "dozer")
    expect(await game_mcp.game_select([dozer["id"]]), "select the worker")
    await game_mcp.game_wait(passes=4)
    buttons = expect(await game_mcp.game_buttons(), "buttons")["buttons"]
    structure = next(button for button in buttons if button.get("builds") and button["enabled"])
    armed = expect(await game_mcp.game_press_button(index=structure["index"]), f"arm {structure['builds']}")
    assert armed["placing"] == structure["builds"], armed
    expect(await game_mcp.game_click(10, 10, "right"), "right click cancels the placement")

    command_center = next(unit for unit in mine if unit["kind"] == "structure" and unit.get("production") is not None)
    expect(await game_mcp.game_select([command_center["id"]]), "select the command center")
    await game_mcp.game_wait(passes=4)
    buttons = expect(await game_mcp.game_buttons(), "buttons")["buttons"]
    worker = next(button for button in buttons if button.get("builds") and button["enabled"])
    expect(await game_mcp.game_produce(command_center["id"], worker["builds"]), f"queue {worker['builds']}")
    await game_mcp.game_wait(frames=2)
    queue = (await game_mcp.game_objects("mine", object_id=command_center["id"]))["objects"][0]["production"]
    assert queue, "nothing in the production queue"


async def run() -> None:
    await check_shell()

    expect(await game_mcp.game_skirmish(2, SEED, MAP_NAME), "skirmish")
    await wait_for_match()
    mine = expect(await game_mcp.game_objects("mine"), "objects mine")["objects"]
    command_center = next(unit for unit in mine if unit["kind"] == "structure")

    unit_ids = await check_group_move(command_center)
    await check_keys_and_modifiers(unit_ids)
    await check_command_bar(mine)

    picture = await game_mcp.game_screenshot()
    assert len(picture.data) > 0, "empty screenshot"
    print("ok  screenshot")

    expect(await game_mcp.game_kill(), "quit")


def main() -> None:
    try:
        asyncio.run(run())
        print("SMOKE TEST PASSED")
    finally:
        # only the copy this test started: another session may be running its own
        game_mcp.session.kill_process()


if __name__ == "__main__":
    main()
