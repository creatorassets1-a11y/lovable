// Boot, game loop, and the facade the story script drives.
//
// Everything with state lives on `Game`. The story file calls into `this.g`,
// which is a deliberately small surface: say, sfx, scare, and a handful of world
// pokes. Keeping that boundary narrow is what makes the nine loops readable.

import { AudioEngine } from './audio.js';
import { Entity, STATE } from './entity.js';
import { Input } from './input.js';
import { Renderer } from './render.js';
import { UI } from './ui.js';
import { VO } from './vo-manifest.js';
import { buildTextures } from './textures.js';
import { LOOPS, DEATH_LINES, ENDINGS } from './story.js';
import {
  buildWorld, applyLoop, updateDoors, moveWithCollision, usableNear,
  openDoor, closeDoor, addProp, removeProp, NOTES,
} from './world.js';
import { clamp, approach, lerp, pick, rng, TAU } from './util.js';

const SAVE_KEY = 'ninthloop.save.v1';
const OPT_KEY = 'ninthloop.opts.v1';

const WALK = 1.55;
const RUN = 2.75;
const TORCH_LIFE = 165;      // seconds of continuous use on a full charge

class Game {
  constructor() {
    this.ui = new UI(VO);
    this.audio = new AudioEngine();
    this.state = 'loading';
    this.rand = rng(Date.now() & 0xffff);

    this.opts = {
      volume: 0.9, sens: 1.0, haptics: true, subs: true,
      invertY: false, reducedFlash: false, quality: 1.0,
    };
    this.loadOptions();

    this.player = {
      x: 0, y: 0, a: 0, pitch: 0,
      torch: 0, torchOn: false, battery: 1,
      sanity: 1, running: false,
      hiding: false, hideSpot: null, breath: 1, holding: false, betrayed: false,
      bob: 0, bobPhase: 0, stepAcc: 0, wasMoving: false,
    };

    this.fx = {
      shake: 0, roll: 0, chroma: 0, glitch: 0, redshift: 0,
      blind: 1, flash: 0, blood: 0, grain: 0.1, vignette: 0,
    };

    this.loop = 1;
    this.loopTime = 0;
    this.tension = 0;
    this.timers = [];
    this.fired = new Set();
    this.transient = [];       // temporary sprites (glimpses)
    this.finale = false;
    this.paused = false;
    this.lastFrame = 0;
    this.scare = null;
  }

  // ================================================================= boot

  async boot() {
    this.canvas = document.getElementById('view');
    this.scareCanvas = document.getElementById('scare');
    this.scareCtx = this.scareCanvas.getContext('2d');

    this.ui.show('load');
    this.ui.progress(0.02, 'building the hall…');
    await frame();

    this.tex = buildTextures();
    this.ui.progress(0.30, 'hanging the photographs…');
    await frame();

    this.world = buildWorld();
    this.renderer = new Renderer(this.canvas, this.tex);
    this.entity = new Entity(this.world);
    this.entity.onCatch = () => this.caught();
    this.entity.onStep = (d) => this.entityStep(d);
    this.entity.onSpotted = (d) => this.entitySpotted(d);

    this.input = new Input(document.getElementById('app'), { sensitivity: this.opts.sens });
    this.input.invertY = this.opts.invertY;

    this.applyOptions();
    this.resize();
    window.addEventListener('resize', () => this.resize());
    if (window.visualViewport) {
      window.visualViewport.addEventListener('resize', () => this.resize());
    }

    this.ui.progress(0.42, 'tuning the radio…');
    await frame();

    await this.audio.load((f) => {
      this.ui.progress(0.42 + f * 0.55, f < 0.5 ? 'tuning the radio…' : 'filling the channel…');
    });

    this.ui.progress(1, 'ready');
    this.wireUI();

    const save = this.loadSave();
    this.ui.setContinueVisible(!!save && save.loop > 1);

    await wait(400);
    this.ui.show('title');
    this.state = 'title';
    this.startLoopRAF();
  }

  // ============================================================== options

  loadOptions() {
    try {
      const raw = localStorage.getItem(OPT_KEY);
      if (raw) Object.assign(this.opts, JSON.parse(raw));
    } catch { /* first run, or storage disabled */ }
  }

  saveOptions() {
    try { localStorage.setItem(OPT_KEY, JSON.stringify(this.opts)); } catch { /* ignore */ }
  }

  applyOptions() {
    this.audio.setMasterVolume(this.opts.volume);
    this.ui.subsEnabled = this.opts.subs;
    if (!this.opts.subs) this.ui.clearSubtitle();
    if (this.input) {
      this.input.sensitivity = this.opts.sens;
      this.input.invertY = this.opts.invertY;
    }
    if (this.renderer) this.renderer.setQuality(this.opts.quality);
  }

