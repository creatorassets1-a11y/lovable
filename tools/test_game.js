// Headless harness for the game bundle.
//
// Serves assets/game over http (module scripts and fetch() both need a real
// origin), boots the game in Chromium, and checks three things that are easy to
// break and impossible to notice from source: JS errors, layout overflow at
// awkward viewport sizes, and whether the world actually renders anything.
//
//   node tools/test_game.js            # full run, writes screenshots
//   node tools/test_game.js --quick    # errors + overflow only

const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const ROOT = path.join(__dirname, '..', 'android', 'app', 'src', 'main', 'assets', 'game');
const SHOTS = path.join(__dirname, '..', 'dist', 'shots');
const QUICK = process.argv.includes('--quick');

const MIME = {
  '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css',
  '.ogg': 'audio/ogg', '.json': 'application/json', '.png': 'image/png',
};

// Viewports chosen to be hostile: a very short landscape phone, a tall notched
// phone forced into portrait, and a tablet.
const VIEWPORTS = [
  { name: 'phone-landscape', width: 800, height: 360 },
  { name: 'phone-landscape-tall', width: 915, height: 412 },
  { name: 'phone-narrow', width: 640, height: 300 },
  { name: 'tablet-landscape', width: 1280, height: 800 },
  { name: 'portrait', width: 412, height: 915 },
];

function serve(root) {
  const server = http.createServer((req, res) => {
    const url = decodeURIComponent(req.url.split('?')[0]);
    if (url === '/favicon.ico') { res.writeHead(204).end(); return; }
    const rel = url === '/' ? '/index.html' : url;
    const file = path.join(root, rel);
    if (!file.startsWith(root)) { res.writeHead(403).end(); return; }
    fs.readFile(file, (err, data) => {
      if (err) { res.writeHead(404).end('not found'); return; }
      res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream' });
      res.end(data);
    });
  });
  return new Promise((r) => server.listen(0, () => r(server)));
}

