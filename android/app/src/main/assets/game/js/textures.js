// Every image in the game is generated here at boot. Nothing is downloaded.
//
// Wall textures are packed into Uint32Arrays (0xAABBGGRR, matching ImageData's
// byte order) because the raycaster reads one texel per screen pixel and cannot
// afford object access. Sprites and the scare faces stay as canvases, since they
// are blitted with drawImage.

import { clamp, rng, TAU } from './util.js';

export const TEX_SIZE = 128;

// Wall texture ids. Index 0 is reserved for "empty".
export const T = {
  WALLPAPER: 1,
  PHOTOS: 2,
  CLOCK: 3,
  TILE: 4,
  WOOD: 5,
  CONCRETE: 6,
  MIRROR: 7,
  NURSERY: 8,
  DOOR: 9,
  DOOR_RED: 10,
  MEAT: 11,
};

// ---------------------------------------------------------------- noise

function hash2(x, y, seed) {
  let h = x * 374761393 + y * 668265263 + seed * 1442695040888963407;
  h = (h ^ (h >> 13)) * 1274126177;
  return ((h ^ (h >> 16)) >>> 0) / 4294967296;
}

function valueNoise(x, y, seed) {
  const xi = Math.floor(x), yi = Math.floor(y);
  const xf = x - xi, yf = y - yi;
  const u = xf * xf * (3 - 2 * xf);
  const v = yf * yf * (3 - 2 * yf);
  const a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
  const c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
  return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

function fbm(x, y, octaves, seed, lac = 2.0, gain = 0.5) {
  let sum = 0, amp = 0.5, freq = 1, norm = 0;
  for (let i = 0; i < octaves; i++) {
    sum += valueNoise(x * freq, y * freq, seed + i * 37) * amp;
    norm += amp;
    amp *= gain;
    freq *= lac;
  }
  return sum / norm;
}

// --------------------------------------------------------------- helpers

const pack = (r, g, b) =>
  (255 << 24) | (clamp(b, 0, 255) << 16) | (clamp(g, 0, 255) << 8) | clamp(r, 0, 255);

function makeCanvas(w, h) {
  const c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  return c;
}

/** Rasterise a per-pixel shader into a packed Uint32Array. */
function shade(fn, size = TEX_SIZE) {
  const out = new Uint32Array(size * size);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const c = fn(x, y, x / size, y / size);
      out[y * size + x] = pack(c[0] | 0, c[1] | 0, c[2] | 0);
    }
  }
  return out;
}

/** Convert a canvas to a packed texture (for textures easier to draw than shade). */
function canvasToTex(cv) {
  const d = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height);
  return new Uint32Array(d.data.buffer.slice(0));
}

// ---------------------------------------------------------- wall shaders

function texWallpaper(seed) {
  return shade((x, y, u, v) => {
    // Damp base with vertical staining that pools toward the skirting.
    const grime = fbm(u * 5, v * 5, 4, seed) * 0.5 + fbm(u * 19, v * 19, 3, seed + 9) * 0.5;
    const damp = clamp((v - 0.55) * 2.2, 0, 1) * fbm(u * 3.3, v * 1.4, 3, seed + 4);

    // Faded floral repeat.
    const fx = (u * 4) % 1, fy = (v * 5) % 1;
    const petal = Math.max(0, 1 - Math.hypot(fx - 0.5, fy - 0.5) * 3.6);
    const flower = Math.pow(petal, 1.6) * (0.5 + 0.5 * Math.sin(Math.atan2(fy - 0.5, fx - 0.5) * 5));

    let r = 96 + grime * 34 - damp * 46 + flower * 22;
    let g = 82 + grime * 28 - damp * 44 + flower * 10;
    let b = 68 + grime * 22 - damp * 38 + flower * 12;

    // Peeling seams every quarter width.
    const seam = Math.abs(((u * 4) % 1) - 0.5);
    if (seam > 0.47) { r *= 0.62; g *= 0.6; b *= 0.58; }

    // Torn strips lifting away from the plaster.
    const tear = fbm(u * 8 + 30, v * 2, 4, seed + 71);
    if (tear > 0.72 && v > 0.25) {
      const k = (tear - 0.72) / 0.28;
      r = r * (1 - k) + 58 * k;
      g = g * (1 - k) + 50 * k;
      b = b * (1 - k) + 44 * k;
    }
    return [r, g, b];
  });
}