  loadSave() {
    try { return JSON.parse(localStorage.getItem(SAVE_KEY) || 'null'); } catch { return null; }
  }

  save() {
    try { localStorage.setItem(SAVE_KEY, JSON.stringify({ loop: this.loop })); } catch { /* ignore */ }
  }

  // =================================================================== UI

  wireUI() {
    const on = (id, ev, fn) => {
      const el = document.getElementById(id);
      if (el) el.addEventListener(ev, fn);
    };

    on('btn-start', 'click', () => this.begin(1));
    on('btn-continue', 'click', () => {
      const s = this.loadSave();
      this.begin(s ? s.loop : 1);
    });

    on('btn-opts', 'click', () => { this.optsFrom = 'title'; this.ui.show('opts'); });
    on('btn-pause-opts', 'click', () => { this.optsFrom = 'pause'; this.ui.show('opts'); });
    on('btn-opts-back', 'click', () => this.ui.show(this.optsFrom || 'title'));
    on('btn-wipe', 'click', () => {
      try { localStorage.removeItem(SAVE_KEY); } catch { /* ignore */ }
      this.ui.setContinueVisible(false);
      this.ui.progress(1, 'erased');
    });

    on('btn-menu', 'click', () => this.pause());
    on('btn-resume', 'click', () => this.unpause());
    on('btn-quit', 'click', () => this.abandon());
    on('btn-retry', 'click', () => this.restartLoop());
    on('btn-end-ok', 'click', () => this.afterEnding());

    on('btn-torch', 'click', () => this.toggleTorch());
    on('btn-run', 'pointerdown', () => { this.player.running = true; this.ui.setRunning(true); });
    on('btn-run', 'pointerup', () => { this.player.running = false; this.ui.setRunning(false); });
    on('btn-run', 'pointercancel', () => { this.player.running = false; this.ui.setRunning(false); });
    on('btn-use', 'click', () => this.useAction());

    on('btn-breath', 'pointerdown', () => { this.player.holding = true; });
    on('btn-breath', 'pointerup', () => { this.player.holding = false; });
    on('btn-breath', 'pointercancel', () => { this.player.holding = false; });

    const bind = (id, ev, fn) => {
      const el = document.getElementById(id);
      if (el) el.addEventListener(ev, () => fn(el));
    };
    bind('opt-vol', 'input', (el) => { this.opts.volume = el.value / 100; this.applyOptions(); this.saveOptions(); });
    bind('opt-sens', 'input', (el) => { this.opts.sens = el.value / 100; this.applyOptions(); this.saveOptions(); });
    bind('opt-qual', 'input', (el) => { this.opts.quality = el.value / 100; this.applyOptions(); this.saveOptions(); });
    bind('opt-haptic', 'change', (el) => { this.opts.haptics = el.checked; this.saveOptions(); });
    bind('opt-subs', 'change', (el) => { this.opts.subs = el.checked; this.applyOptions(); this.saveOptions(); });
    bind('opt-invy', 'change', (el) => { this.opts.invertY = el.checked; this.applyOptions(); this.saveOptions(); });
    bind('opt-flash', 'change', (el) => { this.opts.reducedFlash = el.checked; this.saveOptions(); });

    // Reflect stored options into the controls.
    const set = (id, v, prop = 'value') => {
      const el = document.getElementById(id);
      if (el) el[prop] = v;
    };
    set('opt-vol', Math.round(this.opts.volume * 100));
    set('opt-sens', Math.round(this.opts.sens * 100));
    set('opt-qual', Math.round(this.opts.quality * 100));
    set('opt-haptic', this.opts.haptics, 'checked');
    set('opt-subs', this.opts.subs, 'checked');
    set('opt-invy', this.opts.invertY, 'checked');
    set('opt-flash', this.opts.reducedFlash, 'checked');

    // Android back button and lifecycle, forwarded from MainActivity.
    window.__onNativeBack = () => this.onBack();
    window.__onNativePause = () => this.pause(true);
    window.__onNativeResume = () => { this.audio.resume(); };
    document.addEventListener('visibilitychange', () => {
      if (document.hidden) this.pause(true);
    });
  }

  onBack() {
    if (this.ui.noteOpen) { this.ui.hideNote(); return; }
    if (this.state === 'playing') this.pause();
    else if (this.ui.current === 'opts') this.ui.show(this.optsFrom || 'title');
    else if (this.ui.current === 'pause') this.unpause();
    else if (this.ui.current === 'title' && window.AndroidHost) window.AndroidHost.quit();
  }

