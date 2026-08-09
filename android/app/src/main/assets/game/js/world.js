// The eighth floor of a seven-floor building.
//
// The map is a U: you start at the west end of the long hall, walk east, turn
// south at the bend, then walk back west to a door. Going through that door puts
// you at the start again. You can never see the whole route at once, which is the
// entire point — the hall gets to change behind you.

import { T } from './textures.js';
import { clamp } from './util.js';

export const MAP_W = 32;
export const MAP_H = 24;

const idx = (x, y) => y * MAP_W + x;

export const DOOR = {
  BATH: 'bath',
  NURSERY: 'nursery',
  STORE: 'store',
  EXIT: 'exit',
  CLOSET_A: 'closetA',
  CLOSET_B: 'closetB',
  CLOSET_C: 'closetC',
};

export function buildWorld() {
  const grid = new Uint8Array(MAP_W * MAP_H).fill(T.WALLPAPER);

  const carve = (x0, y0, x1, y1, tex = 0) => {
    for (let y = y0; y <= y1; y++) {
      for (let x = x0; x <= x1; x++) {
        if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H) grid[idx(x, y)] = tex;
      }
    }
  };
  const face = (x0, y0, x1, y1, tex) => {
    for (let y = y0; y <= y1; y++) {
      for (let x = x0; x <= x1; x++) {
        if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && grid[idx(x, y)] !== 0) {
          grid[idx(x, y)] = tex;
        }
      }
    }
  };

  // --- the route -----------------------------------------------------------
  carve(3, 4, 26, 5);      // hall A, running east
  carve(25, 4, 26, 17);    // hall B, the bend, running south
  carve(5, 16, 26, 17);    // hall C, running back west
  carve(2, 16, 3, 17);     // vestibule behind the exit door

  // --- rooms ---------------------------------------------------------------
  carve(9, 1, 13, 2);      // bathroom
  face(8, 0, 14, 3, T.TILE);
  carve(14, 19, 20, 22);   // nursery
  face(13, 18, 21, 23, T.NURSERY);
  carve(28, 9, 30, 12);    // storage
  face(27, 8, 31, 13, T.CONCRETE);

  // Closets, one tile deep, for hiding in.
  carve(8, 7, 8, 7);
  carve(22, 7, 22, 7);
  carve(10, 14, 10, 14);
  face(7, 6, 9, 8, T.WOOD);
  face(21, 6, 23, 8, T.WOOD);
  face(9, 13, 11, 15, T.WOOD);

  // --- wall dressing -------------------------------------------------------
  for (let x = 4; x <= 24; x++) {
    if (grid[idx(x, 3)]) grid[idx(x, 3)] = (x % 5 < 2) ? T.PHOTOS : T.WALLPAPER;
  }
  grid[idx(18, 3)] = T.CLOCK;
  grid[idx(16, 6)] = T.PHOTOS;
  grid[idx(12, 6)] = T.PHOTOS;
  for (let y = 8; y <= 14; y++) if (grid[idx(27, y)]) grid[idx(27, y)] = T.WOOD;
  grid[idx(14, 15)] = T.MIRROR;
  grid[idx(20, 18)] = T.PHOTOS;
  for (let x = 6; x <= 24; x++) if (grid[idx(x, 18)] === T.WALLPAPER && x % 7 === 0) {
    grid[idx(x, 18)] = T.PHOTOS;
  }

  // --- doors ---------------------------------------------------------------
  // A door owns its tile. It is solid until `open` passes the threshold, and it
  // renders as a sliding panel on the tile's centre plane.
  const doors = [
    { id: DOOR.BATH,     x: 11, y: 3,  open: 0, locked: false, tex: T.DOOR,     axis: 'y' },
    { id: DOOR.CLOSET_A, x: 8,  y: 6,  open: 0, locked: false, tex: T.DOOR,     axis: 'y', closet: true },
    { id: DOOR.CLOSET_B, x: 22, y: 6,  open: 0, locked: false, tex: T.DOOR,     axis: 'y', closet: true },
    { id: DOOR.CLOSET_C, x: 10, y: 15, open: 0, locked: false, tex: T.DOOR,     axis: 'y', closet: true },
    { id: DOOR.STORE,    x: 27, y: 10, open: 0, locked: false, tex: T.DOOR,     axis: 'x' },
    { id: DOOR.NURSERY,  x: 17, y: 18, open: 0, locked: true,  tex: T.DOOR_RED, axis: 'y' },
    { id: DOOR.EXIT,     x: 4,  y: 16, open: 0, locked: false, tex: T.DOOR_RED, axis: 'x' },
  ];
  const doorAt = new Map();
  for (const d of doors) {
    grid[idx(d.x, d.y)] = d.tex;
    doorAt.set(idx(d.x, d.y), d);
  }
  // The exit door is two tiles tall so the whole corridor mouth is blocked.
  grid[idx(4, 17)] = T.DOOR_RED;
  const exitLower = { ...doors[6], y: 17, mirrorOf: DOOR.EXIT };
  doorAt.set(idx(4, 17), exitLower);

  const world = {
    grid,
    doors,
    doorAt,
    exitLower,
    props: [],
    // Player start, and the direction they always wake up facing.
    spawn: { x: 4.5, y: 5.0, a: 0 },
    // Stepping into this rectangle in the vestibule closes the loop.
    loopTrigger: { x0: 1.5, y0: 15.5, x1: 3.4, y1: 18.0 },
    waterLevel: 0,       // 0..1, how far up the wall the flood has come
    ambient: 0.62,       // base light with the torch off
    fog: 0.085,          // exponential fog density, per world unit
    lightsOn: true,
    roll: 0,             // world tilt, radians
    baseGrid: null,
  };
  world.baseGrid = grid.slice();
  return world;
}