function texPhotos(seed) {
  const cv = makeCanvas(TEX_SIZE, TEX_SIZE);
  const g = cv.getContext('2d');
  const base = texWallpaper(seed);
  const img = g.createImageData(TEX_SIZE, TEX_SIZE);
  new Uint32Array(img.data.buffer).set(base);
  g.putImageData(img, 0, 0);

  const r = rng(seed);
  // Four frames, deliberately not level.
  const frames = [
    [12, 18, 40, 34], [66, 12, 44, 38], [20, 66, 38, 44], [72, 62, 40, 46],
  ];
  for (const [fx, fy, fw, fh] of frames) {
    g.save();
    g.translate(fx + fw / 2, fy + fh / 2);
    g.rotate((r() - 0.5) * 0.22);
    g.translate(-fw / 2, -fh / 2);

    g.fillStyle = '#1a1310';
    g.fillRect(-2, -2, fw + 4, fh + 4);
    g.fillStyle = '#6b5f4e';
    g.fillRect(0, 0, fw, fh);

    // The photograph itself: two figures, and a third that is always too tall.
    g.fillStyle = '#c9bda6';
    g.fillRect(3, 3, fw - 6, fh - 6);
    g.fillStyle = 'rgba(30,24,20,0.85)';
    g.beginPath();
    g.arc(fw * 0.38, fh * 0.42, fw * 0.09, 0, TAU);
    g.fill();
    g.fillRect(fw * 0.30, fh * 0.52, fw * 0.16, fh * 0.34);
    g.beginPath();
    g.arc(fw * 0.62, fh * 0.5, fw * 0.07, 0, TAU);
    g.fill();
    g.fillRect(fw * 0.56, fh * 0.58, fw * 0.13, fh * 0.28);

    // Scratched-out face.
    g.strokeStyle = 'rgba(20,14,12,0.9)';
    g.lineWidth = 2;
    for (let i = 0; i < 6; i++) {
      g.beginPath();
      g.moveTo(fw * 0.28 + r() * 8, fh * 0.32 + r() * 12);
      g.lineTo(fw * 0.46 - r() * 8, fh * 0.54 - r() * 12);
      g.stroke();
    }

    g.fillStyle = 'rgba(0,0,0,0.35)';
    g.fillRect(3, 3, fw - 6, 3);
    g.restore();
  }
  return canvasToTex(cv);
}

function texClock(seed) {
  const cv = makeCanvas(TEX_SIZE, TEX_SIZE);
  const g = cv.getContext('2d');
  const img = g.createImageData(TEX_SIZE, TEX_SIZE);
  new Uint32Array(img.data.buffer).set(texWallpaper(seed));
  g.putImageData(img, 0, 0);

  const cx = 64, cy = 52, rad = 30;
  g.fillStyle = '#0d0908';
  g.beginPath(); g.arc(cx, cy, rad + 5, 0, TAU); g.fill();
  g.fillStyle = '#3a3129';
  g.beginPath(); g.arc(cx, cy, rad + 3, 0, TAU); g.fill();
  g.fillStyle = '#ded2b8';
  g.beginPath(); g.arc(cx, cy, rad, 0, TAU); g.fill();

  g.strokeStyle = '#231b17';
  g.lineWidth = 1.5;
  for (let i = 0; i < 12; i++) {
    const a = (i / 12) * TAU;
    g.beginPath();
    g.moveTo(cx + Math.cos(a) * (rad - 5), cy + Math.sin(a) * (rad - 5));
    g.lineTo(cx + Math.cos(a) * (rad - 1), cy + Math.sin(a) * (rad - 1));
    g.stroke();
  }
  // 11:59. It is always 11:59.
  g.lineWidth = 3;
  g.strokeStyle = '#1a120f';
  g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx - 3, cy - 16); g.stroke();
  g.lineWidth = 2;
  g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx - 5, cy - 25); g.stroke();
  g.fillStyle = '#8e1f14';
  g.beginPath(); g.arc(cx, cy, 3, 0, TAU); g.fill();

  return canvasToTex(cv);
}

function texTile(seed) {
  return shade((x, y, u, v) => {
    const gx = (u * 8) % 1, gy = (v * 8) % 1;
    const grout = (gx < 0.07 || gy < 0.07) ? 1 : 0;
    const dirt = fbm(u * 6, v * 6, 4, seed);
    const chip = hash2(Math.floor(u * 8), Math.floor(v * 8), seed) > 0.88 ? 0.6 : 1;

    let r = grout ? 52 : 138 * chip;
    let g2 = grout ? 48 : 142 * chip;
    let b = grout ? 44 : 134 * chip;

    r -= dirt * 40; g2 -= dirt * 44; b -= dirt * 40;

    // Rust bleeding down from a fixing.
    const rust = clamp(fbm(u * 3 + 11, v * 1.2, 3, seed + 5) - 0.55, 0, 1) * 2.4;
    r += rust * 62; g2 += rust * 18; b += rust * 8;
    return [r, g2, b];
  });
}