  resize() {
    const vv = window.visualViewport;
    const w = Math.round(vv ? vv.width : window.innerWidth);
    const h = Math.round(vv ? vv.height : window.innerHeight);
    // The world buffer is a fixed small size whatever the panel is, so the only
    // thing display resolution buys is a sharper upscale — and the per-pixel
    // grain, scanline and vignette passes are paid in full at that resolution.
    // Cap total backing-store area so a 1440p phone or a tablet does not spend
    // its whole frame compositing overlays.
    const MAX_PIXELS = 2.2e6;
    let dpr = clamp(window.devicePixelRatio || 1, 1, 2.5);
    const area = w * h * dpr * dpr;
    if (area > MAX_PIXELS) dpr = Math.max(0.8, dpr * Math.sqrt(MAX_PIXELS / area));

    // --u drives every size in the stylesheet.
    document.getElementById('app').style.setProperty('--u', `${Math.min(w, h) / 100}px`);

    if (this.renderer) this.renderer.resize(w, h, dpr);
    const sdpr = Math.min(dpr, 1.4);
    this.scareCanvas.width = Math.round(w * sdpr);
    this.scareCanvas.height = Math.round(h * sdpr);
  }

  // ============================================================== flow

  async begin(startLoop) {
    await this.audio.resume();
    this.audio.init();
    this.loop = clamp(startLoop, 1, 9);
    this.state = 'playing';
    this.ui.setHud(true);
    this.enterLoop(this.loop, true);
  }

  enterLoop(n, showCard = true) {
    this.loop = clamp(n, 1, 9);
    this.save();
    this.clearTimers();
    this.fired.clear();
    this.transient.length = 0;
    this.finale = false;
    this.tension = 0;
    this.loopTime = 0;
    this.scare = null;

    applyLoop(this.world, this.loop);
    const s = this.world.spawn;
    Object.assign(this.player, {
      x: s.x, y: s.y, a: s.a, pitch: 0,
      hiding: false, hideSpot: null, breath: 1, holding: false, betrayed: false,
      running: false, bob: 0, bobPhase: 0,
    });
    // Battery and composure both recover between loops, but never fully.
    this.player.battery = clamp(0.55 + 0.45 / this.loop, 0.3, 1);
    this.player.sanity = clamp(1 - this.loop * 0.045, 0.55, 1);
    this.player.torchOn = true;

    this.entity.reset();
    this.entity.setAggression(0);

    this.ui.setLoop(this.loop);
    this.ui.setClock('11:59');
    this.ui.setRunning(false);
    this.ui.setTorchOn(this.player.torchOn);
    this.ui.setBreath(false, 1);
    this.ui.clearSubtitle();
    this.ui.objective('');

    const script = LOOPS[this.loop];
    this.audio.stopAllLoops(0.8);
    this.audio.setLoop(script.ambientLoop || 'drone_calm', 0.55, 3.0, 'music');
    this.audio.setLoop('heart_slow', 0.18, 2.0);
    this.audio.warm(this.upcomingVoiceIds(script));

    this.fx.blind = 1;
    this.fx.roll = this.world.roll;

    if (showCard) {
      this.ui.loopCard(this.loop, script.caption);
      this.state = 'card';
      this.clearTimers();
      this.after(2.8, () => {
        this.ui.hideAll();
        this.state = 'playing';
        this.after(0.1, () => { if (script.objective) this.g.objective(script.objective); });
      }, true);
    } else {
      this.ui.hideAll();
      this.state = 'playing';
    }
  }

  upcomingVoiceIds(script) {
    // Cheap static scan: pull every quoted id out of the loop's source so the
    // decoder is warm before the line is needed.
    const ids = new Set();
    const src = script.events.map((e) => String(e.run)).join(';');
    for (const m of src.matchAll(/'([armew]_\d\d)'/g)) ids.add(m[1]);
    return [...ids];
  }

  advanceLoop() {
    if (this.loop >= 9) return;      // loop 9 only ends through the finale
    this.audio.play('door_creak2', { vol: 0.7 });
    this.fx.blind = 1;
    this.state = 'card';
    this.after(0.9, () => this.enterLoop(this.loop + 1), true);
  }

  pause(silent = false) {
    if (this.state !== 'playing') return;
    this.state = 'paused';
    this.input.release();
    this.player.running = false;
    this.player.holding = false;
    this.audio.suspend();
    if (!silent) this.ui.show('pause');
    else this.ui.show('pause');
  }

  unpause() {
    if (this.state !== 'paused') return;
    this.audio.resume();
    this.ui.hideAll();
    this.state = 'playing';
    this.lastFrame = performance.now();
  }

  abandon() {
    this.state = 'title';
    this.audio.stopAllLoops(0.6);
    this.audio.stopVoice();
    this.audio.resume();
    this.ui.setHud(false);
    this.ui.setContinueVisible(true);
    this.ui.show('title');
  }

