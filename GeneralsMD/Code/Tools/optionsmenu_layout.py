#!/usr/bin/env python3
"""Rebuild OptionsMenu.wnd as six tabbed pages on one grid.

EA's options screen is one 800x600 panel with everything on it at once, and it was already full
when it shipped: the language filter, the keyboard button and the four camera checkboxes are all
still in the file, parked off the right edge with HIDDEN set, because there was nowhere left to put
them.  Seventeen settings later there is no version of "find room" that works.

So the screen becomes six pages behind six buttons: Display, Graphics, Audio, Controls, Gameplay and
Network.  Every graphics setting is on Graphics, including the ones EA hid in a popup that only
opened when Custom was picked from a combo box on another page.  Every page is laid out on the same
two columns and the same rows, so a label starts in the same place whichever tab is open, and every
slider has a readout beside it.  Nothing is redrawn: the controls keep the images and tooltips they
shipped with, a label or a check box takes the lettering of the one next to it, and what is new is
cloned from a control that is already there.

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


# The panel and everything on it, in the layout's own 800x600 creation resolution.  One inner edge,
# 16 pixels in from the panel on both sides, is where the title, the tabs, the pages, the buttons
# and the version line all start and stop.
PANEL = (100, 16, 600, 568)          # left, top, width, height
INNER_LEFT, INNER_WIDTH = 116, 568
TITLE = (INNER_LEFT, 22, 400, 32)
RULE = (100, 58, 600, 1)
TAB_TOP, TAB_HEIGHT, TAB_GAP = 66, 28, 3
PAGE = (INNER_LEFT, 104, INNER_WIDTH, 408)
BUTTON_TOP, BUTTON_HEIGHT, BUTTON_WIDTH = 528, 32, 180
VERSION = (INNER_LEFT, 564, INNER_WIDTH, 16)

TABS = [
    ("PageDisplay",  "TabDisplay",  "GUI:OptionsTabDisplay"),
    ("PageGraphics", "TabGraphics", "GUI:OptionsTabGraphics"),
    ("PageAudio",    "TabAudio",    "GUI:OptionsTabAudio"),
    ("PageControls", "TabControls", "GUI:OptionsTabControls"),
    ("PageGameplay", "TabGameplay", "GUI:OptionsTabGameplay"),
    ("PageNetwork",  "TabNetwork",  "GUI:OptionsTabNetwork"),
]

# EA's four group panels.  Each drew a framed box of its own inside the page, sized for a heading
# and a row that are gone, so no two pages had their first control in the same place.  The panels
# go and their controls stand on the page; the two volume sliders EA left outside the audio panel
# come along with the rest.
GROUPS = ["VideoParent", "AudioParent", "ScrollParent", "NetworkParent"]
LOOSE = ["SliderMusicVolume", "SliderSFXVolume"]

# The advanced display popup.  Every setting in it is a graphics setting and it opened only when
# Custom was picked in a combo box on the display page, so a player choosing High never saw what
# High turns on.  Its controls move onto the Graphics page; the popup, its two buttons, its heading,
# its rules and its captions go.
ADVANCED = "WinAdvancedDisplayOptions"
GRAPHICS_CHECKS = [
    "Check3DShadows", "Check2DShadows", "CheckCloudShadows", "CheckGroundLighting",
    "CheckSmoothWater", "CheckShowProps", "CheckExtraAnimations", "CheckHeatEffects",
    "CheckBehindBuilding", "CheckNoDynamicLOD",
]
ADVANCED_KEEP = GRAPHICS_CHECKS + ["CheckUnlockFPS", "LowResSlider", "ParticleCapSlider",
                                   "LabelTextureResolution", "LabelParticleCap"]

# The tab a page opens is already captioned with the page's name, so EA's caption inside the panel
# says the same word a second time, and the rule under it then divides nothing from nothing.  Both
# go: the headings are unnamed statics inside the four group panels, found by the string they draw,
# and the four rules are loose children of the old parent.
HEADINGS = ["GUI:DisplayOptions", "GUI:AudioOptions", "GUI:ControlOptions", "GUI:NetworkOptions"]
RULES = ["Line1", "Line2", "Line3", "Line4"]

# The keyboard button opens a screen that no longer exists, so it goes out here rather than being
# deleted from the output by hand every time this runs.  It shipped HIDDEN and off the right edge
# with nothing behind it: the layout its code wanted was never in any .big.
DROP = ["ButtonKeyboardOptions"]

# Controls EA left unnamed that still have to be positioned, found by what they draw and given a
# name on the way through.
NAME_THE_UNNAMED = [
    ("GUI:AntiAliasing", "AntiAliasingLabel"),
    ("GUI:LowResSlider", "LabelTextureResolution"),
    ("GUI:ParticleCap", "LabelParticleCap"),
    ("GUI:Options", "LabelTitle"),
]

# Controls that are in the shipped file and are not wanted at all.  CheckAlternateMouse chose
# between the classic mouse and the alternate one; there is one mouse now, so the choice is gone.
DELETE = ["CheckAlternateMouse"]

# The templates new controls are cloned from, and whose lettering the moved ones take.
CHECK, LABEL, COMBO, SLIDER = "Retaliation", "DetailLabel", "ComboBoxDetail", "SliderGamma"

# The fork's own controls.  Where they stand is decided below with everything else.
#   (template, name, text key)
NEW_CONTROLS = [
    (LABEL,  "LabelWindowMode",        "GUI:WindowMode"),
    (COMBO,  "ComboBoxWindowMode",     None),
    (CHECK,  "CheckVSync",             "GUI:VSync"),
    (LABEL,  "LabelMSAA",              "GUI:MSAA"),
    (COMBO,  "ComboBoxMSAA",           None),
    (LABEL,  "LabelBloom",             "GUI:Bloom"),
    (COMBO,  "ComboBoxBloom",          None),
    (LABEL,  "LabelBloomThreshold",    "GUI:BloomThreshold"),
    (COMBO,  "ComboBoxBloomThreshold", None),
    (LABEL,  "LabelTextureFilter",     "GUI:TextureFilter"),
    (COMBO,  "ComboBoxTextureFilter",  None),
    (LABEL,  "LabelAnisotropy",        "GUI:Anisotropy"),
    (SLIDER, "SliderAnisotropy",       None),
    (LABEL,  "LabelHealthBars",        "GUI:HealthBars"),
    (COMBO,  "ComboBoxHealthBars",     None),
    (LABEL,  "LabelPlayerColors",      "GUI:PlayerColors"),
    (COMBO,  "ComboBoxPlayerColors",   None),
]

# A slider on its own says nothing about where it stands, so each one has a readout beside it that
# OptionsMenu.cpp writes.  Cloned from a label with its caption and tooltip taken off.
READOUTS = [
    "ValueGamma", "ValueTextureResolution", "ValueParticleCap", "ValueAnisotropy",
    "ValueMusicVolume", "ValueSFXVolume", "ValueVoiceVolume", "ValueScrollSpeed",
]

# a cloned slider keeps its template's range unless it is given one
SLIDER_RANGES = [("SliderAnisotropy", 0, 16)]

# EA's captions that do not fit the page: two popup headings written in capitals, and a check box
# caption that ran 20 pixels past the panel's right edge once it stood in a 268 pixel column.
#   (control, text key)
TEXT_OVERRIDES = [
    ("LabelTextureResolution", "GUI:TextureResolution"),
    ("LabelParticleCap", "GUI:ParticleLimit"),
    ("CheckNoDynamicLOD", "GUI:NeverLowerDetail"),
]

# Two columns to a page and one rhythm on all six: a setting is its label with its control under
# it and takes 56 pixels, a check box is its own label and takes 28.  The columns are 268 wide, 16
# apart and 8 in from the page edge.
COLUMNS = (124, 408)
COLUMN_WIDTH = 268
ROW_TOP, ROW_PITCH, CHECK_PITCH, ROW_HEIGHT = 112, 56, 28, 24
# the readout is wide enough for "Medium" in the label's 14 point, which 60 pixels cut off
SLIDER_WIDTH, READOUT_GAP = 180, 8


def row(index):
    return ROW_TOP + index * ROW_PITCH


def check_row(index):
    return ROW_TOP + index * CHECK_PITCH


#   (page, column, row, label, control, readout or None)
SETTINGS = [
    ("PageDisplay",  0, 0, "ResolutionLabel",        "ComboBoxResolution",     None),
    ("PageDisplay",  0, 1, "LabelWindowMode",        "ComboBoxWindowMode",     None),
    ("PageDisplay",  0, 2, "GammaLabel",             "SliderGamma",            "ValueGamma"),

    ("PageGraphics", 0, 0, "DetailLabel",            "ComboBoxDetail",         None),
    ("PageGraphics", 0, 1, "LabelTextureResolution", "LowResSlider",           "ValueTextureResolution"),
    ("PageGraphics", 0, 2, "LabelParticleCap",       "ParticleCapSlider",      "ValueParticleCap"),
    ("PageGraphics", 0, 3, "LabelMSAA",              "ComboBoxMSAA",           None),
    ("PageGraphics", 0, 4, "LabelBloom",             "ComboBoxBloom",          None),
    ("PageGraphics", 0, 5, "LabelBloomThreshold",    "ComboBoxBloomThreshold", None),
    ("PageGraphics", 0, 6, "LabelTextureFilter",     "ComboBoxTextureFilter",  None),
    ("PageGraphics", 1, 6, "LabelAnisotropy",        "SliderAnisotropy",       "ValueAnisotropy"),

    ("PageAudio",    0, 0, "MusicVolumeLabel",       "SliderMusicVolume",      "ValueMusicVolume"),
    ("PageAudio",    0, 1, "SFXVolumeLabel",         "SliderSFXVolume",        "ValueSFXVolume"),
    ("PageAudio",    0, 2, "VoiceVolumeLabel",       "SliderVoiceVolume",      "ValueVoiceVolume"),

    ("PageControls", 0, 0, "ScrollSpeedLabel",       "SliderScrollSpeed",      "ValueScrollSpeed"),

    ("PageGameplay", 0, 0, "LabelHealthBars",        "ComboBoxHealthBars",     None),
    ("PageGameplay", 0, 1, "LabelPlayerColors",      "ComboBoxPlayerColors",   None),

    ("PageNetwork",  0, 0, "StaticTextOnlineIpAddresses",    "ComboBoxOnlineIP",              None),
    ("PageNetwork",  0, 1, "StaticTextLANIpAddresses",       "ComboBoxIP",                    None),
    ("PageNetwork",  0, 2, "StaticTextFirewallPortOverride", "TextEntryFirewallPortOverride", None),
    ("PageNetwork",  0, 3, "StaticTextHTTPProxy",            "TextEntryHTTPProxy",            None),
]

#   (page, column, top, check box)
CHECKS = [("PageDisplay", 0, row(3), "CheckVSync")] + \
    [("PageGraphics", 1, check_row(index), name) for index, name in enumerate(GRAPHICS_CHECKS)] + [
    ("PageControls", 0, row(1),           "Retaliation"),
    ("PageControls", 0, row(1) + CHECK_PITCH, "CheckDoubleClickAttackMove"),
    ("PageNetwork",  1, row(0),           "CheckSendDelay"),
]

# What fits neither shape.  The antialiasing pair and the frame rate box are EA controls that ship
# hidden and stay hidden; they get a place on a page only so nothing is left parked off the panel.
#   (page, name, left, top, width, height)
OTHERS = [
    ("PageNetwork",  "ButtonFirewallRefresh", COLUMNS[1], row(2) + ROW_HEIGHT, 160, 25),
    ("PageDisplay",  "AntiAliasingLabel",     COLUMNS[1], row(0), COLUMN_WIDTH, ROW_HEIGHT),
    ("PageDisplay",  "ComboBoxAntiAliasing",  COLUMNS[1], row(0) + ROW_HEIGHT, COLUMN_WIDTH, ROW_HEIGHT),
    ("PageGraphics", "CheckUnlockFPS",        COLUMNS[1], row(5), COLUMN_WIDTH, ROW_HEIGHT),
]


def _named(name):
    return "OptionsMenu.wnd:%s" % name


def detach(parent, name):
    """Take one child out of parent's list and return it."""
    for i, child in enumerate(parent.children):
        if (child.name or "").split(":")[-1] == name:
            return parent.children.pop(i)
    raise KeyError(name)