function texWood(seed) {
  return shade((x, y, u, v) => {
    // Grain: warp the coordinate then band it.
    const warp = fbm(u * 2.5, v * 0.6, 4, seed) * 2.4;
    const rings = Math.sin((v * 9 + warp) * Math.PI * 2) * 0.5 + 0.5;
    const fine = fbm(u * 40, v * 6, 2, seed + 3);
    let r = 74 + rings * 26 + fine * 14;
    let g = 54 + rings * 18 + fine * 10;
    let b = 38 + rings * 12 + fine * 8;

    // Panel divisions.
    const px = Math.abs(((u * 2) % 1) - 0.5);
    if (px > 0.46) { r *= 0.55; g *= 0.55; b *= 0.55; }
    return [r, g, b];
  });
}

function texConcrete(seed) {
  return shade((x, y, u, v) => {
    const n = fbm(u * 9, v * 9, 5, seed);
    const pit = hash2(x, y, seed) > 0.985 ? 0.4 : 1;
    const c = (68 + n * 44) * pit;
    const damp = clamp((v - 0.6) * 2, 0, 1) * 0.35;
    return [c * (1 - damp), c * (1 - damp * 0.9), c * (1 - damp * 0.7)];
  });
}

function texMirror(seed) {
  return shade((x, y, u, v) => {
    const smear = fbm(u * 4, v * 4, 4, seed);
    const edge = Math.min(u, v, 1 - u, 1 - v);
    if (edge < 0.06) return [42, 36, 30];
    // A dark reflective sheet: mostly your own absence.
    const c = 26 + smear * 30 + (1 - v) * 18;
    return [c * 0.85, c * 0.95, c];
  });
}

function texNursery(seed) {
  return shade((x, y, u, v) => {
    const base = fbm(u * 6, v * 6, 4, seed);
    // Faded pastel stripes with hand-drawn stars.
    const stripe = Math.sin(u * Math.PI * 12) > 0 ? 1 : 0.86;
    let r = (128 + base * 26) * stripe;
    let g = (126 + base * 24) * stripe;
    let b = (112 + base * 20) * stripe;

    const sx = (u * 4) % 1, sy = (v * 4) % 1;
    const d = Math.hypot(sx - 0.5, sy - 0.5);
    const star = Math.max(0, 1 - d * 7) * (0.6 + 0.4 * Math.cos(Math.atan2(sy - 0.5, sx - 0.5) * 5));
    r += star * 40; g += star * 34; b += star * 8;

    const damp = clamp((v - 0.5) * 2.4, 0, 1) * fbm(u * 3, v, 3, seed + 8);
    r -= damp * 70; g -= damp * 66; b -= damp * 54;
    return [r, g, b];
  });
}

function texDoor(seed, red) {
  const cv = makeCanvas(TEX_SIZE, TEX_SIZE);
  const g = cv.getContext('2d');
  const img = g.createImageData(TEX_SIZE, TEX_SIZE);
  new Uint32Array(img.data.buffer).set(texWood(seed));
  g.putImageData(img, 0, 0);

  if (red) {
    g.fillStyle = 'rgba(96,18,12,0.55)';
    g.fillRect(0, 0, TEX_SIZE, TEX_SIZE);
  }
  // Two recessed panels.
  g.strokeStyle = 'rgba(0,0,0,0.55)';
  g.lineWidth = 3;
  g.strokeRect(20, 12, 88, 44);
  g.strokeRect(20, 68, 88, 48);
  g.strokeStyle = 'rgba(255,240,210,0.06)';
  g.lineWidth = 1;
  g.strokeRect(22, 14, 84, 40);
  g.strokeRect(22, 70, 84, 44);

  // Handle.
  g.fillStyle = '#241a14';
  g.beginPath(); g.arc(102, 64, 6, 0, TAU); g.fill();
  g.fillStyle = '#8a7a5a';
  g.beginPath(); g.arc(101, 63, 4, 0, TAU); g.fill();

  return canvasToTex(cv);
}

function texMeat(seed) {
  // Loop 8+: the walls stop pretending.
  return shade((x, y, u, v) => {
    const n = fbm(u * 7, v * 7, 5, seed);
    const veins = Math.abs(fbm(u * 14, v * 9, 4, seed + 2) - 0.5);
    const vein = clamp(1 - veins * 9, 0, 1);
    let r = 58 + n * 74 + vein * 52;
    let g = 20 + n * 22 + vein * 8;
    let b = 18 + n * 20 + vein * 10;
    const wet = Math.pow(fbm(u * 24, v * 24, 2, seed + 6), 3) * 90;
    return [r + wet, g + wet * 0.5, b + wet * 0.5];
  });
}

// ---------------------------------------------------------------- floors

function texCarpet(seed) {
  return shade((x, y, u, v) => {
    const fib = fbm(u * 46, v * 46, 3, seed);
    const pattern = Math.sin(u * Math.PI * 8) * Math.sin(v * Math.PI * 8);
    const stain = clamp(fbm(u * 3, v * 3, 4, seed + 12) - 0.48, 0, 1) * 2.6;
    let r = 52 + fib * 26 + pattern * 7 - stain * 26;
    let g = 34 + fib * 18 + pattern * 5 - stain * 24;
    let b = 30 + fib * 16 + pattern * 4 - stain * 20;
    return [r, g, b];
  });
}

