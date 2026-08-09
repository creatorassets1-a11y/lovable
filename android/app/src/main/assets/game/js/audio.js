// Audio engine.
//
// Memory is the constraint that shapes this file. Fully decoding every asset
// would cost well over 100 MB of PCM on a phone, so sounds are split by how
// they are used:
//
//   BUFFERED  short one-shots that must fire on the exact frame they are asked
//             for (footsteps, stingers, screams). Decoded up front into
//             AudioBuffers. ~20 MB.
//   STREAMED  long loops (drones, water, breathing). Played from
//             HTMLAudioElement through MediaElementAudioSourceNode, so they
//             cost a decoder and a small buffer rather than their full PCM.
//   VO        dialogue. Streamed too, but pooled and pre-warmed per loop so
//             playback starts promptly. The three shouted lines are also
//             buffered because they are jump-scare payloads.

import { clamp } from './util.js';

const SFX_DIR = 'audio/sfx/';
const VO_DIR = 'audio/vo/';

// Long, steady, loopable — streamed.
const STREAMED = [
  'drone_calm', 'drone_dread', 'drone_chaos', 'water_rise', 'light_buzz',
  'whisper_bed', 'heart_slow', 'heart_fast', 'breath_calm', 'breath_panic',
  'music_box', 'ending',
];

// Everything else in the sfx bank is short — decoded into memory.
const BUFFERED = [
  'scream_mara', 'scream_mara2', 'scream_child', 'giggle', 'sub_boom',
  'stinger_a', 'stinger_b', 'stinger_c', 'riser',
  'door_creak1', 'door_creak2', 'door_slam', 'knock', 'scrape', 'bone',
  'static', 'radio_tune',
  'step_wet1', 'step_wet2', 'step_wet3', 'step_wet4',
  'step_dry1', 'step_dry2', 'step_dry3', 'step_dry4',
  'drip1', 'drip2', 'drip3',
];

// Shouts double as scare payloads and must not wait on a stream to spin up.
const BUFFERED_VO = ['m_04', 'm_07', 'm_13'];

export class AudioEngine {
  constructor() {
    this.ctx = null;
    this.ready = false;
    this.buffers = new Map();
    this.streams = new Map();   // name -> {el, src, gain, playing}
    this.voPool = [];
    this.voWarm = new Map();
    this.masterVolume = 0.9;
    this._muffle = 0;
    this._activeVoice = null;
  }

  // ------------------------------------------------------------- graph

  init() {
    if (this.ctx) return;
    const Ctx = window.AudioContext || window.webkitAudioContext;
    this.ctx = new Ctx({ latencyHint: 'interactive' });
    const c = this.ctx;

    this.master = c.createGain();
    this.master.gain.value = this.masterVolume;

    // Stops the stack of drones + a scream from clipping into mush.
    this.comp = c.createDynamicsCompressor();
    this.comp.threshold.value = -14;
    this.comp.knee.value = 22;
    this.comp.ratio.value = 5;
    this.comp.attack.value = 0.004;
    this.comp.release.value = 0.22;

    // Global tone control: closes down when hiding, drowning or unravelling.
    this.muffle = c.createBiquadFilter();
    this.muffle.type = 'lowpass';
    this.muffle.frequency.value = 20000;
    this.muffle.Q.value = 0.7;

    this.master.connect(this.muffle);
    this.muffle.connect(this.comp);
    this.comp.connect(c.destination);

    // Buses
    this.bus = {};
    for (const [name, vol] of [['amb', 0.85], ['sfx', 1.0], ['voice', 1.0], ['music', 0.8]]) {
      const g = c.createGain();
      g.gain.value = vol;
      g.connect(this.master);
      this.bus[name] = g;
    }

    // One shared hallway reverb, fed by sends.
    this.verb = c.createConvolver();
    this.verb.buffer = this._makeIR(2.4, 2.2);
    this.verbGain = c.createGain();
    this.verbGain.gain.value = 0.5;
    this.verb.connect(this.verbGain);
    this.verbGain.connect(this.master);

    this.ready = true;
  }

  /** Exponentially decaying filtered noise — a corridor, near enough. */
  _makeIR(seconds, decay) {
    const c = this.ctx;
    const n = Math.floor(c.sampleRate * seconds);
    const ir = c.createBuffer(2, n, c.sampleRate);
    for (let ch = 0; ch < 2; ch++) {
      const d = ir.getChannelData(ch);
      let lp = 0;
      for (let i = 0; i < n; i++) {
        const t = i / n;
        const env = Math.pow(1 - t, decay);
        lp += ((Math.random() * 2 - 1) - lp) * 0.42;   // cheap one-pole tilt
        d[i] = lp * env;
      }
    }
    return ir;
  }

  resume() {
    if (this.ctx && this.ctx.state !== 'running') return this.ctx.resume();
    return Promise.resolve();
  }

  suspend() {
    if (this.ctx && this.ctx.state === 'running') return this.ctx.suspend();
    return Promise.resolve();
  }

