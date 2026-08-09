// Mara.
//
// She is a state machine over a BFS flow field. The important thing about her is
// not the pathfinding, it is the pacing: she spends most of her time visible but
// not approaching, or approaching but not visible. HUNT is rare and short,
// because a monster that is always chasing stops being frightening within about
// ninety seconds.

import { MAP_W, MAP_H, isSolid } from './world.js';
import { clamp, angleDelta, rng } from './util.js';

export const STATE = {
  GONE: 'gone',        // not in the level at all
  LURK: 'lurk',        // standing somewhere you will eventually look
  STALK: 'stalk',      // closing, slowly, and only while unobserved
  HUNT: 'hunt',        // running at you
  SEARCH: 'search',    // lost you; sweeping your last known position
  CAUGHT: 'caught',
};

// Places she likes to be seen standing: ends of straights and the far side of
// the bend, where the torch will find her at the edge of its range.
const HAUNTS = [
  [24.5, 4.6], [4.5, 5.4], [25.5, 16.5], [6.5, 16.5],
  [25.5, 9.5], [11.5, 4.6], [18.5, 16.5], [11.5, 1.5],
];

export class Entity {
  constructor(world) {
    this.world = world;
    this.x = 0;
    this.y = 0;
    this.a = 0;
    this.state = STATE.GONE;
    this.speed = 0;
    this.timer = 0;
    this.rand = rng(9001);
    this.path = [];
    this.flow = new Int16Array(MAP_W * MAP_H);
    this.flowTarget = -1;
    this.lastKnown = { x: 0, y: 0 };
    this.visibleFor = 0;
    this.aggression = 0;      // 0..1, set per loop
    this.stepTimer = 0;
    this.onStep = null;
    this.onCatch = null;
    this.onSpotted = null;
    this.spottedCooldown = 0;
  }

  reset() {
    this.state = STATE.GONE;
    this.timer = 0;
    this.visibleFor = 0;
    this.path.length = 0;
  }

  /** Place her at a haunt that the player is not currently looking at. */
  spawnAway(player, minDist = 7) {
    const cands = HAUNTS
      .map((h) => ({ h, d: Math.hypot(h[0] - player.x, h[1] - player.y) }))
      .filter((c) => c.d > minDist && !this.canSee(c.h[0], c.h[1], player.x, player.y));
    const pool = cands.length ? cands : HAUNTS.map((h) => ({ h, d: 99 }));
    const chosen = pool[(this.rand() * pool.length) | 0].h;
    this.x = chosen[0];
    this.y = chosen[1];
    this.state = STATE.LURK;
    this.timer = 2 + this.rand() * 4;
  }

  setAggression(v) {
    this.aggression = clamp(v, 0, 1);
  }

  // ------------------------------------------------------------- sensing

  /** Bresenham-ish line-of-sight over the tile grid. */
  canSee(x0, y0, x1, y1) {
    const w = this.world;
    const dx = x1 - x0, dy = y1 - y0;
    const dist = Math.hypot(dx, dy);
    const steps = Math.ceil(dist * 3);
    for (let i = 1; i < steps; i++) {
      const t = i / steps;
      if (isSolid(w, x0 + dx * t, y0 + dy * t)) return false;
    }
    return true;
  }

  /** Is she inside the player's view cone, lit, and unobstructed? */
  seenBy(player) {
    const dx = this.x - player.x;
    const dy = this.y - player.y;
    const dist = Math.hypot(dx, dy);
    const reach = player.torch > 0.05 ? 12 : 4.5;
    if (dist > reach) return false;
    const rel = Math.abs(angleDelta(player.a, Math.atan2(dy, dx)));
    const cone = player.torch > 0.05 ? 0.55 : 0.75;
    if (rel > cone) return false;
    return this.canSee(this.x, this.y, player.x, player.y);
  }

  // --------------------------------------------------------- pathfinding

