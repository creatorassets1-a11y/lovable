// Screens, HUD and text.
//
// Anything that touches the DOM lives here so the game loop never has to. The
// subtitle box is the only part with real logic: lines are held for the spoken
// duration reported by the voice manifest, not the file length, because every
// line has seconds of reverb tail after the last word.

import { ROMAN, clamp } from './util.js';

const $ = (id) => document.getElementById(id);

const SPEAKER = {
  adam: '', radio: 'RADIO', ellie: 'ELLIE', mara: 'MARA', whisper: '',
};

export class UI {
  constructor(vo) {
    this.vo = vo;
    this.el = {
      hud: $('hud'),
      loopNum: $('loop-num'),
      clock: $('clock'),
      battery: $('battery-fill'),
      sanity: $('sanity-fill'),
      objective: $('objective'),
      subs: $('subs'),
      btnUse: $('btn-use'),
      useLabel: $('use-label'),
      btnTorch: $('btn-torch'),
      btnRun: $('btn-run'),
      btnBreath: $('btn-breath'),
      breathFill: $('breath-fill'),
      loadFill: $('load-fill'),
      loadText: $('load-text'),
      loopRoman: $('loop-roman'),
      loopCaption: $('loop-caption'),
      deadLine: $('dead-line'),
      endTitle: $('end-title'),
      endBody: $('end-body'),
      btnEndOk: $('btn-end-ok'),
      pauseLoopNum: $('pause-loop-num'),
      btnContinue: $('btn-continue'),
    };
    this.screens = {
      load: $('scr-load'),
      title: $('scr-title'),
      opts: $('scr-opts'),
      pause: $('scr-pause'),
      loop: $('scr-loop'),
      dead: $('scr-dead'),
      end: $('scr-end'),
      tap: $('scr-tap'),
    };
    this.subsEnabled = true;
    this._subTimer = 0;
    this._noteEl = null;
  }

  // ------------------------------------------------------------- screens

  show(name) {
    for (const [k, el] of Object.entries(this.screens)) {
      el.classList.toggle('hidden', k !== name);
    }
    this.current = name;
  }

  hideAll() {
    for (const el of Object.values(this.screens)) el.classList.add('hidden');
    this.current = null;
  }

  setHud(visible) {
    this.el.hud.classList.toggle('hidden', !visible);
  }

  progress(f, text) {
    this.el.loadFill.style.width = `${clamp(f, 0, 1) * 100}%`;
    if (text) this.el.loadText.textContent = text;
  }

  // ----------------------------------------------------------------- hud

  setLoop(n) {
    this.el.loopNum.textContent = String(n);
    this.el.pauseLoopNum.textContent = String(n);
  }

  setMeters(batteryFrac, sanityFrac) {
    this.el.battery.style.width = `${clamp(batteryFrac, 0, 1) * 100}%`;
    this.el.sanity.style.width = `${clamp(sanityFrac, 0, 1) * 100}%`;
  }

  /** The clock only ever moves in loop 9, and only once. */
  setClock(text) {
    this.el.clock.textContent = text;
  }

  setUseButton(label) {
    const b = this.el.btnUse;
    if (label) {
      this.el.useLabel.textContent = label;
      b.classList.remove('hidden');
    } else {
      b.classList.add('hidden');
    }
  }

  setTorchOn(on) {
    this.el.btnTorch.classList.toggle('on', on);
  }

  setRunning(on) {
    this.el.btnRun.classList.toggle('on', on);
  }

  setBreath(visible, frac) {
    this.el.btnBreath.classList.toggle('hidden', !visible);
    // The breath bar occupies the strip subtitles normally sit in; lift them.
    this.el.hud.classList.toggle('breathing', visible);
    if (visible) this.el.breathFill.style.width = `${clamp(frac, 0, 1) * 100}%`;
  }

  objective(text, holdMs = 6000) {
    const el = this.el.objective;
    clearTimeout(this._objTimer);
    if (!text) {
      el.classList.remove('show');
      return;
    }
    el.textContent = text;
    el.classList.add('show');
    this._objTimer = setTimeout(() => el.classList.remove('show'), holdMs);
  }

  // ----------------------------------------------------------- subtitles

  /** Show the line for `id`, held for its spoken length plus a short tail. */
  subtitle(id) {
    const entry = this.vo[id];
    if (!entry || !this.subsEnabled) return;
    const el = this.el.subs;
    const who = SPEAKER[entry.c] ?? '';
    el.dataset.who = entry.c;
    el.innerHTML =
      (who ? `<span class="who">${who}</span>` : '') +
      `<span class="said">${escapeHtml(entry.t)}</span>`;
    el.classList.add('show');

    clearTimeout(this._subTimer);
    const holdMs = ((entry.s || entry.d || 2) + 0.7) * 1000;
    this._subTimer = setTimeout(() => el.classList.remove('show'), holdMs);
  }

  clearSubtitle() {
    clearTimeout(this._subTimer);
    this.el.subs.classList.remove('show');
  }

  // --------------------------------------------------------------- notes

  /**
   * Notes get their own overlay rather than a screen in index.html, because
   * they are the only text the player can dismiss at their own pace.
   */
  showNote(note, onClose) {
    if (!this._noteEl) {
      const sec = document.createElement('section');
      sec.className = 'layer screen';
      sec.innerHTML =
        '<div class="screen-inner scrollable">' +
        '<h2 class="heading" id="note-title"></h2>' +
        '<div class="end-body" id="note-body"></div>' +
        '<div class="btn-row"><button class="big-btn ghost" id="note-ok" type="button">PUT IT BACK</button></div>' +
        '</div>';
      document.getElementById('app').appendChild(sec);
      this._noteEl = sec;
      this._noteTitle = sec.querySelector('#note-title');
      this._noteBody = sec.querySelector('#note-body');
      sec.querySelector('#note-ok').addEventListener('click', () => this.hideNote());
    }
    this._noteTitle.textContent = note.title;
    this._noteBody.innerHTML = note.body
      .split('\n\n')
      .map((p) => `<p>${escapeHtml(p)}</p>`)
      .join('');
    this._noteEl.classList.remove('hidden');
    this._noteClose = onClose;
  }

  hideNote() {
    if (!this._noteEl) return;
    this._noteEl.classList.add('hidden');
    if (this._noteClose) {
      const cb = this._noteClose;
      this._noteClose = null;
      cb();
    }
  }

  get noteOpen() {
    return !!(this._noteEl && !this._noteEl.classList.contains('hidden'));
  }

  // ---------------------------------------------------------- set pieces

  loopCard(n, caption) {
    this.el.loopRoman.textContent = ROMAN[n] || String(n);
    this.el.loopCaption.textContent = caption || '';
    this.show('loop');
  }

  deathCard(line) {
    this.el.deadLine.textContent = line;
    this.show('dead');
  }

  ending(ending) {
    this.el.endTitle.textContent = ending.title;
    this.el.endBody.innerHTML = ending.body.map((p) => `<p>${p}</p>`).join('');
    this.el.btnEndOk.textContent = ending.button;
    this.show('end');
  }

  setContinueVisible(v) {
    this.el.btnContinue.classList.toggle('hidden', !v);
  }
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}
