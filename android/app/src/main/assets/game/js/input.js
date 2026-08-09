// Touch input.
//
// Left of the screen is a floating stick: it materialises wherever the thumb
// lands rather than at a fixed spot, which is the difference between a control
// that works and one that gets you killed. The right side is a look-drag.
// Pointer ids are tracked separately so both work at once.

import { clamp } from './util.js';

export class Input {
  constructor(el, opts = {}) {
    this.el = el;
    this.move = { x: 0, y: 0 };       // -1..1, y positive = forward
    this.look = { x: 0, y: 0 };       // consumed each frame
    this.sensitivity = opts.sensitivity ?? 1;
    this.invertY = false;

    this.stickId = null;
    this.lookId = null;
    this.stickOrigin = { x: 0, y: 0 };
    this.stickRadius = 0;

    this.stickEl = document.getElementById('stick');
    this.nubEl = document.getElementById('stick-nub');
    this.zoneEl = document.getElementById('stick-zone');

    this._bind();
  }

  _bind() {
    const opt = { passive: false };
    this.el.addEventListener('pointerdown', (e) => this._down(e), opt);
    this.el.addEventListener('pointermove', (e) => this._move(e), opt);
    this.el.addEventListener('pointerup', (e) => this._up(e), opt);
    this.el.addEventListener('pointercancel', (e) => this._up(e), opt);
    // Belt and braces: the CSS already disables these, but a stray long-press
    // context menu mid-chase would be unforgivable.
    this.el.addEventListener('contextmenu', (e) => e.preventDefault());
    this.el.addEventListener('touchstart', (e) => {
      if (e.touches.length > 1) e.preventDefault();
    }, opt);

    // Desktop fallback, purely so the game can be driven in a test harness.
    window.addEventListener('keydown', (e) => this._key(e, 1));
    window.addEventListener('keyup', (e) => this._key(e, 0));
    this.keys = {};
  }

  _key(e, v) {
    this.keys[e.key.toLowerCase()] = v;
    const k = this.keys;
    this.move.y = (k['w'] || k['arrowup'] ? 1 : 0) - (k['s'] || k['arrowdown'] ? 1 : 0);
    this.move.x = (k['d'] ? 1 : 0) - (k['a'] ? 1 : 0);
    if (k['arrowleft']) this.look.x -= 0.04;
    if (k['arrowright']) this.look.x += 0.04;
  }

  /** Buttons and menus sit above the canvas; ignore anything that starts on one. */
  _isUI(e) {
    const t = e.target;
    return t && t.closest && t.closest('button, input, .screen, .breath-btn');
  }

  _down(e) {
    if (this._isUI(e)) return;
    const rect = this.el.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    const leftHalf = x < rect.width * 0.46;

    if (leftHalf && this.stickId === null) {
      this.stickId = e.pointerId;
      this.stickOrigin.x = x;
      this.stickOrigin.y = y;
      this.stickRadius = Math.min(rect.width, rect.height) * 0.16;
      this.stickEl.style.left = x + 'px';
      this.stickEl.style.top = y + 'px';
      this.stickEl.classList.add('active');
      this._setNub(0, 0);
      e.preventDefault();
    } else if (!leftHalf && this.lookId === null) {
      this.lookId = e.pointerId;
      this.lookLast = { x, y };
      e.preventDefault();
    }
  }

  _move(e) {
    const rect = this.el.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    if (e.pointerId === this.stickId) {
      let dx = x - this.stickOrigin.x;
      let dy = y - this.stickOrigin.y;
      const d = Math.hypot(dx, dy);
      const r = this.stickRadius;
      if (d > r) { dx = (dx / d) * r; dy = (dy / d) * r; }
      this.move.x = clamp(dx / r, -1, 1);
      this.move.y = clamp(-dy / r, -1, 1);
      this._setNub(dx / r, dy / r);
      e.preventDefault();
    } else if (e.pointerId === this.lookId) {
      const dx = x - this.lookLast.x;
      const dy = y - this.lookLast.y;
      this.lookLast.x = x;
      this.lookLast.y = y;
      const k = 0.0042 * this.sensitivity;
      this.look.x += dx * k;
      this.look.y += dy * k * (this.invertY ? -1 : 1);
      e.preventDefault();
    }
  }

  _up(e) {
    if (e.pointerId === this.stickId) {
      this.stickId = null;
      this.move.x = 0;
      this.move.y = 0;
      this.stickEl.classList.remove('active');
      this._setNub(0, 0);
    } else if (e.pointerId === this.lookId) {
      this.lookId = null;
    }
  }

  _setNub(nx, ny) {
    this.nubEl.style.transform =
      `translate(${(nx * 34).toFixed(1)}%, ${(ny * 34).toFixed(1)}%)`;
  }

  /** Read and clear the look delta accumulated since the last frame. */
  consumeLook() {
    const l = { x: this.look.x, y: this.look.y };
    this.look.x = 0;
    this.look.y = 0;
    return l;
  }

  release() {
    this.stickId = null;
    this.lookId = null;
    this.move.x = 0;
    this.move.y = 0;
    this.look.x = 0;
    this.look.y = 0;
    this.stickEl.classList.remove('active');
  }
}