  /** BFS flow field out from the target, so she can follow the gradient down. */
  _buildFlow(tx, ty) {
    const key = (ty | 0) * MAP_W + (tx | 0);
    if (key === this.flowTarget) return;
    this.flowTarget = key;

    const w = this.world;
    const flow = this.flow;
    flow.fill(-1);
    const q = new Int32Array(MAP_W * MAP_H);
    let head = 0, tail = 0;
    flow[key] = 0;
    q[tail++] = key;

    while (head < tail) {
      const cur = q[head++];
      const cx = cur % MAP_W;
      const cy = (cur / MAP_W) | 0;
      const d = flow[cur];
      for (let i = 0; i < 4; i++) {
        const nx = cx + (i === 0 ? 1 : i === 1 ? -1 : 0);
        const ny = cy + (i === 2 ? 1 : i === 3 ? -1 : 0);
        if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
        const nk = ny * MAP_W + nx;
        if (flow[nk] !== -1) continue;
        // Treat closed doors as passable for planning; she opens them.
        if (w.grid[nk] !== 0 && !w.doorAt.has(nk)) continue;
        flow[nk] = d + 1;
        q[tail++] = nk;
      }
    }
  }

  /** Unit vector pointing downhill on the flow field. */
  _flowDir() {
    const cx = this.x | 0, cy = this.y | 0;
    const here = this.flow[cy * MAP_W + cx];
    if (here < 0) return null;
    let best = here, bx = 0, by = 0;
    for (let i = 0; i < 4; i++) {
      const nx = cx + (i === 0 ? 1 : i === 1 ? -1 : 0);
      const ny = cy + (i === 2 ? 1 : i === 3 ? -1 : 0);
      if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
      const v = this.flow[ny * MAP_W + nx];
      if (v >= 0 && v < best) { best = v; bx = nx; by = ny; }
    }
    if (best === here) return null;
    // Aim for the centre of the better tile, so she does not clip corners.
    const dx = (bx + 0.5) - this.x;
    const dy = (by + 0.5) - this.y;
    const d = Math.hypot(dx, dy) || 1;
    return { x: dx / d, y: dy / d };
  }

  // -------------------------------------------------------------- update