  setMasterVolume(v) {
    this.masterVolume = clamp(v, 0, 1);
    if (this.master) this._ramp(this.master.gain, this.masterVolume, 0.1);
  }

  /** 0 = clear, 1 = heavily underwater. */
  setMuffle(amount) {
    this._muffle = clamp(amount, 0, 1);
    if (!this.ready) return;
    const hz = 20000 * Math.pow(0.018, this._muffle);   // 20k -> ~360 Hz
    this._ramp(this.muffle.frequency, hz, 0.25);
  }

  _ramp(param, value, time = 0.08) {
    const t = this.ctx.currentTime;
    try {
      param.cancelScheduledValues(t);
      param.setValueAtTime(param.value, t);
      param.linearRampToValueAtTime(value, t + time);
    } catch {
      param.value = value;
    }
  }

  // ------------------------------------------------------------ loading

  async load(onProgress) {
    this.init();
    const jobs = BUFFERED.map((n) => [SFX_DIR + n + '.ogg', n])
      .concat(BUFFERED_VO.map((n) => [VO_DIR + n + '.ogg', 'vo:' + n]));

    let done = 0;
    const total = jobs.length + STREAMED.length;
    const step = () => onProgress && onProgress(++done / total);

    // A little concurrency: enough to saturate the asset reader, not enough to
    // stall the main thread decoding four megabytes at once.
    const queue = jobs.slice();
    const workers = Array.from({ length: 4 }, async () => {
      while (queue.length) {
        const [url, key] = queue.shift();
        try {
          const res = await fetch(url);
          const buf = await res.arrayBuffer();
          this.buffers.set(key, await this.ctx.decodeAudioData(buf));
        } catch (e) {
          console.warn('audio load failed', url, e);
        }
        step();
      }
    });
    await Promise.all(workers);

    for (const name of STREAMED) {
      this._makeStream(name);
      step();
    }
  }

  _makeStream(name) {
    const el = new Audio(SFX_DIR + name + '.ogg');
    el.loop = true;
    el.preload = 'auto';
    el.crossOrigin = 'anonymous';
    const gain = this.ctx.createGain();
    gain.gain.value = 0;
    let src = null;
    try {
      src = this.ctx.createMediaElementSource(el);
      src.connect(gain);
    } catch (e) {
      console.warn('stream node failed', name, e);
    }
    const entry = { el, src, gain, playing: false, bus: null };
    this.streams.set(name, entry);
    return entry;
  }

  // ------------------------------------------------------------ playback

  /**
   * Fire a one-shot from the buffered bank.
   * opts: {vol, rate, pan, bus, verb, delay, detune}
   */
  play(name, opts = {}) {
    if (!this.ready) return null;
    const buf = this.buffers.get(name);
    if (!buf) return null;

    const c = this.ctx;
    const src = c.createBufferSource();
    src.buffer = buf;
    src.playbackRate.value = opts.rate ?? 1;

    const g = c.createGain();
    g.gain.value = opts.vol ?? 1;

    let node = src;
    if (opts.pan !== undefined && c.createStereoPanner) {
      const p = c.createStereoPanner();
      p.pan.value = clamp(opts.pan, -1, 1);
      node.connect(p);
      node = p;
    }
    node.connect(g);
    g.connect(this.bus[opts.bus || 'sfx']);

    const send = opts.verb ?? 0.18;
    if (send > 0) {
      const s = c.createGain();
      s.gain.value = send * (opts.vol ?? 1);
      node.connect(s);
      s.connect(this.verb);
    }

    src.start(c.currentTime + (opts.delay || 0));
    src.onended = () => { try { g.disconnect(); } catch { /* already gone */ } };
    return src;
  }

  /**
   * Positional one-shot. Converts a world position into gain + pan relative to
   * where the player is standing and facing.
   */
  playAt(name, sx, sy, listener, opts = {}) {
    const dx = sx - listener.x;
    const dy = sy - listener.y;
    const dist = Math.hypot(dx, dy);
    const range = opts.range ?? 14;
    if (dist > range) return null;

    const falloff = Math.pow(1 - dist / range, 1.7);
    // Angle of the source relative to the player's facing; sin gives left/right.
    const rel = Math.atan2(dy, dx) - listener.a;
    const pan = clamp(Math.sin(rel) * 0.9, -1, 1);

    return this.play(name, {
      ...opts,
      vol: (opts.vol ?? 1) * falloff,
      pan,
      verb: (opts.verb ?? 0.25) + Math.min(0.35, dist * 0.03),
    });
  }

  // ---------------------------------------------------------- ambience

