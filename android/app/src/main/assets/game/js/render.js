// Raycasting renderer.
//
// The world is drawn into a small Uint32 buffer (typically ~380x210) and then
// upscaled to the display. That is what makes a per-pixel lit, floor-cast,
// sprite-composited scene run at 60fps inside a WebView on a mid-range phone —
// and the softness of the upscale plus the grain on top is most of the look.
//
// Lighting is computed per pixel from three terms: a flat ambient, the torch
// (an ellipse in screen space that falls off with distance), and exponential
// fog. Nothing is precomputed per shade level, because the torch moves.

import { MAP_W, MAP_H, doorTile } from './world.js';
import { TEX_SIZE, T } from './textures.js';
import { clamp } from './util.js';

const TORCH_RANGE = 11.0;
const MAX_STEPS = 64;

export class Renderer {
  constructor(canvas, tex) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d', { alpha: false, desynchronized: true });
    this.tex = tex;

    // Low-res world buffer.
    this.buf = document.createElement('canvas');
    this.bctx = this.buf.getContext('2d', { alpha: false });
    this.img = null;
    this.pix = null;
    this.zbuf = null;

    this.iw = 0;
    this.ih = 0;
    this.quality = 1.0;
    this.dynScale = 1.0;
    this._frameTimes = [];

