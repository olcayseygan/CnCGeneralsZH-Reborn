#!/usr/bin/env python3
"""Rebuild OptionsMenu.wnd as five tabbed pages.

EA's options screen is one 800x600 panel with everything on it at once, and it was already full
when it shipped: the language filter, the keyboard button and the four camera checkboxes are all
still in the file, parked off the right edge with HIDDEN set, because there was nowhere left to put
them.  Seventeen settings later there is no version of "find room" that works.

So the screen becomes five pages behind five buttons.  Nothing is redrawn: every control keeps the
exact images, fonts, colours and tooltips it shipped with, and the four group panels EA already had
(video, audio, scrolling, network) move into a page each without being touched inside.  What is new
is cloned from a control that is already there, which is why the new checkboxes look like the old
ones instead of like something a tool made.

    python optionsmenu_layout.py <shipped OptionsMenu.wnd> <output .wnd>

The input is the file out of WindowZH.big:

    python bigfile.py extract ../../Run/WindowZH.big "*/OptionsMenu.wnd" -o wnd
    python optionsmenu_layout.py wnd/Window/Menus/OptionsMenu.wnd ../Data/Window/Menus/OptionsMenu.wnd

The output is the tracked master under Code/Data; the build copies it to Run/Window/Menus/, where a
loose file beats the archive.  Layouts are not in the multiplayer INI checksum, so this one does not
have to match across a network game.

    python optionsmenu_layout.py selfcheck

reads the three tracked files back and checks they still agree: every widget TheOptionCatalog names
exists in the layout, and every label, tooltip and combo box entry it needs is in Patch.str.  Run by
CTest as optionsmenu_selfcheck.
"""

import os
import re
import sys

import wndlayout
from wndlayout import clone


# The page frame, in the layout's own 800x600 creation resolution.
PAGE = (151, 96, 485, 420)          # left, top, width, height
TAB_TOP, TAB_HEIGHT, TAB_WIDTH = 60, 28, 97

TABS = [
    ("PageDisplay",  "TabDisplay",  "GUI:OptionsTabDisplay"),
    ("PageAudio",    "TabAudio",    "GUI:OptionsTabAudio"),
    ("PageControls", "TabControls", "GUI:OptionsTabControls"),
    ("PageGameplay", "TabGameplay", "GUI:OptionsTabGameplay"),
    ("PageNetwork",  "TabNetwork",  "GUI:OptionsTabNetwork"),
]

# EA's four group panels and the loose controls that visually belong to each, with the page they
# move to.  A group's top left corner goes to the page's top left corner; everything listed with it
# shifts by the same amount, which is what keeps the two volume sliders with the audio panel they
# were never actually a child of.
GROUPS = [
    ("PageDisplay",  "VideoParent",   []),
    ("PageAudio",    "AudioParent",   ["SliderMusicVolume", "SliderSFXVolume"]),
    ("PageControls", "ScrollParent",  []),
    ("PageNetwork",  "NetworkParent", []),
]

# The tab a page opens is already captioned with the page's name, so EA's caption inside the panel
# says the same word a second time, and the rule under it then divides nothing from nothing.  Both
# go: the headings are unnamed statics inside the four group panels, found by the string they draw,
# and the four rules are loose children of the old parent.
HEADINGS = ["GUI:DisplayOptions", "GUI:AudioOptions", "GUI:ControlOptions", "GUI:NetworkOptions"]
RULES = ["Line1", "Line2", "Line3", "Line4"]

# The antialiasing label is the one control in the file EA left unnamed that still has to be
# positioned by hand, so it gets a name on the way through.
NAME_THE_UNNAMED = [("GUI:AntiAliasing", "AntiAliasingLabel")]

# Controls that are in the shipped file and are not wanted at all.  CheckAlternateMouse chose
# between the classic mouse and the alternate one; there is one mouse now, so the choice is gone.
DELETE = ["CheckAlternateMouse"]

# The new controls, cloned from a control of the same kind that is already in the file.
#   (page, template, name, text key, left, top, width, height)
CHECK, LABEL, COMBO, SLIDER = "Retaliation", "DetailLabel", "ComboBoxDetail", "SliderGamma"

