#!/usr/bin/env python3
"""Score a generated .map for fairness, without launching the game.

The generator writes an uncompressed CkMp stream, which is the same chunk format WorldBuilder
saves, so everything this needs is in the file: the height field, the tile indices that say which
texture class a cell got, the start waypoints, the supply docks and the water areas.

What comes out is one row per map:

    cells   the playable size
    players how many start positions
    build   buildable cells within a base radius of a start: worst player / best player
    supply  cells from a start to its nearest supply dock: worst / best
    path    walking distance between the two closest starts, and between the two furthest
    choke   how many separate crossings the flood fill has to squeeze through
    money   docks and derricks nearest to a player, counted by walking distance: worst / best

Nothing about the map is mirrored or turned round any more - the terrain is one noise field and the
starts are found in it - so fairness is a spread rather than an identity. What these columns catch
is the seed where one player opens with half the ground or twice the money of another.

    python mapscore.py <map file or directory> [...]
"""

import argparse
import math
import os
import struct
import sys
from collections import deque

MAP_XY_FACTOR = 10.0
MAP_HEIGHT_SCALE = MAP_XY_FACTOR / 16.0
CLIFF_WORLD_SPAN = 9.8
BASE_RADIUS_CELLS = 14


class ChunkReader:
    """Just enough of DataChunkInput to walk the chunks this tool cares about."""

    def __init__(self, data):
        if data[:4] != b"CkMp":
            raise ValueError("not an uncompressed CkMp map: %r" % data[:4])

        self.data = data
        self.pos = 4
        self.names = {}

        count = self.read_int()
        for _ in range(count):
            length = self.read_byte()
            name = self.data[self.pos:self.pos + length].decode("latin-1")
            self.pos += length
            self.names[self.read_uint()] = name

    def read_byte(self):
        value = self.data[self.pos]
        self.pos += 1
        return value

    def read_int(self):
        value = struct.unpack_from("<i", self.data, self.pos)[0]
        self.pos += 4
        return value

    def read_uint(self):
        value = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return value

    def read_short(self):
        value = struct.unpack_from("<h", self.data, self.pos)[0]
        self.pos += 2
        return value

    def read_real(self):
        value = struct.unpack_from("<f", self.data, self.pos)[0]
        self.pos += 4
        return value

    def read_ascii(self):
        length = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        text = self.data[self.pos:self.pos + length].decode("latin-1")
        self.pos += length
        return text

    def read_dict(self):
        pairs = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2

        entries = {}
        for _ in range(pairs):
            key_and_type = self.read_uint()
            key = self.names.get(key_and_type >> 8, "?")
            data_type = key_and_type & 0xFF

            if data_type == 0:                      # bool
                entries[key] = self.read_byte() != 0
            elif data_type == 1:                    # int
                entries[key] = self.read_int()
            elif data_type == 2:                    # real
                entries[key] = self.read_real()
            elif data_type == 3:                    # ascii
                entries[key] = self.read_ascii()
            elif data_type == 4:                    # unicode
                length = struct.unpack_from("<H", self.data, self.pos)[0]
                self.pos += 2
                self.pos += length * 2
                entries[key] = ""
            else:
                raise ValueError("unknown dict type %d for key %s" % (data_type, key))

        return entries

    def chunks(self, end=None):
        """Yield (name, version, body end offset) for each chunk at this level."""
        if end is None:
            end = len(self.data)

        # id, version, size: four bytes, two bytes, four bytes (DataChunkVersionType is a short)
        while self.pos + 10 <= end:
            chunk_id = self.read_uint()
            version = struct.unpack_from("<H", self.data, self.pos)[0]
            self.pos += 2
            size = self.read_int()
            body_end = self.pos + size

            yield self.names.get(chunk_id, "?"), version, body_end
            self.pos = body_end