def short(window):
    return (window.name or "").split(":")[-1]


def statement_end(props, start):
    end = start
    while ";" not in props[end]:
        end += 1
    return end


def drop_prop(window, key):
    """Take a statement out of a window, if it has one."""
    start = window.prop_index(key)
    if start >= 0:
        del window.props[start:statement_end(window.props, start) + 1]


def restyle(window, template, keys=("FONT", "HEADERTEMPLATE", "TEXTCOLOR")):
    """Give a control the lettering of the template.  The popup's check boxes were 10 point and the
    page's 14, and the network labels were 10 point beside 14 point labels on every other page; a
    column of settings in two type sizes does not read as one column."""
    for key in keys:
        source, target = template.prop_index(key), window.prop_index(key)
        if source < 0 or target < 0:
            continue
        lines = template.props[source:statement_end(template.props, source) + 1]
        moved = [window.indent + line[len(template.indent):] if line.startswith(template.indent)
                 else line for line in lines]
        window.props[target:statement_end(window.props, target) + 1] = moved


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
    """Six tabs share the inner width; the last takes the pixel the division leaves over, so the
    strip ends exactly where the pages and the buttons end."""
    width = (INNER_WIDTH - (len(TABS) - 1) * TAB_GAP) // len(TABS)
    left = INNER_LEFT + index * (width + TAB_GAP)
    if index == len(TABS) - 1:
        width = INNER_LEFT + INNER_WIDTH - left
    tab = clone(button_template, _named(name))
    tab.place(left, TAB_TOP, width, TAB_HEIGHT)
    tab.put_prop("TEXT", '"%s"' % text)
    return tab