CONTROLS = [
    # Display: the two device settings and the two bloom knobs, in the space the audio panel left
    ("PageDisplay", LABEL,  "LabelWindowMode",       "GUI:WindowMode",       400, 104, 230, 24),
    ("PageDisplay", COMBO,  "ComboBoxWindowMode",    None,                   400, 128, 200, 24),
    ("PageDisplay", LABEL,  "LabelMSAA",             "GUI:MSAA",             400, 160, 230, 24),
    ("PageDisplay", COMBO,  "ComboBoxMSAA",          None,                   400, 184, 200, 24),
    ("PageDisplay", LABEL,  "LabelBloom",            "GUI:Bloom",            400, 216, 230, 24),
    ("PageDisplay", SLIDER, "SliderBloom",           None,                   404, 240, 209, 24),
    ("PageDisplay", LABEL,  "LabelBloomThreshold",   "GUI:BloomThreshold",   400, 272, 230, 24),
    ("PageDisplay", SLIDER, "SliderBloomThreshold",  None,                   404, 296, 209, 24),

    # Gameplay: a page that did not exist at all before, and is one setting.  The eight build and
    # HUD conveniences that used to sit here are on for everybody now and left TheOptionCatalog
    # with their checkboxes; the four camera habits that used to be on Controls went the same way.
    ("PageGameplay", LABEL, "LabelHealthBars",       "GUI:HealthBars",       160, 104, 230, 24),
    ("PageGameplay", COMBO, "ComboBoxHealthBars",    None,                   160, 128, 200, 24),
]

# What is left of EA's own controls, page by page, at the rhythm the display page's right column
# sets: a label on the 104/160/216/272 rows and the thing it labels 24 pixels under it.  The
# panels shrink to what they now hold - each shipped tall enough for a heading, a rule and a row
# that is not there any more.
#   (name, left, top, width, height)
RELAYOUT = [
    ("VideoParent",                   151, 100, 236, 224),
    ("ResolutionLabel",               156, 104, 144, 24),
    ("ComboBoxResolution",            160, 128, 144, 24),
    ("DetailLabel",                   156, 160, 144, 24),
    ("ComboBoxDetail",                160, 184, 144, 24),
    ("GammaLabel",                    156, 216, 196, 24),
    ("SliderGamma",                   164, 240, 208, 24),
    # the antialiasing label shipped on top of the gamma slider, both of them at y=241; it is only
    # visible at all because the shipped panel drew it before the slider
    ("AntiAliasingLabel",             156, 272, 187, 24),
    ("ComboBoxAntiAliasing",          160, 296, 144, 24),

    ("AudioParent",                   151, 100, 244, 168),
    ("MusicVolumeLabel",              160, 104, 184, 24),
    ("SliderMusicVolume",             164, 128, 208, 24),
    ("SFXVolumeLabel",                160, 160, 223, 24),
    ("SliderSFXVolume",               164, 184, 208, 24),
    ("VoiceVolumeLabel",              160, 216, 183, 24),
    ("SliderVoiceVolume",             164, 240, 208, 24),

    ("ScrollParent",                  151, 100, 484,  88),
    ("Retaliation",                   160, 104, 192, 24),
    ("CheckDoubleClickAttackMove",    387, 104, 246, 24),
    ("ScrollSpeedLabel",              160, 136, 464, 24),
    ("SliderScrollSpeed",             167, 160, 444, 24),

    ("NetworkParent",                 151, 100, 484, 96),
    ("StaticTextOnlineIpAddresses",   152, 104,  88, 24),
    ("ComboBoxOnlineIP",              240, 104, 128, 24),
    ("StaticTextLANIpAddresses",      376, 104, 104, 24),
    ("ComboBoxIP",                    480, 104, 128, 24),
    ("StaticTextFirewallPortOverride",152, 136, 152, 24),
    ("TextEntryFirewallPortOverride", 304, 136, 144, 24),
    ("ButtonFirewallRefresh",         480, 136, 116, 25),
    ("StaticTextHTTPProxy",           152, 160, 116, 24),
    ("TextEntryHTTPProxy",            304, 160, 144, 24),
    ("CheckSendDelay",                480, 168, 128, 24),
]


def _named(name):
    return "OptionsMenu.wnd:%s" % name


def detach(parent, name):
    """Take one child out of parent's list and return it."""
    for i, child in enumerate(parent.children):
        if (child.name or "").split(":")[-1] == name:
            return parent.children.pop(i)
    raise KeyError(name)