class GeneratedMap:
    def __init__(self, path):
        with open(path, "rb") as handle:
            data = handle.read()

        self.path = path
        self.width = 0
        self.height = 0
        self.border = 0
        self.playable = 0
        self.heights = []
        self.tiles = []
        self.starts = []
        self.supplies = []
        self.derricks = []
        self.water_areas = []
        self.objects = []                           # every object, as (template, cell x, cell y)

        reader = ChunkReader(data)
        for name, version, body_end in reader.chunks():
            if name == "HeightMapData":
                self._read_heights(reader)
            elif name == "BlendTileData":
                self._read_tiles(reader)
            elif name == "ObjectsList":
                self._read_objects(reader, body_end)
            elif name == "PolygonTriggers":
                self._read_water(reader)

    def _read_heights(self, reader):
        self.width = reader.read_int()
        self.height = reader.read_int()
        self.border = reader.read_int()

        for _ in range(reader.read_int()):
            self.playable = reader.read_int()
            reader.read_int()

        size = reader.read_int()
        self.heights = list(reader.data[reader.pos:reader.pos + size])
        reader.pos += size

    def _read_tiles(self, reader):
        size = reader.read_int()
        self.tiles = list(struct.unpack_from("<%dh" % size, reader.data, reader.pos))
        reader.pos += size * 2

    def _read_objects(self, reader, end):
        for name, version, body_end in reader.chunks(end):
            if name != "Object":
                continue

            x = reader.read_real()
            y = reader.read_real()
            reader.read_real()                      # z
            reader.read_real()                      # angle
            reader.read_int()                       # flags
            template = reader.read_ascii()
            entries = reader.read_dict()

            self.objects.append((template, x / MAP_XY_FACTOR, y / MAP_XY_FACTOR))

            if "waypointID" in entries and entries.get("waypointName", "").endswith("_Start"):
                self.starts.append((x / MAP_XY_FACTOR, y / MAP_XY_FACTOR))
            elif template == "SupplyDock":
                self.supplies.append((x / MAP_XY_FACTOR, y / MAP_XY_FACTOR))
            elif template == "TechOilDerrick":
                self.derricks.append((x / MAP_XY_FACTOR, y / MAP_XY_FACTOR))

    def _read_water(self, reader):
        for _ in range(reader.read_int()):
            reader.read_ascii()                     # name
            reader.read_ascii()                     # layer
            reader.read_int()                       # id
            reader.read_byte()                      # is water
            reader.read_byte()                      # is river
            reader.read_int()                       # river start

            points = []
            for _ in range(reader.read_int()):
                points.append((reader.read_int() / MAP_XY_FACTOR,
                               reader.read_int() / MAP_XY_FACTOR,
                               reader.read_int()))
            self.water_areas.append(points)

    def height_at(self, x, y):
        return self.heights[y * self.width + x]

    def cell_span_world(self, x, y):
        corners = (self.height_at(x, y), self.height_at(x + 1, y),
                   self.height_at(x, y + 1), self.height_at(x + 1, y + 1))
        return (max(corners) - min(corners)) * MAP_HEIGHT_SCALE

    def passable_grid(self):
        """Cliff cells only. Water sits inside a polygon and the terrain under it is a basin, so
        the depth check below catches it without carrying the polygon test around."""
        water_level = None
        if self.water_areas:
            water_level = max(point[2] for point in self.water_areas[0]) / MAP_HEIGHT_SCALE

        grid = bytearray(self.width * self.height)
        for y in range(self.height - 1):
            for x in range(self.width - 1):
                walkable = self.cell_span_world(x, y) <= CLIFF_WORLD_SPAN
                if walkable and water_level is not None:
                    walkable = self.height_at(x, y) >= water_level
                grid[y * self.width + x] = 1 if walkable else 0

        return grid

    def cell_of(self, position):
        return (int(position[0] + 0.5) + self.border, int(position[1] + 0.5) + self.border)

    def walk_distances(self, grid, origin):
        """Cell counts from one start over the walkable grid, by breadth first search."""
        distances = [-1] * (self.width * self.height)
        start = self.cell_of(origin)
        index = start[1] * self.width + start[0]

        distances[index] = 0
        queue = deque([index])

        while queue:
            index = queue.popleft()
            x = index % self.width
            y = index // self.width

            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if nx < 0 or ny < 0 or nx >= self.width - 1 or ny >= self.height - 1:
                    continue

                next_index = ny * self.width + nx
                if distances[next_index] >= 0 or not grid[next_index]:
                    continue

                distances[next_index] = distances[index] + 1
                queue.append(next_index)

        return distances

    def buildable_cells(self, grid, origin):
        centre = self.cell_of(origin)
        count = 0

        for dy in range(-BASE_RADIUS_CELLS, BASE_RADIUS_CELLS + 1):
            for dx in range(-BASE_RADIUS_CELLS, BASE_RADIUS_CELLS + 1):
                if dx * dx + dy * dy > BASE_RADIUS_CELLS * BASE_RADIUS_CELLS:
                    continue

                x, y = centre[0] + dx, centre[1] + dy
                if 0 <= x < self.width - 1 and 0 <= y < self.height - 1 and grid[y * self.width + x]:
                    count += 1

        return count

    def money_per_player(self, walked):
        """How many docks and derricks each player is the closest to, by walking distance. A
        player who is nearest to nothing is a player mining his own supply pile all game."""
        counts = [0] * len(self.starts)

        for position in list(self.supplies) + list(self.derricks):
            cell = self.cell_of(position)
            index = cell[1] * self.width + cell[0]

            owner, best = None, None
            for player, distances in enumerate(walked):
                distance = distances[index]
                if distance >= 0 and (best is None or distance < best):
                    owner, best = player, distance

            if owner is not None:
                counts[owner] += 1

        return counts

    def tightest_ring(self, grid):
        """Walk a circle around the middle of the map at a spread of radii and find the one that is
        blocked the most. What comes back is how much of that circle a unit can stand on, and how
        many separate gaps that walkable part is broken into - the crossings a player going round
        the map has to pick between at the tightest point."""
        if not self.starts:
            return 1.0, 0

        centre = self.playable * 0.5
        samples = 720
        tightest = 1.0
        crossings = 0

        for step in range(10, 49):
            radius = self.playable * step / 100.0

            walkable = []
            for i in range(samples):
                angle = 2.0 * math.pi * i / samples
                x = int(centre + radius * math.cos(angle) + 0.5) + self.border
                y = int(centre + radius * math.sin(angle) + 0.5) + self.border
                inside = 0 <= x < self.width - 1 and 0 <= y < self.height - 1
                walkable.append(bool(inside and grid[y * self.width + x]))

            fraction = sum(1 for cell in walkable if cell) / float(samples)
            if fraction >= tightest:
                continue

            tightest = fraction
            crossings = sum(1 for i in range(samples) if walkable[i] and not walkable[i - 1])

        return tightest, crossings


