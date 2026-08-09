#!/usr/bin/env python3
"""Compose the game's levels as ASCII grids.

Hand-drawing a 52x30 map is a reliable way to produce an off-by-one wall, so
levels are assembled from rectangles here and then validated: every walkable
cell must be reachable from the player start, and every required spawn must
survive. A disconnected ward is unshippable, and the check is cheap.

Two construction rules make adjacency work out:
  * A room carved at rows [y0, y1] owns a wall ring at y0-1 and y1+1. To connect
    it to a corridor, the ring row must *be* the corridor's ring row — then a
    single door in that shared row joins them.
  * Spawns are applied after all geometry, because a later room's wall ring
    would otherwise overwrite an earlier spawn letter.

Legend written to disk:
  '#' tiled wall    '%' plaster wall   '=' concrete wall
  '.' floor         'D' doorway        ' ' void
  letters           tagged spawns (walkable floor)

Run: python3 tools/gen_levels.py
Out: android/app/src/main/assets/level/*.txt
"""
import os
import sys
from collections import deque

OUT = os.path.join(os.path.dirname(__file__), "..", "android", "app", "src",
                   "main", "assets", "level")

VOID, FLOOR, DOOR = " ", ".", "D"
WALKABLE = set(".D")
WALLS = set("#%=")


class Grid:
    def __init__(self, w, h, wall="#"):
        self.w, self.h = w, h
        self.wall = wall
        self.g = [[VOID] * w for _ in range(h)]
        self.pending = []          # spawns, applied after all geometry

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.g[y][x] = c

    def get(self, x, y):
        return self.g[y][x] if 0 <= x < self.w and 0 <= y < self.h else VOID

    def room(self, x0, y0, x1, y1, wall=None):
        """Carve floor and ring it with wall, never overwriting existing
        walkable cells (so shared walls and doors survive)."""
        wall = wall or self.wall
        for y in range(y0 - 1, y1 + 2):
            for x in range(x0 - 1, x1 + 2):
                if x0 <= x <= x1 and y0 <= y <= y1:
                    self.set(x, y, FLOOR)
                elif self.get(x, y) not in WALKABLE:
                    self.set(x, y, wall)

    corridor = room

    def door(self, x, y):
        self.set(x, y, DOOR)

    def spawn(self, x, y, tag):
        self.pending.append((x, y, tag))

    def finish(self):
        for x, y, tag in self.pending:
            if self.get(x, y) in WALKABLE:
                self.set(x, y, tag)
            else:
                print(f"    warn: spawn '{tag}' at {x},{y} is not on floor")
        return self

    def text(self):
        return "\n".join("".join(r) for r in self.g) + "\n"


def validate(grid, name, required=""):
    start = None
    tags = {}
    walk = set()
    for y in range(grid.h):
        for x in range(grid.w):
            c = grid.get(x, y)
            if c in WALKABLE:
                walk.add((x, y))
            elif c not in WALLS and c != VOID:
                tags.setdefault(c, []).append((x, y))
                walk.add((x, y))
                if c == "P":
                    start = (x, y)

    problems = [f"missing spawn '{t}'" for t in required if t not in tags]
    if start is None:
        problems.append("no player spawn 'P'")
    if problems:
        print(f"  {name}: FAIL — " + "; ".join(problems))
        return False

    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            p = (x + dx, y + dy)
            if p in walk and p not in seen:
                seen.add(p)
                q.append(p)

    if len(seen) != len(walk):
        stranded = sorted(walk - seen)[:6]
        print(f"  {name}: FAIL — {len(walk) - len(seen)} unreachable cells "
              f"e.g. {stranded}")
        return False

    counts = "".join(f"{t}{len(v)} " for t, v in sorted(tags.items()))
    print(f"  {name}: {grid.w}x{grid.h}, {len(walk)} walkable, spawns {counts}")
    return True


# ============================================================== chapter 1
# The ward. A long spine corridor with patient rooms either side — the shape
# that lets the Matron patrol in and out of sight while staying audible.

def level_ward():
    g = Grid(52, 30, wall="#")

    # Spine corridor rows 13-14; its wall ring is rows 12 and 15.
    g.corridor(3, 13, 48, 14)

    # North rooms: rows 8-11, ring 7-12. Row 12 is shared with the corridor.
    north = [4, 11, 18, 25, 32, 39]
    for x in north:
        g.room(x, 8, x + 4, 11)
        g.door(x + 2, 12)

    # South rooms: rows 16-20, ring 15-21. Row 15 is shared with the corridor.
    south = [6, 14, 22, 30, 38]
    for x in south:
        g.room(x, 16, x + 5, 20)
        g.door(x + 3, 15)

    # Nurses' station, plaster-walled, opening onto the spine.
    g.room(44, 16, 49, 21, wall="%")
    g.door(46, 15)

    # Dead-end store cupboard: the first place to hide.
    g.room(2, 8, 3, 11, wall="%")
    g.door(3, 12)

    g.finish_geometry = True

    # ---- spawns
    for i, x in enumerate(north):
        g.spawn(x, 8, "B")
        g.spawn(x + 4, 8, "B")
        if i % 2 == 0:
            g.spawn(x + 2, 10, "I")
        if i == 1:
            g.spawn(x + 3, 9, "C")
        if i == 4:
            g.spawn(x + 1, 10, "L")
    for i, x in enumerate(south):
        g.spawn(x, 17, "B")
        g.spawn(x + 5, 17, "B")
        if i == 0:
            g.spawn(x + 2, 19, "G")
        if i == 2:
            g.spawn(x + 4, 19, "C")
        if i == 3:
            g.spawn(x + 1, 17, "W")
        if i % 2 == 1:
            g.spawn(x + 2, 18, "L")
    g.spawn(47, 18, "N")
    g.spawn(45, 20, "L")
    g.spawn(3, 9, "L")
    for x in range(6, 48, 7):
        g.spawn(x, 13, "A")
    g.spawn(4, 14, "P")
    g.spawn(47, 13, "M")
    g.spawn(28, 14, "R")
    g.spawn(20, 14, "R")
    g.spawn(35, 14, "X")
    return g.finish()