async function main() {
  const server = await serve(ROOT);
  const port = server.address().port;
  const base = `http://127.0.0.1:${port}/`;
  fs.mkdirSync(SHOTS, { recursive: true });

  const browser = await chromium.launch({
    executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: [
      '--autoplay-policy=no-user-gesture-required',
      '--use-gl=swiftshader',
      '--no-sandbox',
    ],
  });

  let failures = 0;
  const note = (ok, msg) => {
    console.log(`${ok ? '  ok  ' : ' FAIL '} ${msg}`);
    if (!ok) failures++;
  };

  for (const vp of VIEWPORTS) {
    console.log(`\n=== ${vp.name} ${vp.width}x${vp.height} ===`);
    const ctx = await browser.newContext({
      viewport: { width: vp.width, height: vp.height },
      deviceScaleFactor: 2,
      hasTouch: true,
      isMobile: true,
    });
    const page = await ctx.newPage();

    const errors = [];
    page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
    page.on('requestfailed', (r) => errors.push('requestfailed: ' + r.url()));
    page.on('response', (r) => {
      if (r.status() >= 400) errors.push(`http ${r.status()}: ${r.url()}`);
    });
    page.on('console', (m) => {
      const t = m.text();
      if (m.type() === 'error') errors.push('console: ' + t);
      // The engine logs its own failures rather than throwing, so catch those too.
      if (/failed|Failed|undefined is not|cannot read/i.test(t)) errors.push('log: ' + t);
    });

    await page.goto(base, { waitUntil: 'domcontentloaded' });

    // Wait for boot to reach the title screen.
    await page.waitForFunction(
      () => window.__game && window.__game.state === 'title',
      null, { timeout: 60000 },
    ).catch(() => {});

    const state = await page.evaluate(() => window.__game && window.__game.state);
    note(state === 'title', `booted to title (state=${state})`);

    await page.screenshot({ path: path.join(SHOTS, `${vp.name}-title.png`) });

    // --- overflow checks on the title screen
    const ov1 = await checkOverflow(page);
    note(ov1.ok, `title layout contained ${ov1.detail}`);

    // --- start playing
    await page.click('#btn-start');
    // Wait on the state, not the clock: dt is clamped per frame, so under
    // swiftshader the loop card's timer runs in slow motion.
    await page.waitForFunction(
      () => window.__game.state === 'playing', null, { timeout: 60000 },
    ).catch(() => {});

    const playing = await page.evaluate(() => window.__game.state);
    note(playing === 'playing', `entered gameplay (state=${playing})`);

    // Force the HUD into its busiest possible configuration before measuring:
    // long subtitle, long objective, breath bar and interact button all visible.
    await page.evaluate(() => {
      const g = window.__game;
      g.ui.setUseButton('LISTEN');
      g.ui.setBreath(true, 0.6);
      g.ui.objective('The red door at the far end is open. It has never been open before, ' +
                     'and you have walked past it eight times.', 999999);
      g.ui.subtitle('r_02');
      g.ui.setMeters(0.63, 0.41);
    });
    await page.waitForTimeout(400);
    await page.screenshot({ path: path.join(SHOTS, `${vp.name}-hud.png`) });

    const ov2 = await checkOverflow(page);
    note(ov2.ok, `HUD layout contained ${ov2.detail}`);

    if (!QUICK) {
      // --- does the renderer actually produce a picture?
      const stats = await page.evaluate(() => {
        const c = document.getElementById('view');
        const g = c.getContext('2d');
        const d = g.getImageData(0, 0, c.width, c.height).data;
        let sum = 0, max = 0, nonBlack = 0;
        for (let i = 0; i < d.length; i += 4 * 97) {
          const v = (d[i] + d[i + 1] + d[i + 2]) / 3;
          sum += v; if (v > max) max = v;
          if (v > 8) nonBlack++;
        }
        const n = Math.ceil(d.length / (4 * 97));
        return { mean: sum / n, max, nonBlackFrac: nonBlack / n, w: c.width, h: c.height };
      });
      note(stats.max > 30 && stats.nonBlackFrac > 0.05,
           `world rendered (mean=${stats.mean.toFixed(1)} max=${stats.max} lit=${(stats.nonBlackFrac * 100).toFixed(0)}%)`);

      // --- drive it: walk the corridor, check the player actually moves
      const before = await page.evaluate(() => ({ ...window.__game.player }));
      await page.evaluate(() => {
        window.__game.input.move.x = 0;
        window.__game.input.move.y = 1;
      });
      await page.waitForTimeout(2500);
      const after = await page.evaluate(() => ({ ...window.__game.player }));
      await page.evaluate(() => { window.__game.input.move.y = 0; });
      const moved = Math.hypot(after.x - before.x, after.y - before.y);
      note(moved > 1.0, `player walked ${moved.toFixed(2)} tiles`);

      // --- collision: the player must never end up inside geometry
      const inWall = await page.evaluate(() => {
        const g = window.__game;
        const { isSolid } = g.__world || {};
        return g.world.grid[(g.player.y | 0) * 32 + (g.player.x | 0)] !== 0;
      });
      note(!inWall, 'player is not inside a wall');

      // --- exercise a scare end-to-end
      await page.evaluate(() => window.__game.doScare({
        face: 0, sound: 'scream_mara', vo: 'm_04', shake: 1, dur: 1.2,
      }));
      await page.waitForTimeout(280);
      await page.screenshot({ path: path.join(SHOTS, `${vp.name}-scare.png`) });
      const scareDrew = await page.evaluate(() => {
        const c = document.getElementById('scare');
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
        let lit = 0;
        for (let i = 3; i < d.length; i += 4 * 211) if (d[i] > 24) lit++;
        return lit / Math.ceil(d.length / (4 * 211));
      });
      note(scareDrew > 0.25, `scare covered the screen (${(scareDrew * 100).toFixed(0)}%)`);
      await page.waitForTimeout(1500);

      // --- run the whole story: every loop, every event, looking for throws
      const storyErrors = await page.evaluate(async () => {
        const g = window.__game;
        const errs = [];
        const origErr = console.error;
        console.error = (...a) => { errs.push(a.join(' ')); origErr(...a); };
        for (let loop = 1; loop <= 9; loop++) {
          g.enterLoop(loop, false);
          g.state = 'playing';
          const script = (await import('./js/story.js')).LOOPS[loop];
          for (let i = 0; i < script.events.length; i++) {
            const e = script.events[i];
            // Feed each handler a plausible prop so onUse events can read .data.
            const prop = { kind: e.use || 'note', data: { note: 0 }, x: 6, y: 4 };
            try { e.run(g.g, prop); } catch (err) { errs.push(`loop ${loop} event ${i}: ${err.message}`); }
          }
          g.clearTimers();
        }
        console.error = origErr;
        return errs;
      });
      note(storyErrors.length === 0,
           `all 9 loops' events ran clean${storyErrors.length ? ': ' + storyErrors.slice(0, 4).join(' | ') : ''}`);

      // --- frame budget
      const fps = await page.evaluate(() => new Promise((res) => {
        let n = 0;
        const t0 = performance.now();
        const tick = () => {
          if (++n >= 90) return res(n / ((performance.now() - t0) / 1000));
          requestAnimationFrame(tick);
        };
        requestAnimationFrame(tick);
      }));
      // swiftshader on a VM is far slower than a phone GPU; this is a smoke
      // test for "the loop is alive", not a performance claim.
      note(fps > 4, `render loop running (${fps.toFixed(0)} fps under swiftshader)`);
    }

    const real = errors.filter((e) => !/favicon|AudioContext was not allowed/.test(e));
    note(real.length === 0, `no JS errors${real.length ? ': ' + real.slice(0, 5).join(' | ') : ''}`);

    await ctx.close();
  }

  await browser.close();
  server.close();

  console.log(failures === 0
    ? '\nALL CHECKS PASSED'
    : `\n${failures} CHECK(S) FAILED`);
  process.exit(failures === 0 ? 0 : 1);
}

