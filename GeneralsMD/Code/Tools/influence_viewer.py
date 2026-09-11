"""Watch what one player knows in a running game, over its -control WebSocket.

Start the game with -control and get a match going, then:

    python influence_viewer.py                 # port 8787, player index 2
    python influence_viewer.py 8787 3
    python influence_viewer.py check           # the self-check, no window

Needs PyQt5 and numpy. The player index is the "index" that status reports, not the slot: the list
opens with the neutral and civilian players, which have no seat and are left out of the dropdown.

The window reads left to right, the way the data flows. The terrain and every unit this player
knows of go in; each side's influence is spread from them at the falloff rate; the sides are laid
over the ground together. Under that, the share of known value each side holds across the match,
and what those forces are made of.

Our forces are blue, allies green and the enemies this player can see right now red. An enemy that
leaves sight stays, in faded rose, where it was last seen, until vision covers that spot again and
finds it gone; scouting is what keeps the picture of the enemy true. The force balance only counts
what the player knows of, so an unscouted enemy base is missing from it. Influence is scaled to its
own peak on a square-root scale, so it says where; the peak in the status line says how much.
North is up, as on the radar.

The game is read ten times a second and the influence eases toward each new reading at the
screen's rate, so a moving army glides instead of stepping.
"""

import math
import sys
import threading
import time
from collections import namedtuple

import numpy
from PyQt5.QtCore import QPointF, QRect, QRectF, Qt, QTimer
from PyQt5.QtGui import QColor, QImage, QKeySequence, QPainter, QPalette, QPen, QPolygonF
from PyQt5.QtWidgets import (QAbstractSpinBox, QApplication, QCheckBox, QComboBox, QDoubleSpinBox, QFrame, QGridLayout,
                             QHBoxLayout, QLabel, QMainWindow, QPushButton, QShortcut, QSizePolicy, QVBoxLayout,
                             QWidget)

from control_client import Control, ControlError, DEFAULT_PORT

DEFAULT_PLAYER_INDEX = 2
REFRESH_SECONDS = 0.1
RECONNECT_SECONDS = 2.0
SOCKET_TIMEOUT_SECONDS = 5.0
FRAME_MILLISECONDS = 16
EASE_SECONDS = 0.15
# influence strength runs 0 to 1; a gap under this is less than one level of one colour channel
SETTLED_LEVEL = 0.002
WINDOW_SIZE = (1280, 900)
MINIMUM_WINDOW_HEIGHT = 640
MINIMUM_PANEL_SIZE = 120
LOGIC_FRAMES_PER_SECOND = 30
HISTORY_FRAMES = LOGIC_FRAMES_PER_SECOND

SPACE_1 = 4
SPACE_2 = 8
SPACE_3 = 16
SWATCH_SIZE = 12
TEXT_PIXELS = 14
NOTE_WIDTH = 460
PLAYER_TEXT_LENGTH = 12
# padding, border and the arrow or the step buttons a field draws beside its text
FIELD_CHROME_WIDTH = 40
UNIT_DOT_PIXELS = 3
STRUCTURE_DOT_PIXELS = 5
COMPOSITION_ROW_LIMIT = 14
# at most this much of the composition grid goes to the side columns; the rest keeps template names readable
SIDE_COLUMNS_SHARE = 0.55
EMPTY_DOT_RADIUS = 1.5

BACKGROUND = (15, 17, 21)
SURFACE = (23, 26, 33)
SURFACE_RAISED = (34, 38, 47)
SURFACE_HOVER = (46, 51, 62)
SURFACE_PRESSED = (60, 66, 80)
BORDER_STRONG = (112, 121, 138)
TEXT = (232, 234, 240)
TEXT_MUTED = (163, 170, 184)
MAP_EMPTY = BACKGROUND
TEXT_DISABLED = BORDER_STRONG
CLIFF_FILL = SURFACE_RAISED
WATER_FILL = (22, 34, 52)
TERRAIN_EDGE = BORDER_STRONG

SIDE_OWN = 0
SIDE_ALLY = 1
SIDE_ENEMY = 2
SIDE_REMEMBERED = 3
SIDE_COUNT = 4
SIDE_COLOURS = ((70, 130, 255), (60, 220, 120), (255, 80, 80), (200, 110, 120))
SIDE_NAMES = ("Us", "Allies", "Enemies in sight", "Last seen")
SIDE_COLUMN_NAMES = ("Us", "Allies", "In sight", "Last seen")

# the terrain reply's characters, less "0"
TERRAIN_PASSABLE = 0
TERRAIN_CLIFF = 1
TERRAIN_WATER = 2

DEFAULT_FALLOFF_RATE = 0.3
FALLOFF_STEP = 0.05
FALLOFF_LOWEST = 0.05
FALLOFF_HIGHEST = 0.95
KERNEL_FLOOR = 0.02

STATE_CONNECTING = "connecting"
STATE_LOST = "lost"
STATE_NO_MATCH = "no match"
STATE_REFUSED = "refused"
STATE_LIVE = "live"

SEPARATOR = " · "

Snapshot = namedtuple("Snapshot", "serial state message players frame player units remembered terrain history")
Picture = namedtuple("Picture", "strength peak counts empty_message")
BalanceSample = namedtuple("BalanceSample", "frame unit_values structure_values")


# -- influence, as numbers ---------------------------------------------------------------------
#
# A sighting is [x, y, side, structure, template, cost]: a units reply entry less its id, or a
# remembered enemy.