def make_page(video_parent, name):
    """An empty container the size of the page area.

    Cloned from VideoParent for one property: SYSTEMCALLBACK is PassMessagesToParentSystem, without
    which a click on anything inside the page stops at the page and never reaches OptionsMenuSystem.
    SEE_THRU then keeps it from drawing over the panel art behind it."""
    page = clone(video_parent, _named(name))
    page.children = []
    page.place(PAGE[0], PAGE[1], PAGE[2], PAGE[3])
    page.set_prop("STATUS", "ENABLED+NOFOCUS+SEE_THRU")
    return page


def make_tab(button_template, name, text, index):
    tab = clone(button_template, _named(name))
    tab.place(PAGE[0] + index * TAB_WIDTH, TAB_TOP, TAB_WIDTH - 2, TAB_HEIGHT)
    tab.put_prop("TEXT", '"%s"' % text)
    return tab


def setting_of(name):
    """The setting a control belongs to, which is its name without the kind in front.  A label and
    the combo box under it are two controls for one setting and share one tooltip string."""
    for prefix in ("ComboBox", "Slider", "Check", "Label"):
        if name.startswith(prefix):
            return name[len(prefix):]
    return name


def make_control(template, name, text, left, top, width, height):
    control = clone(template, _named(name))
    control.children = []
    control.place(left, top, width, height)
    if text is not None:
        control.put_prop("TEXT", '"%s"' % text)
    control.put_prop("TOOLTIPTEXT", '"TOOLTIP:%s"' % setting_of(name))
    # a label wide enough to read is a label that starts at the left, not one centred in 230 pixels
    if control.prop("STATICTEXTDATA") is not None:
        control.set_prop("STATICTEXTDATA", "CENTERED: 0")
    return control


def drawn_text(window):
    """The TEXT a window draws, or None.  Used to reach the controls EA left unnamed."""
    statement = window.prop("TEXT")
    if statement is None:
        return None
    match = re.search(r'"([^"]*)"', statement)
    return match.group(1) if match else None


def drop_by_name(root, names):
    """Delete every window with one of these names, wherever it sits."""
    wanted = set(names)
    for node in list(root.walk()):
        node.children = [child for child in node.children
                         if (child.name or "").split(":")[-1] not in wanted]


def drop_by_text(root, texts):
    """Delete every window drawing one of these strings, wherever it sits."""
    wanted = set(texts)
    for node in list(root.walk()):
        node.children = [child for child in node.children
                         if drawn_text(child) not in wanted]


def build(layout):
    old = layout.find("OptionsMenuParentOld")
    templates = dict((name, layout.find(name))
                     for name in (CHECK, LABEL, COMBO, SLIDER, "VideoParent", "ButtonDefaults"))

    for text, name in NAME_THE_UNNAMED:
        for node in layout.root.walk():
            # "unnamed" in this file means NAME = "OptionsMenu.wnd:", the layout and nothing after it
            if drawn_text(node) == text and not (node.name or "").split(":")[-1]:
                node.name = _named(name)

    drop_by_text(layout.root, HEADINGS)
    drop_by_name(layout.root, DELETE)
    for name in RULES:
        detach(old, name)

    pages = {}
    for index, (page_name, tab_name, text) in enumerate(TABS):
        pages[page_name] = make_page(templates["VideoParent"], page_name)
        old.children.append(make_tab(templates["ButtonDefaults"], tab_name, text, index))

    for page_name, group_name, along_with in GROUPS:
        page = pages[page_name]
        group = detach(old, group_name)
        left, top = group.rect[:2]
        group.move_to(PAGE[0], PAGE[1] + 4)
        page.children.append(group)
        for name in along_with:
            control = detach(old, name)
            control.move_by(PAGE[0] - left, PAGE[1] + 4 - top)
            page.children.append(control)

    for page_name, template, name, text, left, top, width, height in CONTROLS:
        pages[page_name].children.append(
            make_control(templates[template], name, text, left, top, width, height))

    for page_name, _tab, _text in TABS:
        old.children.append(pages[page_name])

    # last, because place() writes an absolute rectangle and any later move would undo it
    for name, left, top, width, height in RELAYOUT:
        control = layout.find(name)
        if control is None:
            raise KeyError("RELAYOUT names %s, which is not in the layout" % name)
        control.place(left, top, width, height)

    # last in the file is topmost: drawWindow walks the child list from the tail back to the head,
    # and winPointInChild answers with the head, so the advanced-display popup has to stay the last
    # child or a page would draw over it and swallow its clicks
    old.children.append(detach(old, "WinAdvancedDisplayOptions"))

    return layout