# ============================================================== chapter 2
# The flooded lower ward. A closed loop with a central spine, so she can come
# from either direction and you can never fully clear a route behind you.

def level_flood():
    g = Grid(46, 34, wall="%")

    g.corridor(3, 4, 42, 5)          # north run,  ring rows 3 and 6
    g.corridor(3, 28, 42, 29)        # south run,  ring rows 27 and 30
    g.corridor(3, 4, 4, 29)          # west link
    g.corridor(41, 4, 42, 29)        # east link
    g.corridor(20, 6, 21, 27)        # central spine

    # Wards share row 6 with the north run and row 27 with the south run.
    top = [7, 25]
    bot = [7, 25]
    for x in top:
        g.room(x, 7, x + 10, 13, wall="#")
        g.door(x + 5, 6)
    for x in bot:
        g.room(x, 20, x + 10, 26, wall="#")
        g.door(x + 5, 27)

    # Plant room: concrete, opening onto the east link through their shared ring.
    g.room(33, 15, 39, 18, wall="=")
    g.door(40, 17)

    # ---- spawns
    for i, x in enumerate(top + bot):
        y = 7 if i < 2 else 20
        for bx in range(x, x + 10, 3):
            g.spawn(bx, y, "B")
            g.spawn(bx, y + 6, "B")
        g.spawn(x + 1, y + 3, "L")
        g.spawn(x + 8, y + 3, "L")
        g.spawn(x + 5, y + 3, "A")
        if i in (0, 3):
            g.spawn(x + 4, y + 3, "C")
        if i == 1:
            g.spawn(x + 6, y + 2, "G")
        if i == 2:
            g.spawn(x + 3, y + 4, "W")
    g.spawn(36, 17, "N")
    g.spawn(34, 16, "R")
    g.spawn(4, 4, "P")
    g.spawn(41, 29, "M")
    g.spawn(21, 16, "R")
    g.spawn(9, 10, "R")
    g.spawn(4, 29, "X")
    return g.finish()


# ============================================================== chapter 3
# Records and the incinerator. Parallel stacks with narrow aisles: short
# sightlines, no room to dodge past her, and she knows the building.

def level_records():
    g = Grid(40, 32, wall="=")

    g.corridor(3, 3, 36, 4)          # north run, ring rows 2 and 5
    g.corridor(3, 3, 4, 22)          # west link
    g.corridor(35, 3, 36, 22)        # east link
    g.corridor(3, 21, 36, 22)        # south run, ring rows 20 and 23

    # Inner service runs, sharing row 5 with the north run and row 20 with the
    # south run.
    g.corridor(7, 6, 32, 7)
    g.corridor(7, 18, 32, 19)
    for x in (10, 20, 29):
        g.door(x, 5)
        g.door(x, 20)

    # Stacks: 2 wide, aisles between, sharing ring rows 8 and 17 with the runs.
    stacks = [8, 12, 16, 20, 24, 28]
    for x in stacks:
        g.room(x, 9, x + 1, 16, wall="#")
        g.door(x, 8)
        g.door(x + 1, 17)

    # Incinerator, hung below the south run and sharing its ring row 23.
    g.room(14, 24, 25, 29, wall="=")
    g.door(20, 23)

    # ---- spawns
    for i, x in enumerate(stacks):
        if i % 2 == 0:
            g.spawn(x, 12, "L")
        if i == 3:
            g.spawn(x + 1, 14, "C")
    g.spawn(20, 26, "N")
    g.spawn(17, 27, "R")
    g.spawn(23, 25, "R")
    g.spawn(20, 25, "A")
    g.spawn(19, 6, "A")
    g.spawn(19, 19, "A")
    g.spawn(4, 3, "P")
    g.spawn(35, 22, "M")
    g.spawn(4, 22, "X")
    return g.finish()


LEVELS = {
    "ward": (level_ward, "PMXNC"),
    "flood": (level_flood, "PMXNC"),
    "records": (level_records, "PMXNC"),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    only = set(sys.argv[1:])
    ok = True
    for name, (fn, required) in LEVELS.items():
        if only and name not in only:
            continue
        g = fn()
        if not validate(g, name, required):
            ok = False
            continue
        with open(os.path.join(OUT, f"{name}.txt"), "w") as f:
            f.write(f";  St Agnes - {name}\n")
            f.write(g.text())
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