function texCeiling(seed) {
  return shade((x, y, u, v) => {
    const n = fbm(u * 8, v * 8, 4, seed);
    const crack = clamp(1 - Math.abs(fbm(u * 5, v * 5, 3, seed + 4) - 0.5) * 14, 0, 1);
    const c = 46 + n * 26 - crack * 26;
    return [c, c * 0.96, c * 0.9];
  });
}

function texWater(seed) {
  return shade((x, y, u, v) => {
    const n = fbm(u * 10, v * 10, 4, seed);
    const c = 16 + n * 18;
    return [c * 0.8, c * 1.0, c * 1.15];
  });
}

// --------------------------------------------------------------- sprites

/**
 * The figure. `nearness` 0..1 slides her from a distant smear to something with
 * a face. Deliberately drawn without hard outlines so she reads as wrong rather
 * than as a cartoon.
 */
export function drawFigure(size = 256, nearness = 0, seed = 5) {
  const cv = makeCanvas(size, size);
  const g = cv.getContext('2d');
  const r = rng(seed);
  const cx = size / 2;

  const skin = `rgba(${188 + nearness * 30},${182 + nearness * 26},${176 + nearness * 20},`;

  // Dress: soaked, clinging, dripping into nothing at the hem.
  const hemY = size * 0.97;
  const grad = g.createLinearGradient(0, size * 0.3, 0, hemY);
  grad.addColorStop(0, 'rgba(28,30,36,0.96)');
  grad.addColorStop(0.7, 'rgba(16,18,24,0.9)');
  grad.addColorStop(1, 'rgba(8,9,13,0.0)');
  g.fillStyle = grad;
  g.beginPath();
  g.moveTo(cx - size * 0.10, size * 0.30);
  g.lineTo(cx + size * 0.10, size * 0.30);
  g.quadraticCurveTo(cx + size * 0.22, size * 0.66, cx + size * 0.19, hemY);
  g.lineTo(cx - size * 0.19, hemY);
  g.quadraticCurveTo(cx - size * 0.22, size * 0.66, cx - size * 0.10, size * 0.30);
  g.closePath();
  g.fill();

  // Arms, hanging slightly too long.
  g.strokeStyle = skin + '0.9)';
  g.lineWidth = size * 0.035;
  g.lineCap = 'round';
  for (const s of [-1, 1]) {
    g.beginPath();
    g.moveTo(cx + s * size * 0.09, size * 0.34);
    g.quadraticCurveTo(cx + s * size * 0.17, size * 0.52, cx + s * size * 0.13, size * 0.76);
    g.stroke();
  }

  // Head and neck.
  g.fillStyle = skin + '0.95)';
  g.beginPath();
  g.ellipse(cx, size * 0.20, size * 0.075, size * 0.095, 0, 0, TAU);
  g.fill();
  g.fillRect(cx - size * 0.028, size * 0.26, size * 0.056, size * 0.06);

  // Face, only once she is close enough for it to matter.
  if (nearness > 0.25) {
    const a = (nearness - 0.25) / 0.75;
    g.fillStyle = `rgba(4,3,4,${0.9 * a})`;
    for (const s of [-1, 1]) {
      g.beginPath();
      g.ellipse(cx + s * size * 0.028, size * 0.19, size * 0.019, size * 0.026, 0, 0, TAU);
      g.fill();
    }
    // Mouth: open, and open further than a jaw allows.
    g.beginPath();
    g.ellipse(cx, size * 0.235, size * 0.022, size * 0.038 * (0.5 + a), 0, 0, TAU);
    g.fill();
  }

  // Hair: wet ropes, hanging past the shoulders, hiding most of it.
  g.strokeStyle = 'rgba(8,7,9,0.92)';
  for (let i = 0; i < 46; i++) {
    const off = (r() - 0.5) * size * 0.17;
    const len = size * (0.30 + r() * 0.22);
    g.lineWidth = size * (0.006 + r() * 0.010);
    g.beginPath();
    g.moveTo(cx + off * 0.5, size * 0.12);
    g.quadraticCurveTo(
      cx + off * 1.5, size * 0.12 + len * 0.55,
      cx + off * 1.2 + (r() - 0.5) * size * 0.05, size * 0.12 + len
    );
    g.stroke();
  }

  // Water running off her.
  g.strokeStyle = 'rgba(150,170,190,0.16)';
  g.lineWidth = 1;
  for (let i = 0; i < 20; i++) {
    const x = cx + (r() - 0.5) * size * 0.36;
    const y0 = size * (0.3 + r() * 0.5);
    g.beginPath();
    g.moveTo(x, y0);
    g.lineTo(x + (r() - 0.5) * 3, y0 + size * 0.08 * r());
    g.stroke();
  }

  return cv;
}