    this.grainTiles = [];
    this._buildOverlays();
  }

  // -------------------------------------------------------------- setup

  _buildOverlays() {
    // Four grain tiles, cycled per frame so the noise never sits still.
    for (let t = 0; t < 4; t++) {
      const c = document.createElement('canvas');
      c.width = c.height = 160;
      const g = c.getContext('2d');
      const d = g.createImageData(160, 160);
      for (let i = 0; i < 160 * 160; i++) {
        const v = (Math.random() * 255) | 0;
        d.data[i * 4] = d.data[i * 4 + 1] = d.data[i * 4 + 2] = v;
        d.data[i * 4 + 3] = 255;
      }
      g.putImageData(d, 0, 0);
      this.grainTiles.push(c);
    }

    this.scanline = document.createElement('canvas');
    this.scanline.width = 4;
    this.scanline.height = 4;
    const sg = this.scanline.getContext('2d');
    sg.fillStyle = 'rgba(0,0,0,0.30)';
    sg.fillRect(0, 3, 4, 1);
  }

  resize(cssW, cssH, dpr) {
    this.canvas.width = Math.max(1, Math.round(cssW * dpr));
    this.canvas.height = Math.max(1, Math.round(cssH * dpr));
    this.dispW = this.canvas.width;
    this.dispH = this.canvas.height;
    this.aspect = cssW / cssH;
    this._sizeBuffer();
    this.vignette = null;   // regenerate at the new size
  }

  _sizeBuffer() {
    // Cap the world buffer independently of the panel: a 1440p phone gains
    // nothing from casting 1440 columns.
    const targetW = clamp(
      Math.round(420 * this.quality * this.dynScale), 160, 640);
    const iw = targetW;
    const ih = Math.max(90, Math.round(iw / Math.max(1.1, this.aspect || 1.8)));
    if (iw === this.iw && ih === this.ih) return;

    this.iw = iw;
    this.ih = ih;
    this.buf.width = iw;
    this.buf.height = ih;
    this.img = this.bctx.createImageData(iw, ih);
    this.pix = new Uint32Array(this.img.data.buffer);
    this.zbuf = new Float32Array(iw);
  }

  setQuality(q) {
    this.quality = clamp(q, 0.5, 1.5);
    this._sizeBuffer();
  }

  /** Track frame cost and quietly trade resolution for smoothness. */
  _adapt(frameMs) {
    const ft = this._frameTimes;
    ft.push(frameMs);
    if (ft.length < 40) return;
    ft.sort((a, b) => a - b);
    const median = ft[ft.length >> 1];
    ft.length = 0;
    const before = this.dynScale;
    if (median > 24 && this.dynScale > 0.55) this.dynScale -= 0.08;
    else if (median < 12 && this.dynScale < 1.0) this.dynScale += 0.05;
    this.dynScale = clamp(this.dynScale, 0.55, 1.0);
    if (before !== this.dynScale) this._sizeBuffer();
  }

  // ------------------------------------------------------------- drawing

  /**
   * cam:   {x, y, a, pitch, bob, torch, torchWidth}
   * world: the map
   * fx:    {shake, roll, chroma, glitch, redshift, blind, grain, vignette}
   */
  render(cam, world, sprites, fx, time, frameMs) {
    this._adapt(frameMs);
    const { iw, ih, pix } = this;

    const dirX = Math.cos(cam.a);
    const dirY = Math.sin(cam.a);
    // FOV ~ 75deg horizontal. The plane vector length is tan(fov/2).
    const planeLen = 0.78 * (this.aspect ? clamp(this.aspect / 1.78, 0.75, 1.25) : 1);
    const planeX = -dirY * planeLen;
    const planeY = dirX * planeLen;

    // Horizon moves with look-pitch and head-bob.
    const horizon = (ih * 0.5 + cam.pitch * ih * 0.5 + cam.bob * ih) | 0;

    const ambient = world.ambient;
    const FOG = world.fog ?? 0.20;
    const torchOn = cam.torch > 0.01;
    const torchPow = cam.torch;
    const beamW = cam.torchWidth ?? 0.42;

    this._castFloor(cam, world, dirX, dirY, planeX, planeY, horizon,
                    ambient, torchOn, torchPow, beamW, time, FOG);
    this._castWalls(cam, world, dirX, dirY, planeX, planeY, horizon,
                    ambient, torchOn, torchPow, beamW, time, FOG);
    this._castSprites(cam, sprites, dirX, dirY, planeX, planeY, horizon,
                      ambient, torchOn, torchPow, beamW, FOG);

    if (fx.redshift > 0.001) this._redshift(fx.redshift);

    this.bctx.putImageData(this.img, 0, 0);
    this._present(fx, time);
  }

  // --------------------------------------------------------- floor/ceiling

  _castFloor(cam, world, dirX, dirY, planeX, planeY, horizon,
             ambient, torchOn, torchPow, beamW, time, FOG) {
    const { iw, ih, pix } = this;
    const floorTex = this.tex.floor;
    const ceilTex = this.tex.ceiling;
    const waterTex = this.tex.water;
    const water = world.waterLevel;
    const TS = TEX_SIZE;

    // Ray directions at the extreme left and right of the screen.
    const rdx0 = dirX - planeX, rdy0 = dirY - planeY;
    const rdx1 = dirX + planeX, rdy1 = dirY + planeY;

    const posZ = 0.5 * ih;
    const wob = Math.sin(time * 1.7) * 0.5 + 0.5;

    // Anything the casts below do not reach must be black, not last frame's
    // pixels — looking up or down leaves bands the floor loop never visits.
    pix.fill(0xFF000000);

    // Every other row is cast and duplicated. At this buffer size and with
    // grain over the top, the difference is invisible and it halves the cost.
    for (let y = horizon + 1; y < ih; y += 2) {
      const p = y - horizon;
      if (p <= 0) continue;
      const rowDist = posZ / p;
      if (rowDist > 40) continue;

      const stepX = (rowDist * (rdx1 - rdx0)) / iw;
      const stepY = (rowDist * (rdy1 - rdy0)) / iw;
      let fx = cam.x + rowDist * rdx0;
      let fy = cam.y + rowDist * rdy0;

      const fog = Math.exp(-rowDist * FOG);
      // Vertical position of this row within the torch ellipse.
      const yNorm = (y - horizon) / (ih * 0.5);
      const vfall = Math.max(0, 1 - yNorm * yNorm * 0.55);
      const tdist = Math.max(0, 1 - rowDist / TORCH_RANGE);
      const tdist2 = tdist * tdist;

      const rowOff = y * iw;
      const rowOff2 = (y + 1 < ih) ? (y + 1) * iw : -1;

      // Water only exists below the horizon and only once it has risen.
      const submerged = water > 0.001 && rowDist < 26;

      for (let x = 0; x < iw; x++) {
        const camX = (2 * x) / iw - 1;
        const hfall = Math.max(0, 1 - (camX * camX) / (beamW * beamW * 2.4));
        let light = ambient;
        if (torchOn) light += torchPow * hfall * vfall * tdist2 * 1.55;
        light *= fog;

        let tx = ((fx * TS) | 0) & (TS - 1);
        let ty = ((fy * TS) | 0) & (TS - 1);

        // --- floor
        let c = floorTex[ty * TS + tx];
        let r = (c & 255), g = (c >> 8) & 255, b = (c >> 16) & 255;

        if (submerged) {
          // Ripple the lookup, then blend in an inverted ceiling sample as a
          // reflection. Cheap, and it sells standing water better than a tint.
          const rip = Math.sin((fx + fy) * 6.0 + time * 2.1) * 0.5
                    + Math.sin((fx - fy) * 9.0 - time * 1.3) * 0.5;
          const rx = ((fx * TS + rip * 3) | 0) & (TS - 1);
          const ry = ((fy * TS + rip * 3) | 0) & (TS - 1);
          const wc = waterTex[ry * TS + rx];
          const cc = ceilTex[(ry * TS + rx)];
          const k = clamp(water * 1.5, 0, 0.9);
          const refl = 0.35 + 0.2 * wob;
          r = r * (1 - k) + (((wc & 255) * (1 - refl)) + ((cc & 255) * refl)) * k;
          g = g * (1 - k) + ((((wc >> 8) & 255) * (1 - refl)) + (((cc >> 8) & 255) * refl)) * k;
          b = b * (1 - k) + ((((wc >> 16) & 255) * (1 - refl)) + (((cc >> 16) & 255) * refl)) * k;
          b *= 1.12;
        }

        const px = 0xFF000000
          | (clamp(b * light, 0, 255) << 16)
          | (clamp(g * light, 0, 255) << 8)
          | clamp(r * light, 0, 255);
        pix[rowOff + x] = px;
        if (rowOff2 >= 0) pix[rowOff2 + x] = px;

        // --- ceiling (mirrored row above the horizon)
        const cy = horizon - p;
        if (cy >= 0) {
          const cc = ceilTex[ty * TS + tx];
          const cl = light * 0.72;
          const cpx = 0xFF000000
            | (clamp(((cc >> 16) & 255) * cl, 0, 255) << 16)
            | (clamp(((cc >> 8) & 255) * cl, 0, 255) << 8)
            | clamp((cc & 255) * cl, 0, 255);
          pix[cy * iw + x] = cpx;
          if (cy - 1 >= 0) pix[(cy - 1) * iw + x] = cpx;
        }

        fx += stepX;
        fy += stepY;
      }
    }
  }

  // ------------------------------------------------------------- walls

  _castWalls(cam, world, dirX, dirY, planeX, planeY, horizon,
             ambient, torchOn, torchPow, beamW, time, FOG) {
    const { iw, ih, pix, zbuf } = this;
    const walls = this.tex.walls;
    const TS = TEX_SIZE;
    const water = world.waterLevel;

    for (let x = 0; x < iw; x++) {
      const camX = (2 * x) / iw - 1;
      const rayX = dirX + planeX * camX;
      const rayY = dirY + planeY * camX;

      let mapX = cam.x | 0;
      let mapY = cam.y | 0;

      const deltaX = rayX === 0 ? 1e30 : Math.abs(1 / rayX);
      const deltaY = rayY === 0 ? 1e30 : Math.abs(1 / rayY);

      let stepX, stepY, sideX, sideY;
      if (rayX < 0) { stepX = -1; sideX = (cam.x - mapX) * deltaX; }
      else { stepX = 1; sideX = (mapX + 1 - cam.x) * deltaX; }
      if (rayY < 0) { stepY = -1; sideY = (cam.y - mapY) * deltaY; }
      else { stepY = 1; sideY = (mapY + 1 - cam.y) * deltaY; }

      let hit = 0, side = 0, tile = 0, dist = 0, wallX = 0;

      for (let s = 0; s < MAX_STEPS && !hit; s++) {
        if (sideX < sideY) { sideX += deltaX; mapX += stepX; side = 0; }
        else { sideY += deltaY; mapY += stepY; side = 1; }

        if (mapX < 0 || mapY < 0 || mapX >= MAP_W || mapY >= MAP_H) {
          hit = 1; tile = T.CONCRETE;
          dist = side === 0 ? sideX - deltaX : sideY - deltaY;
          break;
        }

        const t = world.grid[mapY * MAP_W + mapX];
        if (!t) continue;

        const door = doorTile(world, mapX, mapY);
        if (door) {
          // A door sits on the centre plane of its tile and slides sideways.
          // Step half a tile further along the ray, then test whether the ray
          // is still inside the leaf.
          const base = side === 0 ? sideX - deltaX : sideY - deltaY;
          const half = (side === 0 ? deltaX : deltaY) * 0.5;
          const d2 = base + half;
          const hx = cam.x + rayX * d2;
          const hy = cam.y + rayY * d2;
          // Where along the door's width did we cross?
          const along = side === 0 ? hy - Math.floor(hy) : hx - Math.floor(hx);
          // Leaf covers [open, 1]; below `open` the gap is see-through.
          if (along < door.open) continue;
          // Did the shifted hit leave the tile entirely?
          if ((side === 0 && (hy | 0) !== mapY) || (side === 1 && (hx | 0) !== mapX)) continue;
          hit = 1;
          tile = door.tex;
          dist = d2;
          wallX = (along - door.open) / Math.max(0.001, 1 - door.open);
          continue;
        }

        hit = 1;
        tile = t;
        dist = side === 0 ? sideX - deltaX : sideY - deltaY;
        wallX = side === 0 ? cam.y + dist * rayY : cam.x + dist * rayX;
        wallX -= Math.floor(wallX);
      }

      if (!hit) { zbuf[x] = 1e30; continue; }
      if (dist < 0.0001) dist = 0.0001;
      zbuf[x] = dist;

      const lineH = (ih / dist) | 0;
      let y0 = horizon - (lineH >> 1);
      let y1 = y0 + lineH;
      const drawStart = y0 < 0 ? 0 : y0;
      const drawEnd = y1 > ih ? ih : y1;

      const tex = walls[tile] || walls[T.WALLPAPER];
      let texX = (wallX * TS) | 0;
      if ((side === 0 && rayX > 0) || (side === 1 && rayY < 0)) texX = TS - texX - 1;
      texX &= TS - 1;
      const texCol = texX * 1;

      const fog = Math.exp(-dist * FOG);
      const sideDim = side === 1 ? 0.74 : 1.0;
      const tdist = Math.max(0, 1 - dist / TORCH_RANGE);
      const tdist2 = tdist * tdist;
      const hfall = Math.max(0, 1 - (camX * camX) / (beamW * beamW * 2.4));

      const step = TS / lineH;
      let texPos = (drawStart - horizon + (lineH >> 1)) * step;

      // Water line on the wall: everything below it is darkened and tinted.
      const waterY = water > 0.001 ? (y1 - lineH * water) : 1e9;

      for (let y = drawStart; y < drawEnd; y++) {
        const texY = (texPos | 0) & (TS - 1);
        texPos += step;

        const c = tex[texY * TS + texCol];
        let r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;

        const yNorm = (y - horizon) / (ih * 0.5);
        const vfall = Math.max(0, 1 - yNorm * yNorm * 0.5);
        let light = ambient;
        if (torchOn) light += torchPow * hfall * vfall * tdist2 * 1.7;
        light *= fog * sideDim;

        if (y > waterY) {
          // Submerged wall: darker, greener, and rippling.
          const rip = Math.sin(y * 0.55 + time * 3.1 + x * 0.11) * 0.09 + 1;
          light *= 0.45 * rip;
          r *= 0.7; g *= 0.95; b *= 1.25;
        }

        pix[y * iw + x] = 0xFF000000
          | (clamp(b * light, 0, 255) << 16)
          | (clamp(g * light, 0, 255) << 8)
          | clamp(r * light, 0, 255);
      }
    }
  }

  // ------------------------------------------------------------ sprites

  _castSprites(cam, sprites, dirX, dirY, planeX, planeY, horizon,
               ambient, torchOn, torchPow, beamW, FOG) {
    if (!sprites.length) return;
    const { iw, ih, pix, zbuf } = this;

    // Painter's order, far to near.
    const list = sprites
      .map((s) => ({ s, d: (s.x - cam.x) ** 2 + (s.y - cam.y) ** 2 }))
      .sort((a, b) => b.d - a.d);

    const invDet = 1.0 / (planeX * dirY - dirX * planeY);

    for (const { s } of list) {
      const spr = s.canvas;
      if (!spr) continue;
      if (!s._data) {
        const c = document.createElement('canvas');
        c.width = spr.width; c.height = spr.height;
        c.getContext('2d').drawImage(spr, 0, 0);
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height);
        s._data = new Uint32Array(d.data.buffer);
        s._w = c.width; s._h = c.height;
      }

      const relX = s.x - cam.x;
      const relY = s.y - cam.y;
      const transX = invDet * (dirY * relX - dirX * relY);
      const transY = invDet * (-planeY * relX + planeX * relY);
      if (transY <= 0.08) continue;

      const screenX = ((iw / 2) * (1 + transX / transY)) | 0;
      const scale = s.scale ?? 1;
      const h = Math.abs((ih / transY) * scale) | 0;
      const w = Math.abs((ih / transY) * scale * (s._w / s._h)) | 0;
      if (h < 1 || w < 1) continue;

      // yOff 0 plants the sprite's feet on the floor; 1 puts it at the ceiling.
      const floorY = horizon + (ih / transY) * 0.5;
      const bottom = floorY - (s.yOff ?? 0) * (ih / transY);
      const y0 = (bottom - h) | 0;

      const dsX = Math.max(0, screenX - (w >> 1));
      const deX = Math.min(iw, screenX + (w >> 1));
      const dsY = Math.max(0, y0);
      const deY = Math.min(ih, y0 + h);

      const fog = Math.exp(-transY * FOG);
      const camXc = (2 * screenX) / iw - 1;
      const hfall = Math.max(0, 1 - (camXc * camXc) / (beamW * beamW * 2.4));
      const tdist = Math.max(0, 1 - transY / TORCH_RANGE);
      let light = ambient * 1.1;
      if (torchOn) light += torchPow * hfall * tdist * tdist * 1.5;
      light *= fog;
      light *= s.light ?? 1;

      const tintR = s.tintR ?? 1, tintG = s.tintG ?? 1, tintB = s.tintB ?? 1;
      const alpha = s.alpha ?? 1;
      const sw = s._w, sh = s._h, sd = s._data;

      for (let x = dsX; x < deX; x++) {
        if (transY >= zbuf[x]) continue;
        const tx = (((x - (screenX - (w >> 1))) * sw) / w) | 0;
        if (tx < 0 || tx >= sw) continue;
        for (let y = dsY; y < deY; y++) {
          const ty = (((y - y0) * sh) / h) | 0;
          if (ty < 0 || ty >= sh) continue;
          const c = sd[ty * sw + tx];
          const a = ((c >> 24) & 255) / 255 * alpha;
          if (a < 0.02) continue;

          const sr = (c & 255) * light * tintR;
          const sg = ((c >> 8) & 255) * light * tintG;
          const sb = ((c >> 16) & 255) * light * tintB;

          const o = y * iw + x;
          const dcol = pix[o];
          const dr = dcol & 255, dg = (dcol >> 8) & 255, db = (dcol >> 16) & 255;

          pix[o] = 0xFF000000
            | (clamp(db + (sb - db) * a, 0, 255) << 16)
            | (clamp(dg + (sg - dg) * a, 0, 255) << 8)
            | clamp(dr + (sr - dr) * a, 0, 255);
        }
      }
    }
  }

  /** Push the whole frame toward blood. Used as sanity collapses. */
  _redshift(amount) {
    const pix = this.pix;
    const n = pix.length;
    const k = clamp(amount, 0, 1);
    for (let i = 0; i < n; i++) {
      const c = pix[i];
      const r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
      const lum = (r * 77 + g * 151 + b * 28) >> 8;
      pix[i] = 0xFF000000
        | (clamp(b + (lum * 0.25 - b) * k, 0, 255) << 16)
        | (clamp(g + (lum * 0.35 - g) * k, 0, 255) << 8)
        | clamp(r + (lum * 1.25 - r) * k, 0, 255);
    }
  }

  // ------------------------------------------------------------- present

  _present(fx, time) {
    const ctx = this.ctx;
    const W = this.dispW, H = this.dispH;

    ctx.save();
    ctx.imageSmoothingEnabled = true;

    const shake = fx.shake || 0;
    const sx = shake ? (Math.random() - 0.5) * shake * W * 0.05 : 0;
    const sy = shake ? (Math.random() - 0.5) * shake * H * 0.05 : 0;
    const roll = (fx.roll || 0) + (shake ? (Math.random() - 0.5) * shake * 0.06 : 0);

    ctx.translate(W / 2 + sx, H / 2 + sy);
    if (roll) ctx.rotate(roll);
    // Slight overscan so shake and roll never expose the page behind.
    const over = 1.10 + shake * 0.06;
    ctx.scale(over, over);

    const dw = W, dh = H;
    if (fx.chroma > 0.001) {
      // Split the channels apart. Three composited passes, only during scares.
      const off = fx.chroma * W * 0.012;
      ctx.globalCompositeOperation = 'lighter';
      ctx.filter = 'url(#none)';
      for (const [dx, col] of [[-off, '#ff0000'], [0, '#00ff00'], [off, '#0000ff']]) {
        ctx.save();
        ctx.globalCompositeOperation = 'lighter';
        ctx.drawImage(this._channel(col), -dw / 2 + dx, -dh / 2, dw, dh);
        ctx.restore();
      }
      ctx.globalCompositeOperation = 'source-over';
    } else {
      ctx.drawImage(this.buf, -dw / 2, -dh / 2, dw, dh);
    }
    ctx.restore();

    // ---- overlays, all at display resolution
    ctx.save();

    if (fx.glitch > 0.001) this._glitch(fx.glitch);

    const grain = fx.grain ?? 0.10;
    if (grain > 0.002) {
      const tile = this.grainTiles[(time * 24 | 0) % 4];
      ctx.globalAlpha = grain;
      ctx.globalCompositeOperation = 'overlay';
      if (!this._grainPat || this._grainPatTile !== tile) {
        this._grainPat = ctx.createPattern(tile, 'repeat');
        this._grainPatTile = tile;
      }
      ctx.fillStyle = this._grainPat;
      ctx.save();
      ctx.translate((Math.random() * 160) | 0, (Math.random() * 160) | 0);
      ctx.fillRect(-160, -160, W + 320, H + 320);
      ctx.restore();
      ctx.globalCompositeOperation = 'source-over';
      ctx.globalAlpha = 1;
    }

    if (!this._scanPat) this._scanPat = ctx.createPattern(this.scanline, 'repeat');
    ctx.globalAlpha = 0.14;
    ctx.fillStyle = this._scanPat;
    ctx.fillRect(0, 0, W, H);
    ctx.globalAlpha = 1;

    if (!this.vignette) this._buildVignette();
    ctx.globalAlpha = clamp(0.40 + (fx.vignette || 0), 0, 1);
    ctx.drawImage(this.vignette, 0, 0, W, H);
    ctx.globalAlpha = 1;

    if (fx.blind > 0.001) {
      ctx.fillStyle = `rgba(0,0,0,${clamp(fx.blind, 0, 1)})`;
      ctx.fillRect(0, 0, W, H);
    }
    if (fx.flash > 0.001) {
      ctx.fillStyle = `rgba(255,248,240,${clamp(fx.flash, 0, 1)})`;
      ctx.fillRect(0, 0, W, H);
    }
    if (fx.blood > 0.001) {
      ctx.fillStyle = `rgba(120,10,6,${clamp(fx.blood, 0, 1) * 0.5})`;
      ctx.fillRect(0, 0, W, H);
    }

    ctx.restore();
  }

  /** Tinted copy of the world buffer, for the chromatic split. */
  _channel(colour) {
    if (!this._chanCv) {
      this._chanCv = document.createElement('canvas');
      this._chanCtx = this._chanCv.getContext('2d');
    }
    const c = this._chanCv;
    if (c.width !== this.iw || c.height !== this.ih) {
      c.width = this.iw; c.height = this.ih;
    }
    const g = this._chanCtx;
    g.globalCompositeOperation = 'source-over';
    g.drawImage(this.buf, 0, 0);
    g.globalCompositeOperation = 'multiply';
    g.fillStyle = colour;
    g.fillRect(0, 0, c.width, c.height);
    g.globalCompositeOperation = 'source-over';
    return c;
  }

  /** Horizontal tear bands, VHS-style. */
  _glitch(amount) {
    const ctx = this.ctx;
    const W = this.dispW, H = this.dispH;
    const bands = 2 + (amount * 9) | 0;
    for (let i = 0; i < bands; i++) {
      const y = (Math.random() * H) | 0;
      const h = 2 + (Math.random() * H * 0.06 * amount) | 0;
      const dx = (Math.random() - 0.5) * W * 0.12 * amount;
      ctx.drawImage(this.canvas, 0, y, W, h, dx, y, W, h);
      if (Math.random() < 0.3) {
        ctx.fillStyle = `rgba(${180 + Math.random() * 70 | 0},20,14,${0.05 + Math.random() * 0.12})`;
        ctx.fillRect(0, y, W, h);
      }
    }
  }

  _buildVignette() {
    const c = document.createElement('canvas');
    c.width = 256; c.height = 256;
    const g = c.getContext('2d');
    const grad = g.createRadialGradient(128, 128, 40, 128, 128, 168);
    grad.addColorStop(0, 'rgba(0,0,0,0)');
    grad.addColorStop(0.55, 'rgba(0,0,0,0.22)');
    grad.addColorStop(1, 'rgba(0,0,0,0.96)');
    g.fillStyle = grad;
    g.fillRect(0, 0, 256, 256);
    this.vignette = c;
  }
}