  update(dt, player, ctx) {
    if (this.state === STATE.GONE || this.state === STATE.CAUGHT) return;

    this.timer -= dt;
    this.spottedCooldown -= dt;
    const dist = Math.hypot(player.x - this.x, player.y - this.y);
    const seen = this.seenBy(player);
    this.visibleFor = seen ? this.visibleFor + dt : 0;

    if (seen && this.spottedCooldown <= 0) {
      this.spottedCooldown = 6;
      if (this.onSpotted) this.onSpotted(dist);
    }

    switch (this.state) {
      case STATE.LURK: {
        // She stands still and lets you find her. If you look away, she is gone.
        this.a = Math.atan2(player.y - this.y, player.x - this.x);
        this.speed = 0;
        if (this.visibleFor > 1.4 + this.rand() * 1.2) {
          if (this.rand() < 0.45 + this.aggression * 0.4) {
            this.state = STATE.HUNT;
            this.timer = 7 + this.aggression * 7;
            if (ctx && ctx.onHuntStart) ctx.onHuntStart(dist);
          } else {
            this.spawnAway(player, 9);
            if (ctx && ctx.onVanish) ctx.onVanish();
          }
        } else if (this.timer <= 0) {
          this.state = STATE.STALK;
          this.timer = 8 + this.rand() * 8;
        }
        break;
      }

      case STATE.STALK: {
        // Closes only while unwatched — the freezer-monster trick, because it
        // makes the player's own decision to look away the thing that hurts them.
        this.speed = seen ? 0 : (0.85 + this.aggression * 0.6);
        this._steer(dt, player.x, player.y);
        if (dist < 2.2) {
          this.state = STATE.HUNT;
          this.timer = 6;
          if (ctx && ctx.onHuntStart) ctx.onHuntStart(dist);
        } else if (this.timer <= 0) {
          if (this.rand() < 0.5) {
            this.spawnAway(player, 8);
            if (ctx && ctx.onVanish) ctx.onVanish();
          } else {
            this.state = STATE.HUNT;
            this.timer = 5 + this.aggression * 6;
            if (ctx && ctx.onHuntStart) ctx.onHuntStart(dist);
          }
        }
        break;
      }

      case STATE.HUNT: {
        this.speed = 1.55 + this.aggression * 1.15;
        this._steer(dt, player.x, player.y);
        this.lastKnown.x = player.x;
        this.lastKnown.y = player.y;
        if (player.hiding && !player.betrayed) {
          this.state = STATE.SEARCH;
          this.timer = 9;
        } else if (dist < 0.75) {
          this.state = STATE.CAUGHT;
          if (this.onCatch) this.onCatch();
        } else if (this.timer <= 0 && dist > 6) {
          this.state = STATE.SEARCH;
          this.timer = 7;
        }
        break;
      }

      case STATE.SEARCH: {
        this.speed = 1.0 + this.aggression * 0.5;
        this._steer(dt, this.lastKnown.x, this.lastKnown.y);
        const near = Math.hypot(this.lastKnown.x - this.x, this.lastKnown.y - this.y);
        if (!player.hiding && dist < 5 && this.canSee(this.x, this.y, player.x, player.y)) {
          this.state = STATE.HUNT;
          this.timer = 8;
        } else if (player.betrayed && dist < 7) {
          // Held your breath too long, or ran out of it. She heard.
          this.state = STATE.HUNT;
          this.timer = 8;
        } else if (near < 0.8 || this.timer <= 0) {
          this.spawnAway(player, 6);
        }
        break;
      }
      default: break;
    }

    // Footsteps, spaced by how fast she is actually moving.
    if (this.speed > 0.05) {
      this.stepTimer -= dt * this.speed;
      if (this.stepTimer <= 0) {
        this.stepTimer = 0.62;
        if (this.onStep) this.onStep(dist);
      }
    }
  }

  _steer(dt, tx, ty) {
    if (this.speed <= 0.001) return;
    this._buildFlow(tx, ty);
    let dir = this._flowDir();
    if (!dir) {
      const dx = tx - this.x, dy = ty - this.y;
      const d = Math.hypot(dx, dy) || 1;
      dir = { x: dx / d, y: dy / d };
    }
    this.a = Math.atan2(dir.y, dir.x);

    const step = this.speed * dt;
    const nx = this.x + dir.x * step;
    const ny = this.y + dir.y * step;
    // She is not stopped by doors — she pushes through them.
    if (!this._blocked(nx, this.y)) this.x = nx;
    if (!this._blocked(this.x, ny)) this.y = ny;
  }

  _blocked(x, y) {
    const w = this.world;
    const tx = x | 0, ty = y | 0;
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return true;
    const k = ty * MAP_W + tx;
    if (w.doorAt.has(k)) {
      const d = w.doorAt.get(k);
      // Force it open as she arrives.
      if (d.open < 1) { d.target = 1; d.fast = true; d.locked = false; }
      return d.open < 0.4;
    }
    return w.grid[k] !== 0;
  }

  /** Sprite descriptor for the renderer. */
  sprite(tex, player) {
    if (this.state === STATE.GONE || this.state === STATE.CAUGHT) return null;
    const dist = Math.hypot(player.x - this.x, player.y - this.y);
    const canvas = dist < 3.2 ? tex.sprites.figure_near
      : dist < 7 ? tex.sprites.figure_mid
      : tex.sprites.figure_far;
    return {
      x: this.x, y: this.y,
      canvas,
      scale: 1.05,
      yOff: 0,
      // She is always a little brighter than the room justifies.
      light: 1.35,
      tintR: 1.0, tintG: 0.94, tintB: 0.92,
      alpha: this.state === STATE.STALK ? 0.94 : 1,
    };
  }
}