def score(path):
    generated = GeneratedMap(path)
    grid = generated.passable_grid()

    build = [generated.buildable_cells(grid, start) for start in generated.starts]

    supply_distance = []
    pair_distances = []
    walked = []
    for index, start in enumerate(generated.starts):
        distances = generated.walk_distances(grid, start)
        walked.append(distances)

        nearest_supply = None
        for supply in generated.supplies:
            cell = generated.cell_of(supply)
            steps = distances[cell[1] * generated.width + cell[0]]
            if steps >= 0 and (nearest_supply is None or steps < nearest_supply):
                nearest_supply = steps
        supply_distance.append(nearest_supply if nearest_supply is not None else -1)

        for other in range(index + 1, len(generated.starts)):
            cell = generated.cell_of(generated.starts[other])
            pair_distances.append(distances[cell[1] * generated.width + cell[0]])

    reachable = [d for d in pair_distances if d >= 0]
    open_fraction, crossings = generated.tightest_ring(grid)
    money = generated.money_per_player(walked)

    return {
        "name": os.path.basename(path),
        "cells": generated.playable,
        "players": len(generated.starts),
        "build_worst": min(build) if build else 0,
        "build_best": max(build) if build else 0,
        "supply_worst": max(supply_distance) if supply_distance else -1,
        "supply_best": min(supply_distance) if supply_distance else -1,
        "path_near": min(reachable) if reachable else -1,
        "path_far": max(reachable) if reachable else -1,
        "unreachable": len(pair_distances) - len(reachable),
        "chokes": crossings,
        "open": open_fraction,
        "water": len(generated.water_areas),
        "derricks": len(generated.derricks),
        "money_worst": min(money) if money else 0,
        "money_best": max(money) if money else 0,
    }


def collect(paths):
    files = []
    for path in paths:
        if os.path.isdir(path):
            for root, _, names in os.walk(path):
                files.extend(os.path.join(root, name) for name in names if name.endswith(".map"))
        else:
            files.append(path)
    return sorted(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help=".map files, or directories holding them")
    arguments = parser.parse_args()

    files = collect(arguments.paths)
    if not files:
        print("no .map files found", file=sys.stderr)
        return 1

    header = ("map", "cells", "plr", "build w/b", "supply w/b", "path near/far",
              "cross", "open", "water", "money w/b")
    print("%-34s %5s %4s %12s %12s %14s %6s %6s %6s %10s" % header)

    widest_money_gap = 0
    for path in files:
        row = score(path)
        widest_money_gap = max(widest_money_gap, row["money_best"] - row["money_worst"])

        print("%-34s %5d %4d %12s %12s %14s %6d %5d%% %6d %10s" % (
            row["name"][:34], row["cells"], row["players"],
            "%d/%d" % (row["build_worst"], row["build_best"]),
            "%d/%d" % (row["supply_worst"], row["supply_best"]),
            "%d/%d" % (row["path_near"], row["path_far"]),
            row["chokes"], int(row["open"] * 100 + 0.5), row["water"],
            "%d/%d" % (row["money_worst"], row["money_best"])))

        if row["unreachable"]:
            print("    %d start pairs cannot walk to each other" % row["unreachable"])

    print()
    print("widest money gap over %d map(s): %d sites" % (len(files), widest_money_gap))
    return 0


if __name__ == "__main__":
    sys.exit(main())