# ---------------------------------------------------------------------------
# selfcheck: the three files have to agree
#
# TheOptionCatalog names a widget, the layout has to carry a control with that name, and both the
# label and the tooltip have to be in Patch.str or the screen draws a row of raw key names.  Nothing
# at build time notices any of that - a typo in a widget name just means the control is silently
# never filled in - so it is checked here, against the tracked files.
# ---------------------------------------------------------------------------

_HERE = os.path.dirname(os.path.abspath(__file__))
_CODE = os.path.dirname(_HERE)

CATALOG = os.path.join(_CODE, "GameEngine", "Source", "Common", "OptionsCatalog.cpp")
STRINGS = os.path.join(_CODE, "Data", "Patch.str")
LAYOUT = os.path.join(_CODE, "Data", "Window", "Menus", "OptionsMenu.wnd")
INCLUDE = os.path.join(_CODE, "GameEngine", "Include")

_ROW = re.compile(
    r'\{\s*"(?P<ini>[^"]+)",\s*'
    r'(?:OPT_WND\(\s*"(?P<widget>[^"]+)"\s*\)|"")\s*,\s*'
    r'"(?P<label>[^"]*)",\s*'
    r'(?P<kind>OPTION_\w+),\s*APPLY_\w+,\s*(?P<lo>[^,]+?),\s*(?P<hi>[^,]+?),')


def read_catalog():
    with open(CATALOG, "rb") as fp:
        text = fp.read().decode("latin-1")
    return [match.groupdict() for match in _ROW.finditer(text)]


def read_strings():
    """The keys defined in Patch.str.  A key is a line of its own, the value is the quoted line
    under it, and '//' is the only comment GameText.cpp's parseStringFile knows."""
    keys = set()
    with open(STRINGS, "rb") as fp:
        for line in fp.read().decode("latin-1").splitlines():
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def enum_count(name):
    """The value of an enum constant like WINDOW_MODE_COUNT, out of whichever header declares it."""
    pattern = re.compile(r"\b%s\s*=\s*(\d+)" % re.escape(name))
    for root, _dirs, files in os.walk(INCLUDE):
        for filename in files:
            if not filename.endswith(".h"):
                continue
            with open(os.path.join(root, filename), "rb") as fp:
                match = pattern.search(fp.read().decode("latin-1"))
            if match:
                return int(match.group(1))
    return None


def selfcheck():
    rows = read_catalog()
    keys = read_strings()
    layout = wndlayout.load(LAYOUT)
    controls = set((node.name or "").split(":")[-1] for node in layout.root.walk())

    problems = []

    if len(rows) < 9:
        problems.append("only %d catalog rows parsed, the regex has stopped matching" % len(rows))

    for _page, _tab, text in TABS:
        if text not in keys:
            problems.append("tab caption %s is not in Patch.str" % text)

    for row in rows:
        widget, label = row["widget"], row["label"]
        if widget is None:
            # a setting with no control yet is allowed, but then it has no label either
            if label:
                problems.append("%s has a label key and no widget" % row["ini"])
            continue

        if widget not in controls:
            problems.append("%s names %s, which is not in OptionsMenu.wnd" % (row["ini"], widget))
        if not label:
            problems.append("%s has a widget and no label key" % row["ini"])
            continue
        if label not in keys:
            problems.append("%s label %s is not in Patch.str" % (row["ini"], label))

        tooltip = "TOOLTIP:%s" % setting_of(widget)
        if tooltip not in keys:
            problems.append("%s tooltip %s is not in Patch.str" % (row["ini"], tooltip))

        if row["kind"] == "OPTION_ENUM":
            constant = row["hi"].split()[0]
            count = enum_count(constant)
            if count is None:
                problems.append("%s: no header declares %s" % (row["ini"], constant))
                continue
            for entry in range(int(row["lo"]), count):
                if "%s%d" % (label, entry) not in keys:
                    problems.append("%s entry %s%d is not in Patch.str" % (row["ini"], label, entry))

    for problem in problems:
        print("optionsmenu: %s" % problem)
    if problems:
        return 1

    print("optionsmenu: %d catalog rows agree with %s and %s"
          % (len(rows), os.path.basename(LAYOUT), os.path.basename(STRINGS)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()

    if len(argv) != 3:
        print(__doc__)
        return 2

    layout = wndlayout.load(argv[1])
    wndlayout.save(build(layout), argv[2])
    print("%s: %d windows" % (argv[2], sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