/** Ellie, seen only at a distance and only ever from behind. */
export function drawChild(size = 192, seed = 12) {
  const cv = makeCanvas(size, size);
  const g = cv.getContext('2d');
  const r = rng(seed);
  const cx = size / 2;

  const grad = g.createLinearGradient(0, size * 0.42, 0, size * 0.98);
  grad.addColorStop(0, 'rgba(96,104,118,0.92)');
  grad.addColorStop(1, 'rgba(30,34,42,0.0)');
  g.fillStyle = grad;
  g.beginPath();
  g.moveTo(cx - size * 0.085, size * 0.44);
  g.lineTo(cx + size * 0.085, size * 0.44);
  g.lineTo(cx + size * 0.14, size * 0.97);
  g.lineTo(cx - size * 0.14, size * 0.97);
  g.closePath();
  g.fill();

  g.fillStyle = 'rgba(196,190,182,0.9)';
  g.beginPath();
  g.ellipse(cx, size * 0.36, size * 0.072, size * 0.082, 0, 0, TAU);
  g.fill();

  // Back of the head: hair, flat, wet. No face — she never turns around.
  g.fillStyle = 'rgba(26,20,18,0.95)';
  g.beginPath();
  g.ellipse(cx, size * 0.345, size * 0.078, size * 0.078, 0, 0, TAU);
  g.fill();
  g.strokeStyle = 'rgba(20,15,14,0.85)';
  for (let i = 0; i < 22; i++) {
    g.lineWidth = size * 0.008;
    const off = (r() - 0.5) * size * 0.12;
    g.beginPath();
    g.moveTo(cx + off, size * 0.30);
    g.lineTo(cx + off * 1.2, size * (0.44 + r() * 0.08));
    g.stroke();
  }
  return cv;
}

/** Small world props. Flat, dark, readable in a torch beam. */
export function drawProp(kind, size = 128, seed = 3) {
  const cv = makeCanvas(size, size);
  const g = cv.getContext('2d');
  const r = rng(seed);
  const s = size;

  if (kind === 'radio') {
    g.fillStyle = '#3b3128';
    g.fillRect(s * 0.18, s * 0.42, s * 0.64, s * 0.42);
    g.fillStyle = '#1a1512';
    g.fillRect(s * 0.23, s * 0.47, s * 0.34, s * 0.3);
    for (let i = 0; i < 7; i++) {
      g.fillStyle = 'rgba(120,110,90,0.5)';
      g.fillRect(s * 0.24, s * 0.49 + i * s * 0.04, s * 0.32, s * 0.012);
    }
    g.fillStyle = '#8a7a5a';
    g.beginPath(); g.arc(s * 0.7, s * 0.56, s * 0.05, 0, TAU); g.fill();
    g.beginPath(); g.arc(s * 0.7, s * 0.72, s * 0.05, 0, TAU); g.fill();
    g.strokeStyle = '#6b5f4e'; g.lineWidth = s * 0.018;
    g.beginPath(); g.moveTo(s * 0.74, s * 0.42); g.lineTo(s * 0.88, s * 0.1); g.stroke();
  } else if (kind === 'doll') {
    g.fillStyle = 'rgba(198,186,170,0.95)';
    g.beginPath(); g.arc(s * 0.5, s * 0.34, s * 0.13, 0, TAU); g.fill();
    g.fillStyle = 'rgba(120,40,34,0.85)';
    g.fillRect(s * 0.38, s * 0.46, s * 0.24, s * 0.3);
    g.strokeStyle = 'rgba(198,186,170,0.9)'; g.lineWidth = s * 0.035; g.lineCap = 'round';
    for (const d of [-1, 1]) {
      g.beginPath(); g.moveTo(s * 0.5 + d * s * 0.1, s * 0.5);
      g.lineTo(s * 0.5 + d * s * 0.22, s * 0.62); g.stroke();
      g.beginPath(); g.moveTo(s * 0.5 + d * s * 0.06, s * 0.75);
      g.lineTo(s * 0.5 + d * s * 0.10, s * 0.9); g.stroke();
    }
    g.fillStyle = '#100b0a';
    g.beginPath(); g.arc(s * 0.45, s * 0.32, s * 0.017, 0, TAU); g.fill();
    g.beginPath(); g.arc(s * 0.55, s * 0.32, s * 0.017, 0, TAU); g.fill();
    g.strokeStyle = '#3a1410'; g.lineWidth = s * 0.012;
    g.beginPath(); g.arc(s * 0.5, s * 0.4, s * 0.05, 0.25, Math.PI - 0.25); g.stroke();
  } else if (kind === 'battery') {
    g.fillStyle = '#2a2620';
    g.fillRect(s * 0.36, s * 0.3, s * 0.28, s * 0.5);
    g.fillStyle = '#9a8a4a';
    g.fillRect(s * 0.36, s * 0.3, s * 0.28, s * 0.12);
    g.fillStyle = '#c8b25a';
    g.fillRect(s * 0.45, s * 0.24, s * 0.1, s * 0.07);
  } else if (kind === 'note') {
    g.fillStyle = 'rgba(214,204,180,0.94)';
    g.save();
    g.translate(s * 0.5, s * 0.5);
    g.rotate((r() - 0.5) * 0.3);
    g.fillRect(-s * 0.2, -s * 0.26, s * 0.4, s * 0.52);
    g.strokeStyle = 'rgba(40,30,26,0.55)'; g.lineWidth = s * 0.012;
    for (let i = 0; i < 7; i++) {
      g.beginPath();
      g.moveTo(-s * 0.15, -s * 0.18 + i * s * 0.06);
      g.lineTo(-s * 0.15 + s * 0.3 * (0.5 + r() * 0.5), -s * 0.18 + i * s * 0.06);
      g.stroke();
    }
    g.restore();
  } else if (kind === 'handprint') {
    g.fillStyle = 'rgba(120,26,18,0.8)';
    g.beginPath(); g.ellipse(s * 0.5, s * 0.58, s * 0.13, s * 0.16, 0, 0, TAU); g.fill();
    for (let i = 0; i < 5; i++) {
      const a = -Math.PI * 0.86 + i * Math.PI * 0.18;
      g.save();
      g.translate(s * 0.5 + Math.cos(a) * s * 0.12, s * 0.5 + Math.sin(a) * s * 0.13);
      g.rotate(a + Math.PI / 2);
      g.beginPath(); g.ellipse(0, 0, s * 0.032, s * 0.09, 0, 0, TAU); g.fill();
      g.restore();
    }
  }
  return cv;
}