// ------------------------------------------------------------------ queries

export function tileAt(w, x, y) {
  if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return T.WALLPAPER;
  return w.grid[idx(x | 0, y | 0)];
}

export function doorTile(w, x, y) {
  return w.doorAt.get(idx(x | 0, y | 0));
}

/** Solid for movement. Doors block until they are most of the way open. */
export function isSolid(w, x, y) {
  const tx = x | 0, ty = y | 0;
  if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return true;
  const d = w.doorAt.get(idx(tx, ty));
  if (d) return d.open < 0.82;
  return w.grid[idx(tx, ty)] !== 0;
}

/** Circle-vs-grid slide. Axes resolved separately so walls guide rather than stop. */
export function moveWithCollision(w, pos, dx, dy, radius = 0.26) {
  const nx = pos.x + dx;
  if (!isSolid(w, nx + Math.sign(dx) * radius, pos.y - radius) &&
      !isSolid(w, nx + Math.sign(dx) * radius, pos.y + radius)) {
    pos.x = nx;
  }
  const ny = pos.y + dy;
  if (!isSolid(w, pos.x - radius, ny + Math.sign(dy) * radius) &&
      !isSolid(w, pos.x + radius, ny + Math.sign(dy) * radius)) {
    pos.y = ny;
  }
}

export function findDoor(w, id) {
  return w.doors.find((d) => d.id === id);
}

export function openDoor(w, id, instant = false) {
  const d = findDoor(w, id);
  if (!d) return null;
  d.locked = false;
  d.target = 1;
  if (instant) d.open = 1;
  return d;
}

export function closeDoor(w, id, instant = false) {
  const d = findDoor(w, id);
  if (!d) return null;
  d.target = 0;
  if (instant) d.open = 0;
  return d;
}

export function updateDoors(w, dt) {
  for (const d of w.doors) {
    if (d.target === undefined) continue;
    const speed = d.fast ? 4.5 : 1.5;
    const before = d.open;
    if (d.open < d.target) d.open = Math.min(d.target, d.open + dt * speed);
    else if (d.open > d.target) d.open = Math.max(d.target, d.open - dt * speed);
    if (before !== d.open && d.id === DOOR.EXIT) w.exitLower.open = d.open;
  }
}

// -------------------------------------------------------------------- props

let propSeq = 0;

export function addProp(w, kind, x, y, opts = {}) {
  const p = {
    uid: ++propSeq,
    kind,
    x, y,
    sprite: opts.sprite || kind,
    scale: opts.scale ?? 0.5,
    yOff: opts.yOff ?? 0,        // 0 = floor level, 1 = ceiling
    usable: opts.usable ?? false,
    label: opts.label || 'USE',
    range: opts.range ?? 1.2,
    used: false,
    data: opts.data || {},
    solid: false,
  };
  w.props.push(p);
  return p;
}