def make_readout(label_template, name):
    readout = clone(label_template, _named(name))
    readout.children = []
    drop_prop(readout, "TEXT")
    drop_prop(readout, "TOOLTIPTEXT")
    readout.put_prop("STATICTEXTDATA", "CENTERED: 0")
    return readout


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
    for name in RULES + DROP:
        detach(old, name)

    # every control that is going onto a page, by name: out of EA's group panels, off the old
    # parent, out of the advanced popup, and new
    waiting = {}
    for group_name in GROUPS:
        for child in detach(old, group_name).children:
            waiting[short(child)] = child
    for name in LOOSE:
        waiting[name] = detach(old, name)
    for node in detach(old, ADVANCED).walk():
        if short(node) in ADVANCED_KEEP:
            waiting[short(node)] = node
    for template, name, text in NEW_CONTROLS:
        waiting[name] = make_control(templates[template], name, text, 0, 0, 1, 1)
    for name in READOUTS:
        waiting[name] = make_readout(templates[LABEL], name)
    for name, low, high in SLIDER_RANGES:
        waiting[name].put_prop("SLIDERDATA", "MINVALUE: %d, MAXVALUE: %d" % (low, high))
    for name, key in TEXT_OVERRIDES:
        waiting[name].put_prop("TEXT", '"%s"' % key)

    pages = dict((page_name, make_page(templates["VideoParent"], page_name))
                 for page_name, _tab, _text in TABS)

    def put(page_name, name, left, top, width, height, template=None):
        control = waiting.pop(name)
        control.place(left, top, width, height)
        if template is not None:
            restyle(control, templates[template])
        if template == LABEL:
            # a label reads from the left edge its control starts at, not from the middle of 268 pixels
            control.put_prop("STATICTEXTDATA", "CENTERED: 0")
        pages[page_name].children.append(control)

    for page_name, column, index, label, control, readout in SETTINGS:
        left, top = COLUMNS[column], row(index)
        put(page_name, label, left, top, COLUMN_WIDTH, ROW_HEIGHT, LABEL)
        put(page_name, control, left, top + ROW_HEIGHT,
            SLIDER_WIDTH if readout else COLUMN_WIDTH, ROW_HEIGHT)
        if readout:
            put(page_name, readout, left + SLIDER_WIDTH + READOUT_GAP, top + ROW_HEIGHT,
                COLUMN_WIDTH - SLIDER_WIDTH - READOUT_GAP, ROW_HEIGHT, LABEL)
    for page_name, column, top, name in CHECKS:
        put(page_name, name, COLUMNS[column], top, COLUMN_WIDTH, ROW_HEIGHT, CHECK)
    for page_name, name, left, top, width, height in OTHERS:
        put(page_name, name, left, top, width, height)
    if waiting:
        raise KeyError("given no place on a page: %s" % ", ".join(sorted(waiting)))

    # the frame round the pages, on the same inner edge
    layout.find("OptionsMenuParent").place(*PANEL)
    old.place(*PANEL)
    layout.find("LabelTitle").place(*TITLE)
    layout.find("Line").place(*RULE)
    gap = (INNER_WIDTH - 3 * BUTTON_WIDTH) // 2
    for index, name in enumerate(("ButtonDefaults", "ButtonAccept", "ButtonBack")):
        layout.find(name).place(INNER_LEFT + index * (BUTTON_WIDTH + gap), BUTTON_TOP,
                                BUTTON_WIDTH, BUTTON_HEIGHT)
    layout.find("LabelVersion").place(*VERSION)

    # last in the file is topmost: drawWindow walks the child list from the tail back to the head,
    # so the tabs go on after the old parent's own children and the pages after the tabs
    for index, (page_name, tab_name, text) in enumerate(TABS):
        old.children.append(make_tab(templates["ButtonDefaults"], tab_name, text, index))
    for page_name, _tab, _text in TABS:
        old.children.append(pages[page_name])

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


