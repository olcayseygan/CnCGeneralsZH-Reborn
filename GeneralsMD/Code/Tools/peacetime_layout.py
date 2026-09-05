#!/usr/bin/env python3
"""Put the peace time combo box into the two network lobby layouts.

Peace time is a host setting like starting cash and the superweapon limit, so it wants a control
next to those two - in the LAN lobby and the GameSpy staging room, and only those.  The skirmish
lobby has no such box on purpose: a skirmish is played against computer players, and a computer
player has no peace time.  There was no room left on either option row, so the box is cloned from
ComboBoxStartingCash (which is how it inherits EA's images, font and colours instead of looking like
something a tool made) and dropped in the nearest gap:

    LanGameOptionsMenu        right of the superweapon checkbox, same row
    GameSpyGameOptionsMenu    under the option row, which costs the chat listbox 22 pixels -
                              the only free space on that screen

    python bigfile.py extract ../../Run/WindowZH.big "*options*" -o wnd
    python peacetime_layout.py wnd/Window/Menus ../Data/Window/Menus

The outputs are tracked masters under Code/Data; the build copies them to Run/Window/Menus/, where
a loose file beats WindowZH.big.  Layouts are not in the multiplayer INI checksum, so a player on a
stale copy gets a screen without the control, not a refused join - which is why the menu code null
checks the window instead of assuming it.

    python peacetime_layout.py selfcheck

reads the tracked files back and checks they still agree with the code: each layout carries
ComboBoxPeaceTime, and every string the game fetches for it is in Patch.str.  Run by CTest as
peacetime_selfcheck.
"""

import os
import sys

import wndlayout
from wndlayout import clone


HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")

# layout -> where the new box goes, in the layout's own 800x600 creation resolution
PLACEMENT = {
    "LanGameOptionsMenu":      (616, 332, 134, 24),
    "GameSpyGameOptionsMenu":  (77, 338, 140, 24),
}

# the GameSpy screen has no gap at all, so its chat listbox gives up its top edge
LISTBOX_TOP = ("GameSpyGameOptionsMenu", "ListboxChatWindowGameSpyGameSetup", 366)

# every string the peace time code asks TheGameText for
STRINGS = [
    "GUI:PeaceTimeOff",
    "GUI:PeaceTimeFormat",
    "GUI:PeaceTimeStarted",
    "GUI:PeaceTimeRemaining",
    "GUI:PeaceTimeOver",
    "GUI:PeaceTimeHud",
    "TOOLTIP:PeaceTime",
]


def build(layout, menu):
    """Clone ComboBoxStartingCash into ComboBoxPeaceTime and place it."""
    template = layout.find("ComboBoxStartingCash")
    if template is None:
        raise SystemExit("%s has no ComboBoxStartingCash to clone" % menu)

    box = clone(template, "%s.wnd:ComboBoxPeaceTime" % menu)
    box.place(*PLACEMENT[menu])
    box.put_prop("TOOLTIPTEXT", '"TOOLTIP:PeaceTime"')

    layout.root.parent_of(template).children.append(box)

    if menu == LISTBOX_TOP[0]:
        listbox = layout.find(LISTBOX_TOP[1])
        l, t, r, b, cw, ch = listbox.rect
        listbox.rect = (l, LISTBOX_TOP[2], r, b, cw, ch)

    return layout


def read_strings():
    keys = set()
    with open(PATCH_STR, "r") as fp:
        for line in fp:
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def selfcheck():
    problems = []

    keys = read_strings()
    for key in STRINGS:
        if key not in keys:
            problems.append("%s is not in Patch.str" % key)

    for menu in sorted(PLACEMENT):
        path = os.path.join(MENUS, menu + ".wnd")
        if not os.path.exists(path):
            problems.append("%s.wnd is not tracked under Data/Window/Menus" % menu)
            continue
        layout = wndlayout.load(path)
        if layout.find("ComboBoxPeaceTime") is None:
            problems.append("%s.wnd has no ComboBoxPeaceTime" % menu)

    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1

    print("peace time: %d layouts, %d strings" % (len(PLACEMENT), len(STRINGS)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()

    if len(argv) != 3:
        print(__doc__)
        return 2

    indir, outdir = argv[1], argv[2]
    for menu in sorted(PLACEMENT):
        layout = wndlayout.load(os.path.join(indir, menu + ".wnd"))
        out = os.path.join(outdir, menu + ".wnd")
        wndlayout.save(build(layout, menu), out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