  /** Fade a streamed loop to `target` gain over `time` seconds. */
  setLoop(name, target, time = 2.0, bus = 'amb') {
    if (!this.ready) return;
    const s = this.streams.get(name) || this._makeStream(name);
    if (s.bus !== bus) {
      try { s.gain.disconnect(); } catch { /* not connected yet */ }
      s.gain.connect(this.bus[bus]);
      s.bus = bus;
    }
    if (target > 0.001 && !s.playing) {
      s.playing = true;
      const p = s.el.play();
      if (p && p.catch) p.catch(() => { s.playing = false; });
    }
    this._ramp(s.gain.gain, target, time);
    if (target <= 0.001) {
      setTimeout(() => {
        if (s.gain.gain.value <= 0.002 && s.playing) {
          s.el.pause();
          s.playing = false;
        }
      }, time * 1000 + 60);
    }
  }

  stopAllLoops(time = 1.2) {
    for (const name of this.streams.keys()) this.setLoop(name, 0, time);
  }

  /** Nudge a running loop's playback rate — used to accelerate the heartbeat. */
  setLoopRate(name, rate) {
    const s = this.streams.get(name);
    if (s) s.el.playbackRate = clamp(rate, 0.5, 2.2);
  }

  // --------------------------------------------------------------- voice

  /** Warm the decoder for lines about to be needed, so playback is prompt. */
  warm(ids) {
    for (const id of ids) {
      if (this.voWarm.has(id) || BUFFERED_VO.includes(id)) continue;
      const el = new Audio(VO_DIR + id + '.ogg');
      el.preload = 'auto';
      el.crossOrigin = 'anonymous';
      el.load();
      this.voWarm.set(id, el);
    }
    // Keep the warm set small; these hold decoders open.
    while (this.voWarm.size > 24) {
      const oldest = this.voWarm.keys().next().value;
      this.voWarm.delete(oldest);
    }
  }

  /**
   * Speak a line. Returns a handle with .stop().
   * Only one voice line plays at a time; a new one cuts the old.
   */
  speak(id, opts = {}) {
    if (!this.ready) return { stop() {} };
    this.stopVoice();

    const vol = (opts.vol ?? 1);
    // Shouts live in the buffer bank so they land on the requested frame.
    if (this.buffers.has('vo:' + id)) {
      const src = this.play('vo:' + id, {
        vol, bus: 'voice', verb: opts.verb ?? 0.2, pan: opts.pan,
      });
      const handle = { stop: () => { try { src.stop(); } catch { /* ended */ } } };
      this._activeVoice = handle;
      return handle;
    }

    const el = this.voWarm.get(id) || new Audio(VO_DIR + id + '.ogg');
    this.voWarm.delete(id);
    el.crossOrigin = 'anonymous';
    el.currentTime = 0;

    const g = this.ctx.createGain();
    g.gain.value = vol;
    let node = null;
    try {
      // A MediaElementSource can only ever be created once per element, so a
      // recycled element carries its node with it.
      node = el._node || this.ctx.createMediaElementSource(el);
      el._node = node;
      node.connect(g);
      g.connect(this.bus.voice);
      const s = this.ctx.createGain();
      s.gain.value = (opts.verb ?? 0.22) * vol;
      node.connect(s);
      s.connect(this.verb);
    } catch (e) {
      console.warn('voice node failed', id, e);
    }

    const p = el.play();
    if (p && p.catch) p.catch(() => {});

    const handle = {
      stop: () => {
        try { el.pause(); el.currentTime = 0; } catch { /* detached */ }
        try { g.disconnect(); } catch { /* already gone */ }
      },
    };
    this._activeVoice = handle;
    return handle;
  }

  stopVoice() {
    if (this._activeVoice) {
      this._activeVoice.stop();
      this._activeVoice = null;
    }
  }

  /** Duck everything but the voice bus, for a beat. */
  duck(amount = 0.45, time = 0.3) {
    if (!this.ready) return;
    for (const b of ['amb', 'music']) this._ramp(this.bus[b].gain, amount, time);
  }

  unduck(time = 0.8) {
    if (!this.ready) return;
    this._ramp(this.bus.amb.gain, 0.85, time);
    this._ramp(this.bus.music.gain, 0.8, time);
  }

  /** Detuned cluster used as a live sting when a baked one would be overkill. */
  dissonance(root = 92, seconds = 1.4, vol = 0.35) {
    if (!this.ready) return;
    const c = this.ctx;
    const g = c.createGain();
    g.gain.value = 0;
    g.connect(this.bus.music);
    const t = c.currentTime;
    g.gain.linearRampToValueAtTime(vol, t + 0.02);
    g.gain.exponentialRampToValueAtTime(0.0001, t + seconds);
    for (const mult of [1, 1.0595, 1.4142, 2.0]) {
      const o = c.createOscillator();
      o.type = 'sawtooth';
      o.frequency.value = root * mult;
      o.detune.value = (Math.random() * 2 - 1) * 12;
      const og = c.createGain();
      og.gain.value = 0.25;
      o.connect(og);
      og.connect(g);
      o.start(t);
      o.stop(t + seconds + 0.05);
    }
  }

  /** Sub-bass thump felt through the chassis. Pairs with a haptic pulse. */
  impact(vol = 0.9) {
    this.play('sub_boom', { vol, verb: 0.1 });
  }
}