def overlaps(layout):
    """Two visible controls on one page drawn over each other.  Nothing at build time notices that,
    and on a page laid out by arithmetic it is the one mistake arithmetic makes."""
    found = []
    for page_name, _tab, _text in TABS:
        page = layout.find(page_name)
        if page is None:
            found.append("OptionsMenu.wnd has no %s" % page_name)
            continue
        visible = [(short(child), child.rect[:4]) for child in page.children
                   if "HIDDEN" not in (child.prop("STATUS") or "")]
        for index, (name, (left, top, right, bottom)) in enumerate(visible):
            for other, (other_left, other_top, other_right, other_bottom) in visible[index + 1:]:
                if left < other_right and other_left < right and top < other_bottom and other_top < bottom:
                    found.append("%s: %s and %s overlap" % (page_name, name, other))
    return found


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

    for _control, key in TEXT_OVERRIDES:
        if key not in keys:
            problems.append("caption %s is not in Patch.str" % key)

    for name in READOUTS + GRAPHICS_CHECKS:
        if name not in controls:
            problems.append("OptionsMenu.wnd has no %s, which OptionsMenu.cpp fills in" % name)
    if ADVANCED in controls:
        problems.append("OptionsMenu.wnd still carries %s; its controls are on the Graphics page"
                        % ADVANCED)
    problems.extend(overlaps(layout))

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