  caught() {
    if (this.state !== 'playing') return;
    this.state = 'dying';
    this.clearTimers();
    this.doScare({
      face: 1,
      sound: 'scream_mara',
      vo: 'm_13',
      shake: 1.0,
      dur: 2.0,
      haptic: [0, 600, 80, 400, 60, 300],
    });
    this.after(2.4, () => {
      this.audio.stopAllLoops(0.8);
      this.ui.setHud(false);
      this.ui.deathCard(pick(DEATH_LINES, this.rand));
      this.state = 'dead';
    }, true);
  }

  restartLoop() {
    this.ui.setHud(true);
    this.state = 'playing';
    this.enterLoop(this.loop, false);
    this.fx.blind = 1;
  }

  finish(which) {
    this.state = 'ending';
    this.clearTimers();
    this.audio.stopAllLoops(1.2);
    this.audio.stopVoice();
    this.ui.setHud(false);
    this.fx.blind = 0;

    if (which === 'stay') {
      this.audio.setLoop('ending', 0.7, 3.0, 'music');
      this.audio.play('sub_boom', { vol: 0.6 });
      this.haptic(700, 120);
    } else {
      this.audio.setLoop('drone_dread', 0.5, 2.0, 'music');
      this.audio.play('door_creak1', { vol: 0.6 });
    }
    this.endingKind = which;
    this.after(1.2, () => this.ui.ending(ENDINGS[which]), true);
    try { localStorage.removeItem(SAVE_KEY); } catch { /* ignore */ }
  }

  afterEnding() {
    if (this.endingKind === 'door') {
      // "Again" means again. Loop 9 restarts, exactly as promised.
      this.ui.setHud(true);
      this.state = 'playing';
      this.enterLoop(9, true);
    } else {
      this.audio.stopAllLoops(1.5);
      this.ui.setHud(false);
      this.ui.setContinueVisible(false);
      this.ui.show('title');
      this.state = 'title';
    }
  }

  // =============================================================== timers

  after(seconds, fn, system = false) {
    this.timers.push({ t: seconds, fn, system });
  }

  clearTimers() {
    this.timers.length = 0;
  }

  // =============================================================== haptics

  haptic(ms, amplitude = 180) {
    if (!this.opts.haptics) return;
    try {
      if (window.AndroidHost && window.AndroidHost.vibrate) {
        window.AndroidHost.vibrate(ms | 0, amplitude | 0);
      } else if (navigator.vibrate) {
        navigator.vibrate(ms | 0);
      }
    } catch { /* no vibrator */ }
  }

  hapticPattern(arr) {
    if (!this.opts.haptics) return;
    try {
      if (window.AndroidHost && window.AndroidHost.vibratePattern) {
        window.AndroidHost.vibratePattern(arr.join(','));
      } else if (navigator.vibrate) {
        navigator.vibrate(arr);
      }
    } catch { /* no vibrator */ }
  }

  // ============================================================ the facade

  get g() {
    if (this._g) return this._g;
    const self = this;
    this._g = {
      after: (t, fn) => self.after(t, fn),

      say(id, opts = {}) {
        self.audio.duck(0.45, 0.25);
        self.audio.speak(id, opts);
        self.ui.subtitle(id);
        const e = VO[id];
        self.after((e ? e.s || e.d : 2) + 0.5, () => self.audio.unduck(1.2));
        // Mara speaking is physically felt.
        if (e && e.c === 'mara') self.haptic(90, 70);
      },

      sfx: (name, opts = {}) => self.audio.play(name, opts),
      sfxAt: (name, x, y, opts = {}) => self.audio.playAt(name, x, y, self.player, opts),
      loopSfx: (name, vol, time = 2.0, bus = 'amb') => self.audio.setLoop(name, vol, time, bus),

      objective: (text) => self.ui.objective(text),
      showNote: (i) => {
        self.state = 'reading';
        self.input.release();
        self.ui.showNote(NOTES[clamp(i, 0, NOTES.length - 1)], () => {
          self.state = 'playing';
          self.lastFrame = performance.now();
        });
      },

      openDoor: (id, instant) => openDoor(self.world, id, instant),
      closeDoor: (id, instant) => closeDoor(self.world, id, instant),
      killLights: () => {
        self.world.lightsOn = false;
        self.world.ambient = 0.13;
        self.world.fog = 0.19;
        self.player.torchOn = true;
        self.ui.setTorchOn(true);
      },

      tension: (v) => { self.tension = clamp(v, 0, 1); },
      shake: (amount, seconds) => {
        self.fx.shake = Math.max(self.fx.shake, amount);
        self.after(seconds, () => { self.fx.shake = 0; });
      },
      glitch: (amount, seconds) => {
        if (self.opts.reducedFlash) amount *= 0.35;
        self.fx.glitch = Math.max(self.fx.glitch, amount);
        self.after(seconds, () => { self.fx.glitch = 0; });
      },
      haptic: (ms, amp) => self.haptic(ms, amp),

      scare: (cfg) => self.doScare(cfg),

      spawnEntity: (aggression) => {
        self.entity.setAggression(aggression);
        self.entity.spawnAway(self.player, 9);
      },
      entityLurk: (x, y) => {
        self.entity.x = x; self.entity.y = y;
        self.entity.state = STATE.LURK;
        self.entity.timer = 6;
      },
      vanishEntity: () => {
        self.entity.reset();
        self.audio.play('static', { vol: 0.35 });
      },
      setEntityAggression: (v) => {
        self.entity.setAggression(v);
        if (v === 0) self.entity.state = STATE.LURK;
      },

      glimpseChild: (x, y, seconds) => {
        self.transient.push({
          x, y, canvas: self.tex.sprites.child, scale: 0.72, yOff: 0,
          light: 1.2, alpha: 0.9, ttl: seconds,
        });
        self.audio.playAt('drip2', x, y, self.player, { vol: 0.5 });
      },

      stageFinale: () => self.stageFinale(),
    };
    return this._g;
  }