/**
 * Layout containment. Checks the document itself cannot scroll, and that no
 * element's border box pokes outside the viewport.
 */
async function checkOverflow(page) {
  return page.evaluate(() => {
    const vw = window.innerWidth;
    const vh = window.innerHeight;
    const bad = [];

    const de = document.documentElement;
    if (de.scrollWidth > vw + 1) bad.push(`document scrollWidth ${de.scrollWidth} > ${vw}`);
    if (de.scrollHeight > vh + 1) bad.push(`document scrollHeight ${de.scrollHeight} > ${vh}`);

    for (const el of document.querySelectorAll('#app *')) {
      if (!el.offsetParent && getComputedStyle(el).position !== 'fixed') continue;
      const cs = getComputedStyle(el);
      if (cs.display === 'none' || cs.visibility === 'hidden') continue;
      const r = el.getBoundingClientRect();
      if (r.width === 0 && r.height === 0) continue;
      const id = el.id ? '#' + el.id : '.' + (el.className || el.tagName).toString().split(' ')[0];
      // 1.5px tolerance for subpixel rounding at deviceScaleFactor 2.
      if (r.left < -1.5) bad.push(`${id} left=${r.left.toFixed(1)}`);
      if (r.top < -1.5) bad.push(`${id} top=${r.top.toFixed(1)}`);
      if (r.right > vw + 1.5) bad.push(`${id} right=${r.right.toFixed(1)} > ${vw}`);
      if (r.bottom > vh + 1.5) bad.push(`${id} bottom=${r.bottom.toFixed(1)} > ${vh}`);
      // An element that scrolls its own content is only allowed to if it opted in.
      if (el.scrollHeight > el.clientHeight + 2 && !el.classList.contains('scrollable')
          && cs.overflowY !== 'auto' && cs.overflowY !== 'scroll' && cs.overflowY !== 'hidden') {
        bad.push(`${id} content overflows (${el.scrollHeight} > ${el.clientHeight})`);
      }
    }
    return { ok: bad.length === 0, detail: bad.length ? '— ' + bad.slice(0, 6).join('; ') : '' };
  });
}

main().catch((e) => { console.error(e); process.exit(1); });