def sightings_of(reply, remembered):
    return [unit[1:] for unit in reply["units"]] + remembered


def counted(sightings, with_structures):
    return [sighting for sighting in sightings if with_structures or not sighting[3]]


def falloff_kernel(rate):
    """A square of weights, centre 1, losing `rate` of the weight per cell of distance and cut to 0
    where it falls under KERNEL_FLOOR; row is dy and column is dx."""
    kept = 1.0 - rate
    reach = int(math.log(KERNEL_FLOOR) / math.log(kept))
    offsets = numpy.arange(-reach, reach + 1)
    weights = kept ** numpy.hypot(offsets[numpy.newaxis, :], offsets[:, numpy.newaxis])
    weights[weights < KERNEL_FLOOR] = 0.0
    return weights


def density_layers(sightings, width, height, cell_size, kernel):
    """One height x width grid per side, each sighting's kernel stamped around its cell. Sightings are
    binned per cell first, so an army of fifty in one cell is one stamp, not fifty."""
    reach = kernel.shape[0] // 2
    padded = numpy.zeros((SIDE_COUNT, height + 2 * reach, width + 2 * reach))
    counts = {}
    for x, y, side, _, _, _ in sightings:
        cell = (side, min(width - 1, max(0, int(x // cell_size))), min(height - 1, max(0, int(y // cell_size))))
        counts[cell] = counts.get(cell, 0) + 1
    for (side, column, row), count in counts.items():
        padded[side, row:row + 2 * reach + 1, column:column + 2 * reach + 1] += count * kernel
    return padded[:, reach:reach + height, reach:reach + width]


def density_picture(reply, remembered, kernel, with_structures):
    """Every side's influence as strength from 0 to 1 against one shared peak, on a square-root scale."""
    kept = counted(sightings_of(reply, remembered), with_structures)
    layers = density_layers(kept, reply["width"], reply["height"], reply["cellSize"], kernel)
    counts = [0] * SIDE_COUNT
    for _, _, side, _, _, _ in kept:
        counts[side] += 1
    peak = float(layers.max())
    strength = numpy.sqrt(layers / peak) if peak else layers
    empty = "" if peak else "No units counted for this player yet."
    return Picture(strength, peak, counts, empty)


def density_rgb(strength, colours):
    """height x width x 3 floats on black, each layer of `strength` added in its own colour, north up.
    Black is what lets the painter add it over the terrain."""
    rgb = numpy.einsum("syx,sc->yxc", strength, numpy.array(colours, dtype=float))
    return numpy.minimum(rgb, 255.0)[::-1]


def ease(shown, target, elapsed):
    """Move what is on screen toward the latest reading by the share of the gap that EASE_SECONDS of
    exponential easing covers in `elapsed`."""
    return shown + (target - shown) * (1.0 - math.exp(-elapsed / EASE_SECONDS))


def to_image(rgb):
    pixels = numpy.ascontiguousarray(numpy.clip(rgb, 0, 255).astype(numpy.uint8))
    height, width, _ = pixels.shape
    return QImage(pixels.data, width, height, 3 * width, QImage.Format_RGB888).copy()


# -- the ground --------------------------------------------------------------------------------

def terrain_kinds(reply):
    """The terrain reply as a height x width array of TERRAIN_ kinds, row 0 the southern edge."""
    cells = numpy.frombuffer(reply["cells"].encode("ascii"), dtype=numpy.uint8)
    return cells.reshape(reply["height"], reply["width"]) - ord("0")


def terrain_rgb(kinds, filled):
    """The ground north up: a line wherever one kind meets another, and with `filled`, cliffs and water
    shaded under the lines."""
    edge = numpy.zeros(kinds.shape, dtype=bool)
    edge[:, :-1] |= kinds[:, :-1] != kinds[:, 1:]
    edge[:-1, :] |= kinds[:-1, :] != kinds[1:, :]
    rgb = numpy.empty(kinds.shape + (3,))
    rgb[:] = MAP_EMPTY
    if filled:
        rgb[kinds == TERRAIN_CLIFF] = CLIFF_FILL
        rgb[kinds == TERRAIN_WATER] = WATER_FILL
    rgb[edge] = TERRAIN_EDGE
    return rgb[::-1]


# -- what the player knows ---------------------------------------------------------------------

def cell_index(x, y, width, height, cell_size):
    """The flat index of the cell under a world position, clamped onto the grid."""
    column = min(width - 1, max(0, int(x // cell_size)))
    row = min(height - 1, max(0, int(y // cell_size)))
    return row * width + column


def balance_sample(frame, sightings):
    unit_values = [0] * SIDE_COUNT
    structure_values = [0] * SIDE_COUNT
    for _, _, side, is_structure, _, cost in sightings:
        (structure_values if is_structure else unit_values)[side] += cost
    return BalanceSample(frame, unit_values, structure_values)


def balance_shares(history, with_structures):
    """The frames that had anything to weigh, and each side's share of the known value on them."""
    frames = []
    shares = []
    for sample in history:
        values = [unit + (structure if with_structures else 0)
                  for unit, structure in zip(sample.unit_values, sample.structure_values)]
        total = sum(values)
        if total:
            frames.append(sample.frame)
            shares.append([value / total for value in values])
    return frames, shares


def composition_rows(sightings, with_structures, limit):
    """(template, count per side) for the `limit` templates seen most, most first."""
    counts = {}
    for _, _, side, _, template, _ in counted(sightings, with_structures):
        counts.setdefault(template, [0] * SIDE_COUNT)[side] += 1
    return sorted(counts.items(), key=lambda item: (-sum(item[1]), item[0]))[:limit]


class PlayerKnowledge(object):
    """What one player knows. An enemy that leaves sight stays at the spot it was last seen until this
    player's vision covers the spot again and finds it gone, so the picture of the enemy is only as
    fresh as the scouting behind it; and once a game second, the value of every side it knows of goes
    into the history. A new match or another player starts it all over."""

    def __init__(self):
        self.player_index = None
        self.frame = -1
        self.last_seen = {}
        self.history = []

    def update(self, player_index, frame, reply):
        """Take one units reply in; the enemies out of sight, as sightings of side SIDE_REMEMBERED."""
        if player_index != self.player_index or frame < self.frame:
            self.last_seen = {}
            self.history = []
        self.player_index = player_index
        self.frame = frame

        width, height, cell_size, seen = reply["width"], reply["height"], reply["cellSize"], reply["seen"]
        in_sight = set()
        for object_id, x, y, side, is_structure, template, cost in reply["units"]:
            if side == SIDE_ENEMY:
                self.last_seen[object_id] = (x, y, is_structure, template, cost)
                in_sight.add(object_id)
        for object_id, (x, y, _, _, _) in list(self.last_seen.items()):
            if object_id not in in_sight and seen[cell_index(x, y, width, height, cell_size)] == "1":
                del self.last_seen[object_id]
        remembered = [[x, y, SIDE_REMEMBERED, is_structure, template, cost]
                      for object_id, (x, y, is_structure, template, cost) in self.last_seen.items()
                      if object_id not in in_sight]

        if not self.history or frame - self.history[-1].frame >= HISTORY_FRAMES:
            self.history.append(balance_sample(frame, sightings_of(reply, remembered)))
        return remembered


# -- the connection ----------------------------------------------------------------------------

def waiting_snapshot(serial, state, message, players=()):
    return Snapshot(serial, state, message, list(players), 0, None, None, [], None, ())


class Feed(object):
    """Polls the game on its own thread, so a game that stops answering while it loads a map never
    freezes the window. The window only ever reads the latest snapshot."""

    def __init__(self, connect, port, player_index):
        self.connect = connect
        self.port = port
        self.player_index = player_index
        self.knowledge = PlayerKnowledge()
        self.terrain = None
        self.snapshot = waiting_snapshot(0, STATE_CONNECTING, "Connecting to 127.0.0.1:%d." % port)
        self.wake = threading.Event()
        self.stopping = False

    def run(self):
        game = None
        serial = 0
        while not self.stopping:
            serial += 1
            try:
                if game is None:
                    game = self.connect(self.port)
                self.snapshot = self.read(game, serial)
                delay = REFRESH_SECONDS
            except (OSError, ControlError) as error:
                if game is not None:
                    game.close()
                game = None
                self.terrain = None
                self.snapshot = waiting_snapshot(serial, STATE_LOST,
                    "Can't reach the game on 127.0.0.1:%d (%s). Start it with -control %d; this "
                    "window retries every %g s." % (self.port, error, self.port, RECONNECT_SECONDS))
                delay = RECONNECT_SECONDS
            self.wake.wait(delay)
            self.wake.clear()
        if game is not None:
            game.close()

    def read(self, game, serial):
        status = game.status()
        if not status["inGame"]:
            self.terrain = None
            return waiting_snapshot(serial, STATE_NO_MATCH,
                                    "No match is running. Start a skirmish; the map appears by itself.")

        player_index = self.player_index
        units = game.send("units %d" % player_index)
        if not units["ok"]:
            return waiting_snapshot(serial, STATE_REFUSED, "The game refused: %s." % units["error"],
                                    status["players"])
        # the ground does not change during a match, so it is read once, and again when a new one starts
        if self.terrain is None or status["frame"] < self.knowledge.frame:
            terrain = game.send("terrain")
            if not terrain["ok"]:
                return waiting_snapshot(serial, STATE_REFUSED, "The game refused: %s." % terrain["error"],
                                        status["players"])
            self.terrain = terrain_kinds(terrain)
        remembered = self.knowledge.update(player_index, status["frame"], units)
        return Snapshot(serial, STATE_LIVE, "", status["players"], status["frame"], player_index, units, remembered,
                        self.terrain, tuple(self.knowledge.history))

    def refresh_now(self):
        self.wake.set()

    def stop(self):
        self.stopping = True
        self.wake.set()


# -- the window --------------------------------------------------------------------------------

def rgb(colour):
    return "rgb(%d, %d, %d)" % colour


def dark_palette():
    palette = QPalette()
    roles = {
        QPalette.Window: BACKGROUND, QPalette.WindowText: TEXT, QPalette.Base: SURFACE,
        QPalette.AlternateBase: SURFACE_RAISED, QPalette.Text: TEXT, QPalette.Button: SURFACE_RAISED,
        QPalette.ButtonText: TEXT, QPalette.ToolTipBase: SURFACE_RAISED, QPalette.ToolTipText: TEXT,
        # the selected row of the player list: a light neutral, so no data colour is borrowed for it
        QPalette.Highlight: TEXT_MUTED, QPalette.HighlightedText: BACKGROUND,
    }
    for role, colour in roles.items():
        palette.setColor(role, QColor(*colour))
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        palette.setColor(QPalette.Disabled, role, QColor(*TEXT_DISABLED))
    return palette


# Fusion outlines a field in the window colour darkened, which on a dark window is no outline at all;
# the edge, the focus ring and the check box are drawn here instead. Focus trades a pixel of padding
# for the second pixel of border, so a focused control keeps its size.
CONTROL_STYLE = """
QComboBox, QDoubleSpinBox, QPushButton {
    background: %(raised)s; color: %(text)s; border: 1px solid %(edge)s; border-radius: 4px; padding: 5px 9px;
}
QComboBox:hover, QDoubleSpinBox:hover, QPushButton:hover { background: %(hover)s; }
QPushButton:pressed { background: %(pressed)s; }
QComboBox:focus, QDoubleSpinBox:focus, QPushButton:focus { border: 2px solid %(text)s; padding: 4px 8px; }
QComboBox:disabled, QDoubleSpinBox:disabled, QPushButton:disabled {
    background: %(surface)s; color: %(disabled)s; border-color: %(hover)s;
}
QCheckBox::indicator {
    width: 14px; height: 14px; border: 1px solid %(edge)s; border-radius: 3px; background: %(surface)s;
}
QCheckBox::indicator:checked { background: %(muted)s; }
QCheckBox::indicator:focus { border: 2px solid %(text)s; width: 12px; height: 12px; }
""" % {"raised": rgb(SURFACE_RAISED), "hover": rgb(SURFACE_HOVER), "pressed": rgb(SURFACE_PRESSED),
       "surface": rgb(SURFACE), "edge": rgb(BORDER_STRONG), "text": rgb(TEXT), "muted": rgb(TEXT_MUTED),
       "disabled": rgb(TEXT_DISABLED)}


def apply_theme(application):
    application.setStyle("Fusion")
    application.setPalette(dark_palette())
    font = application.font()
    font.setPixelSize(TEXT_PIXELS)
    application.setFont(font)
    application.setStyleSheet(CONTROL_STYLE)


def muted_label(text):
    label = QLabel(text)
    label.setStyleSheet("color: %s" % rgb(TEXT_MUTED))
    return label


def captioned(caption, content):
    """A panel's caption over the panel; `content` is a widget or a layout."""
    column = QVBoxLayout()
    column.setSpacing(SPACE_2)
    column.addWidget(muted_label(caption))
    if isinstance(content, QWidget):
        column.addWidget(content, 1)
    else:
        column.addLayout(content, 1)
    return column


def flow_arrow():
    arrow = muted_label("→")
    arrow.setAlignment(Qt.AlignCenter)
    return arrow


def draw_note(painter, area, note):
    painter.setPen(QColor(*TEXT))
    note_area = QRect(area.x() + SPACE_3, area.y() + SPACE_3,
                      min(NOTE_WIDTH, area.width() - 2 * SPACE_3), area.height() - 2 * SPACE_3)
    painter.drawText(note_area, Qt.AlignLeft | Qt.AlignTop | Qt.TextWordWrap, note)


class MapPanel(QWidget):
    """Map-shaped images laid one over another in a frame that keeps the first one's aspect, pinned
    under its caption, with dots on top and a note over it all when there is something to say. Every image covers the
    whole map whatever its resolution, so terrain and influence line up."""

    def __init__(self, name):
        super().__init__()
        self.layers = []
        self.dots = []
        self.note = ""
        self.setMinimumSize(MINIMUM_PANEL_SIZE, MINIMUM_PANEL_SIZE)
        self.setAccessibleName(name)

    def show_layers(self, layers, dots=(), note=""):
        """`layers` is (QImage, QPainter composition mode) bottom first; `dots` is (x, y, colour, size)
        with x and y the fraction of the map's width from the west and of its height from the north."""
        self.layers = layers
        self.dots = dots
        if note != self.note:
            self.note = note
            self.setAccessibleDescription(note)
        self.update()

    def map_rect(self):
        if not self.layers:
            return self.rect()
        image = self.layers[0][0]
        scale = min(self.width() / image.width(), self.height() / image.height())
        return QRect(0, 0, int(image.width() * scale), int(image.height() * scale))

    def paintEvent(self, event):
        painter = QPainter(self)
        frame = self.map_rect()
        painter.fillRect(frame, QColor(*MAP_EMPTY))
        painter.setRenderHint(QPainter.SmoothPixmapTransform)
        for image, mode in self.layers:
            painter.setCompositionMode(mode)
            painter.drawImage(frame, image)
        painter.setCompositionMode(QPainter.CompositionMode_SourceOver)
        for x, y, colour, size in self.dots:
            painter.fillRect(QRectF(frame.x() + x * frame.width() - size / 2, frame.y() + y * frame.height() - size / 2,
                                    size, size), colour)
        painter.setPen(QPen(QColor(*BORDER_STRONG), 1))
        painter.drawRect(frame.adjusted(0, 0, -1, -1))
        if self.note:
            draw_note(painter, frame, self.note)


def match_clock(frame):
    seconds = frame // LOGIC_FRAMES_PER_SECOND
    return "%d:%02d" % (seconds // 60, seconds % 60)


class BalanceChart(QWidget):
    """Each side's share of the value this player knows of, stacked from the bottom in side order and
    drawn over the whole match so far. Blue and green above the middle line is ahead of what has been
    seen of the enemy; the enemy nobody has scouted is not in it."""

    def __init__(self):
        super().__init__()
        self.frames = []
        self.shares = []
        self.setMinimumSize(MINIMUM_PANEL_SIZE, MINIMUM_PANEL_SIZE)
        self.setAccessibleName("Force balance")

    def show_history(self, frames, shares):
        self.frames = frames
        self.shares = shares
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        metrics = self.fontMetrics()
        label_width = metrics.horizontalAdvance("100%")
        text_height = metrics.height()
        plot = QRect(label_width + SPACE_2, text_height // 2, self.width() - label_width - SPACE_2,
                     self.height() - text_height // 2 - text_height - SPACE_1)
        painter.fillRect(plot, QColor(*MAP_EMPTY))

        def height_of(share):
            return plot.bottom() - share * plot.height()

        if len(self.frames) >= 2:
            painter.setRenderHint(QPainter.Antialiasing)
            painter.setPen(Qt.NoPen)
            first = self.frames[0]
            span = max(1, self.frames[-1] - first)
            xs = [plot.left() + (frame - first) / span * plot.width() for frame in self.frames]
            bottom = [0.0] * len(xs)
            for side, colour in enumerate(SIDE_COLOURS):
                top = [below + share[side] for below, share in zip(bottom, self.shares)]
                outline = [QPointF(x, height_of(share)) for x, share in zip(xs, top)]
                outline += [QPointF(x, height_of(share)) for x, share in reversed(list(zip(xs, bottom)))]
                painter.setBrush(QColor(*colour))
                painter.drawPolygon(QPolygonF(outline))
                bottom = top
            painter.setRenderHint(QPainter.Antialiasing, False)
            painter.setPen(QPen(QColor(*TEXT), 1, Qt.DashLine))
            painter.drawLine(plot.left(), int(height_of(0.5)), plot.right(), int(height_of(0.5)))
        else:
            draw_note(painter, plot, "Builds up as the match runs, one sample a game second.")

        painter.setPen(QPen(QColor(*BORDER_STRONG), 1))
        painter.setBrush(Qt.NoBrush)
        painter.drawRect(plot.adjusted(0, 0, -1, -1))
        painter.setPen(QColor(*TEXT_MUTED))
        for share, text in ((1.0, "100%"), (0.5, "50%"), (0.0, "0%")):
            painter.drawText(QRect(0, int(height_of(share)) - text_height // 2, label_width, text_height),
                             Qt.AlignRight | Qt.AlignVCenter, text)
        if self.frames:
            axis = QRect(plot.left(), plot.bottom() + SPACE_1, plot.width(), text_height)
            painter.drawText(axis, Qt.AlignLeft, match_clock(self.frames[0]))
            painter.drawText(axis, Qt.AlignRight, match_clock(self.frames[-1]))


class CompositionGrid(QWidget):
    """One row per template, one column per side, a dot sized by how many of it that side has."""

    def __init__(self):
        super().__init__()
        self.rows = []
        self.setMinimumSize(MINIMUM_PANEL_SIZE, MINIMUM_PANEL_SIZE)
        self.setAccessibleName("Composition")

    def show_rows(self, rows):
        self.rows = rows
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor(*MAP_EMPTY))
        painter.setPen(QPen(QColor(*BORDER_STRONG), 1))
        painter.drawRect(self.rect().adjusted(0, 0, -1, -1))
        if not self.rows:
            draw_note(painter, self.rect(), "Nothing counted yet.")
            return

        metrics = self.fontMetrics()
        row_height = metrics.height() + SPACE_2
        column_width = min(metrics.horizontalAdvance(max(SIDE_COLUMN_NAMES, key=len)) + SPACE_3,
                           int(self.width() * SIDE_COLUMNS_SHARE / SIDE_COUNT))
        names_width = self.width() - SIDE_COUNT * column_width - SPACE_2
        largest = max(max(counts) for _, counts in self.rows)
        painter.setRenderHint(QPainter.Antialiasing)

        painter.setPen(QColor(*TEXT_MUTED))
        for side, name in enumerate(SIDE_COLUMN_NAMES):
            painter.drawText(QRect(names_width + side * column_width, SPACE_1, column_width, row_height),
                             Qt.AlignCenter, metrics.elidedText(name, Qt.ElideRight, column_width))

        visible = max(0, (self.height() - row_height - SPACE_1) // row_height)
        for row, (template, counts) in enumerate(self.rows[:visible]):
            top = SPACE_1 + (row + 1) * row_height
            painter.setPen(QColor(*TEXT))
            painter.drawText(QRect(SPACE_2, top, names_width - SPACE_2, row_height), Qt.AlignLeft | Qt.AlignVCenter,
                             metrics.elidedText(template, Qt.ElideRight, names_width - SPACE_2))
            for side, count in enumerate(counts):
                centre = QPointF(names_width + (side + 0.5) * column_width, top + row_height / 2)
                painter.setPen(Qt.NoPen)
                if count:
                    radius = (row_height / 2 - 1) * math.sqrt(count / largest)
                    painter.setBrush(QColor(*SIDE_COLOURS[side]))
                else:
                    radius = EMPTY_DOT_RADIUS
                    painter.setBrush(QColor(*SURFACE_HOVER))
                painter.drawEllipse(centre, radius, radius)


def labelled(text, field):
    """A label and its field, the label wired as the field's buddy so its Alt shortcut focuses it."""
    label = QLabel(text)
    label.setBuddy(field)
    row = QHBoxLayout()
    row.setSpacing(SPACE_2)
    row.addWidget(label)
    row.addWidget(field)
    return row


class ViewerWindow(QMainWindow):

    def __init__(self, feed, clock=time.monotonic):
        super().__init__()
        self.feed = feed
        self.clock = clock
        self.kernel = falloff_kernel(DEFAULT_FALLOFF_RATE)
        self.with_structures = False
        self.target_key = None
        self.target = None
        self.shown = None
        self.note = ""
        self.players = None
        self.terrain = None
        self.outline_image = None
        self.terrain_image = None
        self.last_tick = clock()

        self.setWindowTitle("What the player knows")
        self.resize(*WINDOW_SIZE)
        # no minimum width of its own: the controls row's width is the floor, so it is never squeezed
        self.setMinimumHeight(MINIMUM_WINDOW_HEIGHT)

        self.player_box = QComboBox()
        self.player_box.setAccessibleName("Player")
        self.player_box.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        self.player_box.setMinimumWidth(
            self.player_box.fontMetrics().horizontalAdvance("0" * PLAYER_TEXT_LENGTH) + FIELD_CHROME_WIDTH)
        self.player_box.currentIndexChanged.connect(self.choose_player)

        self.falloff_box = QDoubleSpinBox()
        self.falloff_box.setAccessibleName("Falloff per cell")
        self.falloff_box.setRange(FALLOFF_LOWEST, FALLOFF_HIGHEST)
        self.falloff_box.setSingleStep(FALLOFF_STEP)
        self.falloff_box.setDecimals(2)
        self.falloff_box.setValue(DEFAULT_FALLOFF_RATE)
        self.falloff_box.setSuffix(" per cell")
        # a styled spin box loses its arrow glyphs and the step buttons ate the text; the arrow keys
        # and the mouse wheel step it just the same
        self.falloff_box.setButtonSymbols(QAbstractSpinBox.NoButtons)
        self.falloff_box.setToolTip("Up and down arrow keys or the mouse wheel change it")
        self.falloff_box.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        self.falloff_box.setMinimumWidth(
            self.falloff_box.fontMetrics().horizontalAdvance("0.95 per cell") + FIELD_CHROME_WIDTH)
        self.falloff_box.valueChanged.connect(self.change_falloff)

        self.structures_box = QCheckBox("Count &structures")
        self.structures_box.toggled.connect(self.toggle_structures)

        self.reconnect_button = QPushButton("&Reconnect now")
        self.reconnect_button.clicked.connect(self.feed.refresh_now)

        controls = QHBoxLayout()
        controls.setSpacing(SPACE_3)
        controls.addLayout(labelled("&Player", self.player_box))
        controls.addLayout(labelled("&Falloff", self.falloff_box))
        controls.addWidget(self.structures_box)
        controls.addStretch(1)
        controls.addWidget(self.reconnect_button)

        self.status = muted_label("")

        self.observation_map = MapPanel("Terrain and units")
        self.side_maps = [MapPanel("%s influence" % name) for name in SIDE_NAMES]
        side_grid = QGridLayout()
        side_grid.setSpacing(SPACE_2)
        for side, side_map in enumerate(self.side_maps):
            side_grid.addWidget(side_map, side // 2, side % 2)
        self.map = MapPanel("Influence over terrain")

        pipeline = QHBoxLayout()
        pipeline.setSpacing(SPACE_2)
        pipeline.addLayout(captioned("Terrain and units", self.observation_map), 2)
        pipeline.addWidget(flow_arrow())
        pipeline.addLayout(captioned("Influence by side", side_grid), 2)
        pipeline.addWidget(flow_arrow())
        pipeline.addLayout(captioned("Influence over terrain", self.map), 3)

        self.balance = BalanceChart()
        self.composition = CompositionGrid()
        lower = QHBoxLayout()
        lower.setSpacing(SPACE_3)
        lower.addLayout(captioned("Force balance, share of known value", self.balance), 1)
        lower.addLayout(captioned("Composition", self.composition), 1)

        legend = QHBoxLayout()
        legend.setSpacing(SPACE_3)
        self.legend_labels = []
        for colour, name in zip(SIDE_COLOURS, SIDE_NAMES):
            swatch = QFrame()
            swatch.setFixedSize(SWATCH_SIZE, SWATCH_SIZE)
            swatch.setStyleSheet("background-color: %s" % rgb(colour))
            label = muted_label(name)
            item = QHBoxLayout()
            item.setSpacing(SPACE_1)
            item.addWidget(swatch)
            item.addWidget(label)
            legend.addLayout(item)
            self.legend_labels.append(label)
        legend.addStretch(1)

        column = QVBoxLayout()
        column.setContentsMargins(SPACE_3, SPACE_3, SPACE_3, SPACE_3)
        column.setSpacing(0)
        column.addLayout(controls)
        column.addSpacing(SPACE_3)
        column.addWidget(self.status)
        column.addSpacing(SPACE_2)
        column.addLayout(legend)
        column.addSpacing(SPACE_3)
        column.addLayout(pipeline, 3)
        column.addSpacing(SPACE_3)
        column.addLayout(lower, 2)
        body = QWidget()
        body.setLayout(column)
        self.setCentralWidget(body)

        QShortcut(QKeySequence(Qt.Key_Escape), self, self.close)
        self.timer = QTimer(self)
        self.timer.setTimerType(Qt.PreciseTimer)
        self.timer.timeout.connect(self.tick)
        self.timer.start(FRAME_MILLISECONDS)
        self.tick()

    # -- actions --

    def choose_player(self, row):
        self.feed.player_index = self.player_box.itemData(row)
        self.feed.refresh_now()

    def change_falloff(self, rate):
        self.kernel = falloff_kernel(rate)

    def toggle_structures(self, checked):
        self.with_structures = checked

    # -- view --

    def tick(self):
        """Once a screen frame: take a new reading in if one arrived, then ease the influence toward it."""
        now = self.clock()
        elapsed = now - self.last_tick
        self.last_tick = now
        self.take_reading()
        if self.target is None:
            return
        if self.shown is None or self.shown.shape != self.target.shape:
            self.shown = self.target.copy()
        elif numpy.abs(self.target - self.shown).max() < SETTLED_LEVEL:
            return
        else:
            self.shown = ease(self.shown, self.target, elapsed)
        self.draw_influence()

    def draw_influence(self):
        over = QPainter.CompositionMode_SourceOver
        add = QPainter.CompositionMode_Plus
        combined = to_image(density_rgb(self.shown, SIDE_COLOURS))
        self.map.show_layers([(self.terrain_image, over), (combined, add)], note=self.note)
        for side, side_map in enumerate(self.side_maps):
            alone = to_image(density_rgb(self.shown[side:side + 1], SIDE_COLOURS[side:side + 1]))
            side_map.show_layers([(self.outline_image, over), (alone, add)])

    def take_reading(self):
        snapshot = self.feed.snapshot
        self.reconnect_button.setEnabled(snapshot.state == STATE_LOST)
        self.sync_players(snapshot.players)

        key = (snapshot.serial, self.falloff_box.value(), self.with_structures)
        if key == self.target_key:
            return
        self.target_key = key

        if snapshot.state != STATE_LIVE:
            self.status.setText(self.status_text(snapshot))
            self.target = None
            self.shown = None
            self.map.show_layers([], note=snapshot.message)
            self.observation_map.show_layers([])
            for side_map in self.side_maps:
                side_map.show_layers([])
            self.balance.show_history([], [])
            self.composition.show_rows([])
            self.show_counts(None)
            return

        if snapshot.terrain is not self.terrain:
            self.terrain = snapshot.terrain
            self.outline_image = to_image(terrain_rgb(snapshot.terrain, False))
            self.terrain_image = to_image(terrain_rgb(snapshot.terrain, True))

        picture = density_picture(snapshot.units, snapshot.remembered, self.kernel, self.with_structures)
        self.target = picture.strength
        self.note = picture.empty_message
        self.status.setText(SEPARATOR.join((self.status_text(snapshot), "peak %.1f" % picture.peak)))
        self.show_counts(picture.counts)

        sightings = sightings_of(snapshot.units, snapshot.remembered)
        self.observation_map.show_layers([(self.outline_image, QPainter.CompositionMode_SourceOver)],
                                         self.unit_dots(snapshot.units, sightings))
        self.balance.show_history(*balance_shares(snapshot.history, self.with_structures))
        self.composition.show_rows(composition_rows(sightings, self.with_structures, COMPOSITION_ROW_LIMIT))

    def unit_dots(self, reply, sightings):
        """Every sighting, structures too, as a dot where it stands; the last seen go under the rest."""
        map_width = reply["width"] * reply["cellSize"]
        map_height = reply["height"] * reply["cellSize"]
        colours = [QColor(*colour) for colour in SIDE_COLOURS]
        ordered = sorted(sightings, key=lambda sighting: sighting[2] != SIDE_REMEMBERED)
        return [(x / map_width, 1.0 - y / map_height, colours[side],
                 STRUCTURE_DOT_PIXELS if is_structure else UNIT_DOT_PIXELS)
                for x, y, side, is_structure, _, _ in ordered]

    def show_counts(self, counts):
        for index, (label, name) in enumerate(zip(self.legend_labels, SIDE_NAMES)):
            label.setText(name if counts is None else "%s %d" % (name, counts[index]))

    def sync_players(self, players):
        """Rebuild the player list only when the game's list changes, so an open dropdown stays open.
        Neutral and civilian have no seat and nothing to show, so only seated players are listed."""
        seated = [(player["index"], player["slot"]) for player in players if player["slot"] >= 0]
        if seated == self.players:
            return
        self.players = seated
        self.player_box.blockSignals(True)
        self.player_box.clear()
        for index, slot in seated:
            self.player_box.addItem("%d  (slot %d)" % (index, slot), index)
        row = self.player_box.findData(self.feed.player_index)
        self.player_box.setCurrentIndex(max(0, row))
        self.player_box.blockSignals(False)
        self.player_box.setEnabled(bool(seated))
        if seated and row < 0:
            self.choose_player(0)

    def status_text(self, snapshot):
        if snapshot.state == STATE_LIVE:
            return SEPARATOR.join(("Live", "frame %d" % snapshot.frame,
                                   "units and structures" if self.with_structures else "units only"))
        return {
            STATE_CONNECTING: "Connecting to 127.0.0.1:%d" % self.feed.port,
            STATE_LOST: "Disconnected",
            STATE_NO_MATCH: "Connected, no match running",
            STATE_REFUSED: "Connected, request refused",
        }[snapshot.state]


# -- entry -------------------------------------------------------------------------------------

def check():
    kernel = falloff_kernel(0.5)
    centre = kernel.shape[0] // 2
    assert kernel[centre, centre] == 1.0
    assert kernel[centre, centre + 1] == 0.5
    assert kernel[kernel > 0].min() >= KERNEL_FLOOR

    step = numpy.array([[0.0, 0.0, 0.0], [0.0, 1.0, 0.5], [0.0, 0.0, 0.0]])
    sightings = [[45, 45, SIDE_OWN, 0, "Tank", 800], [50, 50, SIDE_OWN, 0, "Tank", 800],
                 [45, 45, SIDE_ENEMY, 1, "Barracks", 500], [9999, -5, SIDE_ALLY, 0, "Ranger", 225]]
    layers = density_layers(counted(sightings, False), 3, 3, 40, step)
    assert layers.shape == (SIDE_COUNT, 3, 3)
    assert layers[SIDE_OWN].tolist() == [[0, 0, 0], [0, 2.0, 1.0], [0, 0, 0]]
    assert layers[SIDE_ENEMY].sum() == 0
    assert layers[SIDE_ALLY][0, 2] == 1.0          # off the map, pinned to the nearest edge cell
    assert density_layers(counted(sightings, True), 3, 3, 40, step)[SIDE_ENEMY][1, 1] == 1.0

    # north up: the top row of the picture is the highest row of cells, on black
    grid = numpy.zeros((SIDE_COUNT, 2, 2))
    grid[SIDE_OWN, 1, 0] = 1.0
    picture = density_rgb(grid, SIDE_COLOURS)
    assert picture[0, 0].tolist() == list(SIDE_COLOURS[SIDE_OWN])
    assert picture[1, 0].tolist() == [0, 0, 0]

    reply = {"width": 2, "height": 1, "cellSize": 40, "units": [[9, 45, 5, SIDE_ALLY, 0, "Ranger", 225]]}
    remembered = [[5, 5, SIDE_REMEMBERED, 1, "Barracks", 500]]
    assert density_picture(reply, remembered, step, False).counts == [0, 1, 0, 0]
    assert density_picture(reply, remembered, step, True).counts == [0, 1, 0, 1]
    assert density_picture(reply, remembered, step, True).strength.max() == 1.0

    # easing covers the gap a little at a time and never overshoots it
    shown = ease(numpy.zeros(1), numpy.full(1, 100.0), EASE_SECONDS)
    assert 60 < shown[0] < 65
    assert ease(shown, numpy.full(1, 100.0), 10.0)[0] <= 100.0

    assert cell_index(-10, 99999, 3, 2, 40) == 3

    # a cliff sample in the south-west corner: outlined where it meets the ground, shaded when filled
    kinds = terrain_kinds({"width": 3, "height": 2, "cells": "100000"})
    assert kinds.tolist() == [[1, 0, 0], [0, 0, 0]]
    outline = terrain_rgb(kinds, False)
    assert outline[1, 0].tolist() == list(TERRAIN_EDGE)
    assert outline[1, 2].tolist() == list(MAP_EMPTY)
    assert outline[0, 0].tolist() == list(MAP_EMPTY)
    filled = terrain_rgb(terrain_kinds({"width": 3, "height": 2, "cells": "222222"}), True)
    assert filled[0, 0].tolist() == list(WATER_FILL)

    # value is weighed per side, structures only when asked, and a frame with nothing known is left out
    history = [balance_sample(0, []), balance_sample(30, sightings)]
    assert balance_shares(history, False) == ([30], [[1600 / 1825, 225 / 1825, 0.0, 0.0]])
    assert balance_shares(history, True)[1][0][SIDE_ENEMY] == 500 / 2325

    assert composition_rows(sightings, True, 2) == [("Tank", [2, 0, 0, 0]), ("Barracks", [0, 0, 1, 0])]
    assert composition_rows(sightings, False, 5) == [("Tank", [2, 0, 0, 0]), ("Ranger", [0, 1, 0, 0])]

    # an enemy that walks into the fog is remembered where it was, until the spot is seen empty
    knowledge = PlayerKnowledge()
    in_view = {"width": 2, "height": 2, "cellSize": 40, "seen": "1000",
               "units": [[7, 10, 10, SIDE_ENEMY, 0, "Tank", 800], [8, 50, 50, SIDE_OWN, 0, "Ranger", 225]]}
    gone = [[10, 10, SIDE_REMEMBERED, 0, "Tank", 800]]
    assert knowledge.update(2, 100, in_view) == []
    assert knowledge.update(2, 130, dict(in_view, units=[], seen="0000")) == gone
    assert knowledge.update(2, 160, dict(in_view, units=[], seen="0001")) == gone
    assert knowledge.update(2, 190, dict(in_view, units=[], seen="1000")) == []
    assert [sample.frame for sample in knowledge.history] == [100, 130, 160, 190]
    assert knowledge.history[1].unit_values == [0, 0, 0, 800]
    knowledge.update(2, 200, in_view)
    assert len(knowledge.history) == 4             # under a game second since the last sample
    assert knowledge.update(2, 10, dict(in_view, units=[], seen="0000")) == []
    assert len(knowledge.history) == 1
    knowledge.update(2, 20, in_view)
    assert knowledge.update(3, 30, dict(in_view, units=[], seen="0000")) == []


def main(argv):
    if argv[1:] == ["check"]:
        check()
        print("ok")
        return 0

    port = int(argv[1]) if len(argv) > 1 else DEFAULT_PORT
    player_index = int(argv[2]) if len(argv) > 2 else DEFAULT_PLAYER_INDEX

    application = QApplication(argv)
    apply_theme(application)

    feed = Feed(lambda port: Control(port, timeout=SOCKET_TIMEOUT_SECONDS), port, player_index)
    poller = threading.Thread(target=feed.run, daemon=True)
    poller.start()
    window = ViewerWindow(feed)
    window.show()
    try:
        return application.exec_()
    finally:
        feed.stop()
        poller.join()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