  // ============================================================== scares

  doScare(cfg) {
    const reduced = this.opts.reducedFlash;
    this.scare = {
      t: 0,
      dur: cfg.dur ?? 1.4,
      face: this.tex.faces[cfg.face ?? 0],
      seed: this.rand() * 1000,
    };
    if (cfg.sound) this.audio.play(cfg.sound, { vol: 1.0, verb: 0.25 });
    this.audio.impact(0.85);
    if (cfg.vo) {
      this.audio.speak(cfg.vo, { vol: 1.0 });
      this.ui.subtitle(cfg.vo);
    }
    this.fx.shake = Math.max(this.fx.shake, cfg.shake ?? 0.9);
    this.fx.chroma = reduced ? 0.3 : 1.0;
    this.fx.glitch = reduced ? 0.3 : 0.9;
    this.after(cfg.dur ?? 1.4, () => {
      this.fx.shake = 0;
      this.fx.chroma = 0;
      this.fx.glitch = 0;
    });
    if (cfg.haptic) this.hapticPattern(cfg.haptic);
    else this.haptic(400, 220);

    this.player.sanity = clamp(this.player.sanity - 0.22, 0, 1);
    this.audio.setLoop('heart_fast', 0.6, 0.4);
    this.after(9, () => this.audio.setLoop('heart_fast', 0, 4));
  }

  /** Paint the face over the whole screen, jittering hard. */
  drawScare(dt) {
    const s = this.scare;
    const c = this.scareCanvas;
    const g = this.scareCtx;
    if (!s) {
      if (this._scareClear) return;
      g.clearRect(0, 0, c.width, c.height);
      this._scareClear = true;
      return;
    }
    this._scareClear = false;

    s.t += dt;
    const f = s.t / s.dur;
    if (f >= 1) {
      this.scare = null;
      g.clearRect(0, 0, c.width, c.height);
      return;
    }

    const W = c.width, H = c.height;
    g.clearRect(0, 0, W, H);

    // Hard cut in, ragged fade out.
    const alpha = f < 0.06 ? 1 : clamp(1 - (f - 0.06) / 0.94, 0, 1);
    const strobe = this.opts.reducedFlash ? 1 : (Math.random() < 0.10 ? 0.35 : 1);

    // Black out whatever is behind first, so the face is never competing with
    // the corridor for the player's attention.
    g.globalAlpha = alpha;
    g.fillStyle = '#000';
    g.fillRect(0, 0, W, H);

    g.save();
    g.globalAlpha = alpha * strobe;

    // The face pushes closer over the duration, never quite settling. Sized off
    // the short axis: scaling to cover a wide landscape screen crops the frame
    // down to a band of hair with the eyes and mouth outside it.
    const zoom = 1.0 + f * 0.40 + Math.random() * 0.04;
    const size = Math.max(H * 1.16, W * 0.60) * zoom;
    const jx = (Math.random() - 0.5) * W * 0.05 * (1 - f * 0.5);
    const jy = (Math.random() - 0.5) * H * 0.05 * (1 - f * 0.5);

    g.translate(W / 2 + jx, H / 2 + jy);
    g.rotate((Math.random() - 0.5) * 0.08);
    g.drawImage(s.face, -size / 2, -size / 2, size, size);
    g.restore();

    if (!this.opts.reducedFlash && Math.random() < 0.22) {
      g.fillStyle = `rgba(255,240,235,${0.04 + Math.random() * 0.07})`;
      g.fillRect(0, 0, W, H);
    }
    g.fillStyle = `rgba(90,4,2,${(0.08 + Math.random() * 0.08) * alpha})`;
    g.fillRect(0, 0, W, H);
  }

  // ============================================================ finale