// ------------------------------------------------------------ the face
//
// This is the payload of every jump scare, drawn once at boot and then thrown
// at the whole screen. It has to survive being seen for six frames, so: extreme
// value contrast, no midtones around the eyes, and a mouth that reads as a hole
// rather than as an expression.

/** Uneven, some missing, none straight. Drawn twice: once with the mouth, once
 *  after the hair pass re-punches the cavity. */
function drawTeeth(g, cx, mouthY, mouthW, mouthH, r) {
  g.fillStyle = 'rgba(206,198,178,0.85)';
  const teeth = 11;
  for (let i = 0; i < teeth; i++) {
    if (r() > 0.82) continue;
    const t = i / (teeth - 1);
    const x = cx - mouthW * 0.82 + t * mouthW * 1.64;
    const w = mouthW * 0.13 * (0.7 + r() * 0.5);
    const h = mouthH * (0.28 + r() * 0.22);
    const yTop = mouthY - mouthH * 0.78;
    g.save();
    g.translate(x, yTop);
    g.rotate((r() - 0.5) * 0.35);
    g.fillRect(-w / 2, 0, w, h);
    g.restore();
    if (r() > 0.5) {
      const hb = mouthH * (0.2 + r() * 0.2);
      g.save();
      g.translate(x + (r() - 0.5) * 6, mouthY + mouthH * 0.76);
      g.rotate((r() - 0.5) * 0.4);
      g.fillRect(-w / 2, -hb, w, hb);
      g.restore();
    }
  }
}