export function removeProp(w, prop) {
  const i = w.props.indexOf(prop);
  if (i >= 0) w.props.splice(i, 1);
}

export function clearProps(w) {
  w.props.length = 0;
}

/** Nearest usable prop within reach and roughly in front of the player. */
export function usableNear(w, px, py, pa) {
  let best = null, bestD = Infinity;
  for (const p of w.props) {
    if (!p.usable || p.used) continue;
    const d = Math.hypot(p.x - px, p.y - py);
    if (d > p.range || d > bestD) continue;
    const rel = Math.abs(Math.atan2(p.y - py, p.x - px) - pa);
    const norm = Math.min(rel, Math.PI * 2 - rel);
    if (norm > 1.25) continue;    // must be broadly in view
    best = p; bestD = d;
  }
  return best;
}

// ------------------------------------------------------------- mutations

/**
 * Reshape the floor for a given loop. Called once at the top of each loop, after
 * the corridor has swallowed the player and before they can see anything.
 */
export function applyLoop(w, loop) {
  w.grid.set(w.baseGrid);
  clearProps(w);

  // Light and water rise together, and both only ever go one way. Loops 1-3 are
  // genuinely lit so that the blackout at the top of loop 4 costs the player
  // something; from there the ambient only ever falls, and the fog closes in
  // with it until the torch beam is the whole world.
  const table = [
    null,
    { water: 0.00, ambient: 0.62, fog: 0.085, lights: true,  roll: 0 },
    { water: 0.00, ambient: 0.54, fog: 0.10,  lights: true,  roll: 0 },
    { water: 0.02, ambient: 0.44, fog: 0.12,  lights: true,  roll: 0 },
    { water: 0.06, ambient: 0.13, fog: 0.19,  lights: false, roll: 0 },
    { water: 0.11, ambient: 0.115, fog: 0.20, lights: false, roll: 0.01 },
    { water: 0.19, ambient: 0.10, fog: 0.21,  lights: false, roll: 0.02 },
    { water: 0.30, ambient: 0.09, fog: 0.22,  lights: false, roll: 0.055 },
    { water: 0.45, ambient: 0.08, fog: 0.23,  lights: false, roll: 0.085 },
    { water: 0.62, ambient: 0.07, fog: 0.25,  lights: false, roll: 0.12 },
  ];
  const cfg = table[clamp(loop, 1, 9)];
  w.waterLevel = cfg.water;
  w.ambient = cfg.ambient;
  w.fog = cfg.fog;
  w.lightsOn = cfg.lights;
  w.roll = cfg.roll;

  for (const d of w.doors) {
    d.open = 0;
    d.target = 0;
    d.fast = false;
  }
  findDoor(w, DOOR.NURSERY).locked = loop < 8;
  w.exitLower.open = 0;

  // The radio is the one fixed point in the hall. It is always on the table.
  addProp(w, 'radio', 6.5, 4.4, {
    sprite: 'radio', scale: 0.42, usable: true, label: 'LISTEN', range: 1.5,
  });

  // Torch batteries, fewer as the loops go on.
  const batterySpots = [
    [15.5, 5.5], [26.5, 12.5], [21.5, 16.5], [9.5, 16.5], [29.5, 10.5],
  ];
  const count = Math.max(1, 5 - Math.floor(loop / 2));
  for (let i = 0; i < count; i++) {
    const [bx, by] = batterySpots[i % batterySpots.length];
    addProp(w, 'battery', bx, by, {
      sprite: 'battery', scale: 0.22, usable: true, label: 'TAKE', range: 1.1,
    });
  }

  // Loop-specific dressing.
  if (loop >= 2) {
    // The photographs stop matching the ones from the loop before.
    for (let x = 4; x <= 24; x++) {
      if (w.grid[idx(x, 3)]) w.grid[idx(x, 3)] = ((x + loop) % 4 < 2) ? T.PHOTOS : T.WALLPAPER;
    }
    addProp(w, 'note', 12.5, 4.4, {
      sprite: 'note', scale: 0.26, usable: true, label: 'READ', range: 1.2,
      data: { note: 0 },
    });
  }

  if (loop >= 3) {
    // The bathroom door is open before you get to it. You did not open it.
    openDoor(w, DOOR.BATH, true);
    addProp(w, 'handprint', 11.0, 3.9, { sprite: 'handprint', scale: 0.4, yOff: 0.45 });
    addProp(w, 'doll', 11.5, 1.5, {
      sprite: 'doll', scale: 0.34, usable: true, label: 'TAKE', range: 1.2,
      data: { doll: true },
    });
  }

  if (loop >= 4) {
    addProp(w, 'note', 25.5, 12.5, {
      sprite: 'note', scale: 0.26, usable: true, label: 'READ', range: 1.2,
      data: { note: 1 },
    });
    addProp(w, 'handprint', 25.1, 8.5, { sprite: 'handprint', scale: 0.42, yOff: 0.5 });
  }

  if (loop >= 5) {
    addProp(w, 'handprint', 26.9, 15.2, { sprite: 'handprint', scale: 0.45, yOff: 0.5 });
    addProp(w, 'doll', 18.5, 16.6, { sprite: 'doll', scale: 0.34 });
  }

  if (loop >= 6) {
    addProp(w, 'note', 8.5, 16.6, {
      sprite: 'note', scale: 0.26, usable: true, label: 'READ', range: 1.2,
      data: { note: 2 },
    });
    // Handprints start coming from inside the walls.
    for (const [hx, hy, off] of [[13.2, 15.9, 0.55], [19.8, 15.9, 0.4], [24.9, 6.6, 0.6]]) {
      addProp(w, 'handprint', hx, hy, { sprite: 'handprint', scale: 0.4, yOff: off });
    }
  }

  if (loop >= 7) {
    // The building stops keeping up appearances.
    for (let y = 6; y <= 14; y++) {
      if (w.grid[idx(24, y)]) w.grid[idx(24, y)] = T.MEAT;
      if (w.grid[idx(27, y)]) w.grid[idx(27, y)] = T.MEAT;
    }
    for (let x = 6; x <= 20; x += 3) {
      if (w.grid[idx(x, 18)]) w.grid[idx(x, 18)] = T.MEAT;
    }
    addProp(w, 'doll', 6.5, 16.5, { sprite: 'doll', scale: 0.34 });
    addProp(w, 'doll', 24.5, 4.5, { sprite: 'doll', scale: 0.34 });
  }

  if (loop >= 8) {
    for (let x = 4; x <= 24; x++) {
      if (w.grid[idx(x, 3)] === T.WALLPAPER && x % 3 === 0) w.grid[idx(x, 3)] = T.MEAT;
      if (w.grid[idx(x, 6)] === T.WALLPAPER && x % 4 === 0) w.grid[idx(x, 6)] = T.MEAT;
    }
    addProp(w, 'note', 17.0, 21.0, {
      sprite: 'note', scale: 0.26, usable: true, label: 'READ', range: 1.4,
      data: { note: 3 },
    });
    addProp(w, 'doll', 16.0, 20.5, { sprite: 'doll', scale: 0.4 });
    addProp(w, 'doll', 18.2, 21.5, { sprite: 'doll', scale: 0.4 });
  }

  if (loop >= 9) {
    for (let i = 0; i < w.grid.length; i++) {
      if (w.grid[i] === T.WALLPAPER && (i * 2654435761 % 7) === 0) w.grid[i] = T.MEAT;
    }
  }
}

/** Notes found in the hall, in the order the story hands them out. */
export const NOTES = [
  {
    title: 'a note in your own handwriting',
    body: 'Bring the car round at eleven. Do not let her drive, she has been up ' +
          'since five with Ellie.\n\nYou wrote this. You do not remember writing this.',
  },
  {
    title: 'incident report, page 2',
    body: 'Vehicle entered the channel at approx 23:51. Driver egress via ' +
          'front offside door.\n\nRear child lock: ENGAGED.',
  },
  {
    title: 'a hospital wristband',
    body: 'VALE, ADAM J.\nAdmitted 00:14.\nDischarged against advice 04:40.\n\n' +
          'Someone has written on the back, in biro: he keeps going back up.',
  },
  {
    title: "Ellie's drawing",
    body: 'Three stick figures in a blue box. Two of them are under a wavy line. ' +
          'One of them is above it, drawn much smaller, and much further away.\n\n' +
          'Underneath, in careful letters: DADDY IS AT THE TOP.',
  },
];