  stageFinale() {
    if (this.finale) return;
    this.finale = true;
    this.clearTimers();
    this.entity.setAggression(0);
    this.entity.state = STATE.LURK;
    // She stands in the middle of the return hall, between you and everything.
    this.entity.x = 14.5;
    this.entity.y = 16.5;

    this.audio.setLoop('whisper_bed', 0.25, 2);
    this.audio.setLoop('drone_chaos', 0.45, 2);
    this.ui.setClock('12:00');
    this.g.say('a_13');

    this.after(6, () => {
      this.g.say('a_19');
      this.ui.objective('Open the door. Or turn around and go back to her.');
    });

    // Turning around and reaching her is the other ending.
    this.stayProp = addProp(this.world, 'stay', this.entity.x, this.entity.y, {
      sprite: 'figure_near', scale: 0.01, usable: true, label: 'STAY', range: 1.8,
    });
  }

  // ============================================================== actions

  toggleTorch() {
    if (this.state !== 'playing') return;
    if (this.player.battery <= 0.001) {
      this.audio.play('bone', { vol: 0.4, rate: 1.4 });
      this.player.torchOn = false;
      this.ui.setTorchOn(false);
      return;
    }
    this.player.torchOn = !this.player.torchOn;
    this.ui.setTorchOn(this.player.torchOn);
    this.audio.play('static', { vol: 0.18, rate: 2.2 });
    this.haptic(18, 60);
  }

  useAction() {
    if (this.state !== 'playing') return;
    const p = this.player;

    if (p.hiding) { this.leaveHiding(); return; }

    const closet = this.closetHere();
    if (closet) { this.enterHiding(closet); return; }

    const prop = usableNear(this.world, p.x, p.y, p.a);
    if (!prop) return;

    this.haptic(22, 90);

    if (prop.kind === 'battery') {
      p.battery = clamp(p.battery + 0.4, 0, 1);
      this.audio.play('bone', { vol: 0.3, rate: 1.8 });
      removeProp(this.world, prop);
      return;
    }
    if (prop.kind === 'stay') {
      this.finish('stay');
      return;
    }

    prop.used = prop.kind !== 'radio';
    this.runEvents((e) => e.use === prop.kind, prop);
  }

  closetHere() {
    const p = this.player;
    for (const d of this.world.doors) {
      if (!d.closet) continue;
      // The closet interior is the tile behind the door.
      const ix = d.x, iy = d.y + 1;
      if (Math.hypot(p.x - (ix + 0.5), p.y - (iy + 0.5)) < 0.7) return d;
    }
    return null;
  }

  enterHiding(door) {
    const p = this.player;
    p.hiding = true;
    p.hideSpot = door;
    p.betrayed = false;
    p.breath = 1;
    p.x = door.x + 0.5;
    p.y = door.y + 1.5;
    p.a = -Math.PI / 2;      // face the door
    closeDoor(this.world, door.id);
    door.target = 0;
    this.audio.play('door_creak2', { vol: 0.5, rate: 1.2 });
    this.audio.setMuffle(0.55);
    this.audio.setLoop('breath_panic', 0.5, 0.6);
    this.ui.setBreath(true, 1);
    this.ui.objective('Hold your breath when she is close.');
  }

  leaveHiding() {
    const p = this.player;
    p.hiding = false;
    p.holding = false;
    p.betrayed = false;
    if (p.hideSpot) openDoor(this.world, p.hideSpot.id);
    p.hideSpot = null;
    this.audio.setMuffle(0);
    this.audio.setLoop('breath_panic', 0, 1.0);
    this.ui.setBreath(false, 1);
    this.ui.objective('');
  }

  entityStep(dist) {
    const wet = this.world.waterLevel > 0.05;
    const n = 1 + ((this.rand() * 4) | 0);
    this.audio.playAt(wet ? `step_wet${n}` : `step_dry${n}`,
      this.entity.x, this.entity.y, this.player,
      { vol: 0.85, rate: 0.82 + this.rand() * 0.12, range: 16 });
    if (dist < 3.5) this.haptic(14, clamp(90 - dist * 18, 20, 90) | 0);
  }

  entitySpotted(dist) {
    this.audio.play(dist < 6 ? 'stinger_b' : 'stinger_c', { vol: 0.45 });
    this.player.sanity = clamp(this.player.sanity - 0.06, 0, 1);
    this.haptic(60, 90);
  }

  // ========================================================== event pump

  runEvents(match, prop) {
    const script = LOOPS[this.loop];
    if (!script) return;
    for (let i = 0; i < script.events.length; i++) {
      const e = script.events[i];
      if (this.fired.has(i)) continue;
      if (!match(e)) continue;
      this.fired.add(i);
      try {
        e.run(this.g, prop);
      } catch (err) {
        console.error('story event failed', this.loop, i, err);
      }
    }
  }