export function drawScareFace(size = 640, variant = 0) {
  const cv = makeCanvas(size, size);
  const g = cv.getContext('2d');
  const r = rng(101 + variant * 17);
  const cx = size * 0.5;
  const cy = size * 0.47;
  const fw = size * 0.30;   // face half-width
  const fh = size * 0.40;   // face half-height

  g.fillStyle = '#000';
  g.fillRect(0, 0, size, size);

  // Skin: cold, blotched, wet.
  const skin = g.createRadialGradient(cx, cy - fh * 0.2, fw * 0.1, cx, cy, fh * 1.15);
  skin.addColorStop(0, variant === 1 ? '#d9d2c8' : '#c8c3ba');
  skin.addColorStop(0.55, '#8e8a84');
  skin.addColorStop(1, '#171518');
  g.fillStyle = skin;
  g.beginPath();
  g.ellipse(cx, cy, fw, fh, 0, 0, TAU);
  g.fill();

  // Mottling and burst capillaries.
  for (let i = 0; i < 340; i++) {
    const a = r() * TAU, rad = Math.sqrt(r()) * fw;
    const x = cx + Math.cos(a) * rad, y = cy + Math.sin(a) * rad * (fh / fw);
    const t = r();
    g.fillStyle = t > 0.82
      ? `rgba(${110 + r() * 40},${28 + r() * 18},${24 + r() * 14},${0.05 + r() * 0.16})`
      : `rgba(${40 + r() * 40},${38 + r() * 36},${44 + r() * 30},${0.04 + r() * 0.10})`;
    g.beginPath();
    g.ellipse(x, y, 2 + r() * 16, 2 + r() * 12, r() * TAU, 0, TAU);
    g.fill();
  }

  // Cheekbones and temples pushed out by shadow, so the skull shows through.
  g.fillStyle = 'rgba(10,8,10,0.55)';
  for (const s of [-1, 1]) {
    g.beginPath();
    g.ellipse(cx + s * fw * 0.62, cy - fh * 0.1, fw * 0.30, fh * 0.42, s * 0.3, 0, TAU);
    g.fill();
  }

  // --- eyes: sockets first, then whatever is left in them.
  const eyeY = cy - fh * 0.20;
  const eyeDX = fw * 0.40;
  for (const s of [-1, 1]) {
    const ex = cx + s * eyeDX;

    // Socket: a hole, not a shadow.
    const sock = g.createRadialGradient(ex, eyeY, 1, ex, eyeY, fw * 0.32);
    sock.addColorStop(0, '#000');
    sock.addColorStop(0.45, 'rgba(0,0,0,0.95)');
    sock.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = sock;
    g.beginPath();
    g.ellipse(ex, eyeY, fw * 0.30, fh * 0.26, 0, 0, TAU);
    g.fill();

    g.fillStyle = '#020102';
    g.beginPath();
    g.ellipse(ex, eyeY, fw * 0.155, fh * 0.125, 0, 0, TAU);
    g.fill();

    if (variant === 1) {
      // Variant: the eye is still in there, and it is looking at you.
      g.fillStyle = 'rgba(226,222,214,0.92)';
      g.beginPath();
      g.ellipse(ex, eyeY, fw * 0.105, fh * 0.075, 0, 0, TAU);
      g.fill();
      g.fillStyle = '#0a0709';
      g.beginPath();
      g.arc(ex + fw * 0.012, eyeY, fw * 0.045, 0, TAU);
      g.fill();
      g.strokeStyle = 'rgba(130,26,18,0.55)';
      g.lineWidth = size * 0.0035;
      for (let i = 0; i < 9; i++) {
        const a = r() * TAU;
        g.beginPath();
        g.moveTo(ex + Math.cos(a) * fw * 0.05, eyeY + Math.sin(a) * fh * 0.035);
        g.lineTo(ex + Math.cos(a) * fw * 0.11, eyeY + Math.sin(a) * fh * 0.08);
        g.stroke();
      }
    } else {
      // A single wet glint deep in the socket. Worse than an eye.
      g.fillStyle = 'rgba(210,205,200,0.5)';
      g.beginPath();
      g.arc(ex + fw * 0.03, eyeY - fh * 0.02, fw * 0.016, 0, TAU);
      g.fill();
    }

    // Tears of channel water cutting through the grime.
    g.strokeStyle = 'rgba(150,160,175,0.28)';
    g.lineWidth = size * 0.004;
    for (let i = 0; i < 4; i++) {
      const x0 = ex + (r() - 0.5) * fw * 0.2;
      g.beginPath();
      g.moveTo(x0, eyeY + fh * 0.10);
      g.bezierCurveTo(x0 + (r() - 0.5) * 12, cy + fh * 0.2,
                      x0 + (r() - 0.5) * 20, cy + fh * 0.45,
                      x0 + (r() - 0.5) * 26, cy + fh * 0.8);
      g.stroke();
    }
  }

  // --- mouth: hinged past where a jaw stops.
  const mouthY = cy + fh * 0.38;
  const mouthW = fw * (variant === 1 ? 0.52 : 0.40);
  const mouthH = fh * (variant === 1 ? 0.44 : 0.32);

  const cav = g.createRadialGradient(cx, mouthY, 1, cx, mouthY, mouthW);
  cav.addColorStop(0, '#000');
  cav.addColorStop(0.7, '#0a0406');
  cav.addColorStop(1, '#1d0d0e');
  g.fillStyle = cav;
  g.beginPath();
  g.ellipse(cx, mouthY, mouthW, mouthH, 0, 0, TAU);
  g.fill();

  drawTeeth(g, cx, mouthY, mouthW, mouthH, r);

  // Lips torn back from the gums.
  g.strokeStyle = 'rgba(90,20,16,0.7)';
  g.lineWidth = size * 0.012;
  g.beginPath();
  g.ellipse(cx, mouthY, mouthW * 1.03, mouthH * 1.03, 0, 0, TAU);
  g.stroke();

  // Split at the corners.
  g.strokeStyle = 'rgba(120,24,18,0.8)';
  g.lineWidth = size * 0.007;
  for (const s of [-1, 1]) {
    g.beginPath();
    g.moveTo(cx + s * mouthW, mouthY);
    g.lineTo(cx + s * mouthW * 1.5, mouthY - mouthH * (0.3 + r() * 0.3));
    g.stroke();
  }

  // --- hair: soaked ropes framing the face.
  //
  // Density is biased hard away from the centre. A full-width curtain of strands
  // buries the eyes and mouth, and then the scare reads as noise rather than as
  // a face — the two features doing all the work have to stay legible for the
  // six frames the player actually sees.
  g.strokeStyle = 'rgba(6,5,7,0.95)';
  for (let i = 0; i < 170; i++) {
    const x0 = cx + (r() - 0.5) * size * 0.98;
    const fromCentre = Math.abs(x0 - cx) / (size * 0.5);
    if (fromCentre < 0.34 && r() > 0.22) continue;   // keep the face clear
    const bow = (r() - 0.5) * size * 0.26;
    g.lineWidth = size * (0.002 + r() * 0.010) * (fromCentre < 0.4 ? 0.5 : 1);
    g.beginPath();
    g.moveTo(x0, -size * 0.05);
    g.bezierCurveTo(
      x0 + bow, size * 0.3,
      x0 - bow * 0.6, size * 0.6,
      x0 + bow * 0.4, size * (0.75 + r() * 0.35)
    );
    g.stroke();
  }

  // Re-punch the sockets and the mouth so no stray strand softens them.
  g.fillStyle = '#010101';
  for (const s of [-1, 1]) {
    g.beginPath();
    g.ellipse(cx + s * eyeDX, eyeY, fw * 0.135, fh * 0.108, 0, 0, TAU);
    g.fill();
  }
  if (variant === 1) {
    for (const s of [-1, 1]) {
      const ex = cx + s * eyeDX;
      g.fillStyle = 'rgba(226,222,214,0.92)';
      g.beginPath();
      g.ellipse(ex, eyeY, fw * 0.095, fh * 0.068, 0, 0, TAU);
      g.fill();
      g.fillStyle = '#0a0709';
      g.beginPath();
      g.arc(ex + fw * 0.012, eyeY, fw * 0.042, 0, TAU);
      g.fill();
    }
  }
  g.fillStyle = '#000';
  g.beginPath();
  g.ellipse(cx, mouthY, mouthW * 0.88, mouthH * 0.86, 0, 0, TAU);
  g.fill();
  drawTeeth(g, cx, mouthY, mouthW, mouthH, r);
  // A few strands catch the light and read as wet.
  g.strokeStyle = 'rgba(120,128,138,0.10)';
  for (let i = 0; i < 40; i++) {
    const x0 = cx + (r() - 0.5) * size * 0.9;
    g.lineWidth = size * 0.0025;
    g.beginPath();
    g.moveTo(x0, 0);
    g.quadraticCurveTo(x0 + (r() - 0.5) * 60, size * 0.45, x0 + (r() - 0.5) * 40, size * 0.9);
    g.stroke();
  }

  // Vignette to nail the eye to the centre of the face.
  const vig = g.createRadialGradient(cx, cy, size * 0.18, cx, cy, size * 0.62);
  vig.addColorStop(0, 'rgba(0,0,0,0)');
  vig.addColorStop(1, 'rgba(0,0,0,0.92)');
  g.fillStyle = vig;
  g.fillRect(0, 0, size, size);

  return cv;
}