  pumpStory(dt) {
    const script = LOOPS[this.loop];
    if (!script) return;
    const p = this.player;

    this.runEvents((e) => e.t !== undefined && this.loopTime >= e.t);
    this.runEvents((e) => {
      if (!e.zone) return false;
      const [x0, y0, x1, y1] = e.zone;
      return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
    });
  }

  // ============================================================== update

  update(dt) {
    const p = this.player;
    const w = this.world;

    // Timers run in every state so scares and cards can resolve.
    for (let i = this.timers.length - 1; i >= 0; i--) {
      const t = this.timers[i];
      t.t -= dt;
      if (t.t <= 0) {
        this.timers.splice(i, 1);
        try { t.fn(); } catch (e) { console.error('timer failed', e); }
      }
    }

    if (this.state !== 'playing') {
      this.fx.blind = approach(this.fx.blind, this.state === 'card' ? 1 : this.fx.blind, 4, dt);
      updateDoors(w, dt);
      return;
    }

    this.loopTime += dt;
    this.fx.blind = approach(this.fx.blind, 0, 1.6, dt);

    // ---- look
    const look = this.input.consumeLook();
    p.a += look.x;
    p.pitch = clamp(p.pitch - look.y * 0.9, -0.42, 0.42);
    p.a = ((p.a % TAU) + TAU) % TAU;

    // ---- move
    let moving = false;
    if (!p.hiding) {
      const mv = this.input.move;
      const mag = Math.hypot(mv.x, mv.y);
      if (mag > 0.06) {
        moving = true;
        const speed = (p.running ? RUN : WALK) * clamp(mag, 0, 1)
          * (w.waterLevel > 0.25 ? 0.78 : 1)          // wading is slower
          * lerp(0.72, 1, p.sanity);                  // so is falling apart
        const fx = Math.cos(p.a), fy = Math.sin(p.a);
        const dx = (fx * mv.y - fy * mv.x) * speed * dt;
        const dy = (fy * mv.y + fx * mv.x) * speed * dt;
        moveWithCollision(w, p, dx, dy);
      }
    }

    // ---- head bob and footsteps
    if (moving) {
      const rate = p.running ? 9.5 : 6.2;
      p.bobPhase += dt * rate;
      p.bob = Math.sin(p.bobPhase) * 0.012;
      p.stepAcc += dt * rate;
      if (p.stepAcc > Math.PI) {
        p.stepAcc -= Math.PI;
        const wet = w.waterLevel > 0.03;
        const n = 1 + ((this.rand() * 4) | 0);
        this.audio.play(wet ? `step_wet${n}` : `step_dry${n}`, {
          vol: p.running ? 0.34 : 0.24,
          rate: 0.94 + this.rand() * 0.14,
          verb: 0.3,
        });
      }
    } else {
      p.bob = approach(p.bob, 0, 6, dt);
    }

    // ---- torch
    if (p.torchOn && p.battery > 0) {
      p.battery = clamp(p.battery - dt / TORCH_LIFE, 0, 1);
      if (p.battery <= 0) {
        p.torchOn = false;
        this.ui.setTorchOn(false);
        this.audio.play('static', { vol: 0.3, rate: 0.7 });
        this.haptic(200, 120);
      }
    }
    // Flicker hard once the cell is nearly flat.
    const flicker = p.battery < 0.18 && p.torchOn
      ? (Math.random() < 0.10 ? 0.25 : 1) * (0.75 + Math.random() * 0.25)
      : 1;
    p.torch = approach(p.torch, p.torchOn ? 0.95 * flicker : 0, 12, dt);

    // ---- hiding
    if (p.hiding) {
      if (p.holding) {
        p.breath = clamp(p.breath - dt * 0.30, 0, 1);
        if (p.breath <= 0) {
          // Out of air. You gasp, and she hears it.
          p.holding = false;
          p.betrayed = true;
          this.audio.play('scream_child', { vol: 0.12, rate: 0.6 });
          this.audio.setLoop('breath_panic', 0.75, 0.2);
          this.after(3, () => { p.betrayed = false; });
        }
      } else {
        p.breath = clamp(p.breath + dt * 0.22, 0, 1);
      }
      const near = Math.hypot(this.entity.x - p.x, this.entity.y - p.y);
      if (this.entity.state !== STATE.GONE && near < 3.0 && !p.holding) p.betrayed = true;
      this.ui.setBreath(true, p.breath);
    }

    // ---- doors, props, entity
    updateDoors(w, dt);

    for (let i = this.transient.length - 1; i >= 0; i--) {
      this.transient[i].ttl -= dt;
      if (this.transient[i].ttl <= 0) this.transient.splice(i, 1);
    }

    this.entity.update(dt, p, {
      onHuntStart: (d) => {
        this.audio.play('stinger_a', { vol: 0.7 });
        this.audio.setLoop('heart_fast', 0.55, 0.5);
        this.audio.setLoop('breath_panic', 0.4, 0.8);
        this.haptic(260, 200);
        this.ui.objective('RUN');
      },
      onVanish: () => this.audio.play('static', { vol: 0.28 }),
    });

    // ---- sanity
    const dark = p.torch < 0.05 && !w.lightsOn;
    const eDist = this.entity.state === STATE.GONE
      ? 99 : Math.hypot(this.entity.x - p.x, this.entity.y - p.y);
    let drain = 0;
    if (dark) drain += 0.030;
    if (eDist < 8) drain += (8 - eDist) * 0.012;
    if (this.entity.state === STATE.HUNT) drain += 0.05;
    if (!dark && eDist > 10) drain -= 0.022;      // recovers, slowly
    p.sanity = clamp(p.sanity - drain * dt, 0, 1);

    // ---- feedback driven by sanity, entity distance and scripted tension
    const stress = clamp(
      (1 - p.sanity) * 0.6 + this.tension * 0.35 + clamp((9 - eDist) / 9, 0, 1) * 0.5,
      0, 1);

    this.fx.vignette = stress * 0.35;
    this.fx.redshift = clamp((1 - p.sanity - 0.35) * 0.9, 0, 0.55);
    this.fx.grain = 0.08 + stress * 0.14;
    this.fx.blood = clamp((1 - p.sanity - 0.55) * 0.8, 0, 0.4);
    this.fx.roll = w.roll + Math.sin(this.loopTime * 0.7) * stress * 0.012;

    this.audio.setLoopRate('heart_slow', 1 + stress * 0.55);
    this.audio.setLoop('heart_slow', 0.16 + stress * 0.30, 0.8);
    this.audio.setMuffle(p.hiding ? 0.55 : (1 - p.sanity) * 0.35);
    if (this.entity.state !== STATE.HUNT && !p.hiding) {
      this.audio.setLoop('breath_panic', clamp((1 - p.sanity - 0.4) * 1.2, 0, 0.5), 1.5);
    }

    // ---- HUD
    this.ui.setMeters(p.battery, p.sanity);
    if (!p.hiding) {
      const closet = this.closetHere();
      const prop = closet ? null : usableNear(w, p.x, p.y, p.a);
      this.ui.setUseButton(closet ? 'HIDE' : (prop ? prop.label : ''));
    } else {
      this.ui.setUseButton('LEAVE');
    }

    // ---- story and loop closure
    this.pumpStory(dt);

    const lt = w.loopTrigger;
    if (!this.finale && p.x >= lt.x0 && p.x <= lt.x1 && p.y >= lt.y0 && p.y <= lt.y1) {
      this.advanceLoop();
    }
    if (this.finale && p.x < 3.6 && p.y > 15.4 && p.y < 18) {
      this.finish('door');
    }
  }