// ------------------------------------------------------------- registry

export function buildTextures() {
  const walls = [];
  walls[0] = null;
  walls[T.WALLPAPER] = texWallpaper(1);
  walls[T.PHOTOS] = texPhotos(2);
  walls[T.CLOCK] = texClock(3);
  walls[T.TILE] = texTile(4);
  walls[T.WOOD] = texWood(5);
  walls[T.CONCRETE] = texConcrete(6);
  walls[T.MIRROR] = texMirror(7);
  walls[T.NURSERY] = texNursery(8);
  walls[T.DOOR] = texDoor(9, false);
  walls[T.DOOR_RED] = texDoor(10, true);
  walls[T.MEAT] = texMeat(11);

  return {
    walls,
    floor: texCarpet(20),
    ceiling: texCeiling(21),
    water: texWater(22),
    sprites: {
      figure_far: drawFigure(256, 0.0, 5),
      figure_mid: drawFigure(256, 0.45, 5),
      figure_near: drawFigure(256, 1.0, 5),
      child: drawChild(192, 12),
      radio: drawProp('radio', 128, 3),
      doll: drawProp('doll', 128, 4),
      battery: drawProp('battery', 128, 6),
      note: drawProp('note', 128, 7),
      handprint: drawProp('handprint', 128, 8),
    },
    faces: [drawScareFace(640, 0), drawScareFace(640, 1)],
  };
}