  // ============================================================== render

  frame(now) {
    const dt = clamp((now - this.lastFrame) / 1000, 0.001, 0.05);
    this.lastFrame = now;
    const t0 = performance.now();

    this.update(dt);

    if (this.state === 'title' || this.state === 'loading') {
      this.drawScare(dt);
      return;
    }

    const p = this.player;
    const sprites = [];

    const es = this.entity.sprite(this.tex, p);
    if (es) sprites.push(es);
    for (const t of this.transient) sprites.push(t);
    for (const prop of this.world.props) {
      const canvas = this.tex.sprites[prop.sprite];
      if (!canvas || prop.scale < 0.02) continue;
      sprites.push({
        x: prop.x, y: prop.y, canvas,
        scale: prop.scale, yOff: prop.yOff,
        light: 1, alpha: 1,
      });
    }

    const cam = {
      x: p.x, y: p.y, a: p.a, pitch: p.pitch, bob: p.bob,
      torch: p.torch,
      torchWidth: p.hiding ? 0.26 : 0.42,
    };

    this.renderer.render(cam, this.world, sprites, this.fx,
                         now / 1000, this._lastCost || 16);
    this.drawScare(dt);
    this._lastCost = performance.now() - t0;
  }

  startLoopRAF() {
    this.lastFrame = performance.now();
    const tick = (now) => {
      try {
        this.frame(now);
      } catch (e) {
        console.error('frame failed', e);
      }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }
}

const frame = () => new Promise((r) => requestAnimationFrame(() => r()));
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

const game = new Game();
window.__game = game;      // handy from `adb logcat`-adjacent debugging
game.boot().catch((e) => {
  console.error('boot failed', e);
  const t = document.getElementById('load-text');
  if (t) t.textContent = 'something went wrong: ' + e.message;
});
