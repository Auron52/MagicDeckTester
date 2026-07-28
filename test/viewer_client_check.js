#!/usr/bin/env node
// Headless jsdom CLIENT check for the play viewer (tools/play/index.html).
// =====================================================================================
// The other two viewer checks exercise the ENGINE↔protocol seam and the pure line-builder:
//   - test/viewer_protocol_check.py   feeds plan INDICES straight into the engine (play-drift)
//   - test/viewer_linebuild_check.js  drives linebuild.js line reconstruction
//   - test/viewer_validate_check.js   drives CheckLine (--validate-line) verdicts
// NONE of them run index.html's own client bookkeeping — the S.history / S.checkpoints / S.steps
// state machine behind undo, commit-turn auto-pass, and the history panel. This check closes that
// gap: it loads index.html's REAL <script> in a jsdom DOM and drives it exactly like a browser,
// with the network seam (fetch) pointed at the SAME server.js route logic (runStep/runValidate)
// the live bridge uses — so the engine truth is real, only the transport is in-process.
//
// It is a PROPERTY test, deck-agnostic: because the whole protocol is a STATELESS replay from
// (deck, seed, game-index, choices), the client state after undo-then-nothing MUST equal the
// client state you'd get by never having played the undone step. Any divergence in the rendered
// #hist panel is the viewer's own bookkeeping corruption (issue #2), not an engine difference.
//
// Run:  node test/viewer_client_check.js            (needs a built build/Release/mtg + jsdom)
//       node test/viewer_client_check.js --deck=treasure_hunt --seed=4 --turns=8 --verbose
//
// Exit 0 = all invariants held; 1 = a divergence (printed); 2 = harness/setup error.

const fs = require('fs');
const path = require('path');
let JSDOM;
try { ({ JSDOM } = require('jsdom')); }
catch (e) { console.error('jsdom not installed — add it to the container (npm i -D jsdom in test/).'); process.exit(2); }

const PLAY = path.resolve(__dirname, '..', 'tools', 'play');
const server = require(path.join(PLAY, 'server.js'));   // runStep / runValidate / listDecks / BIN

// ---- arg parsing -----------------------------------------------------------------
function argVal(name, dflt) {
  const hit = process.argv.find(a => a.startsWith('--' + name + '='));
  return hit ? hit.slice(name.length + 3) : dflt;
}
const VERBOSE = process.argv.includes('--verbose');
// Default scenarios: a couple of profiled decks with different tempos. treasure_hunt draws a lot
// (Treasure Hunt / cantrips) and has multi-main turns, which is exactly where the history panel's
// draw re-logging on undo is most likely to corrupt.
const SCENARIOS = argVal('deck', null)
  ? [{ deck: argVal('deck', ''), seed: parseInt(argVal('seed', '4'), 10), turns: parseInt(argVal('turns', '8'), 10) }]
  : [
      { deck: 'treasure_hunt', seed: 4, turns: 8 },
      { deck: 'Dragonstorm',   seed: 1, turns: 8 },
      { deck: 'burn',          seed: 2, turns: 8 },
    ];

// ---- fetch stub: dispatch the viewer's XHRs to the real server.js route logic ----
// Mirrors tools/play/server.js's http routes, but in-process (no port bind). spawnSync inside
// runStep/runValidate is synchronous, so each fetch resolves after the engine actually replayed.
function makeFetch() {
  return function fetch(pathUrl, opts) {
    const body = opts && opts.body ? JSON.parse(opts.body) : {};
    const u = new URL(pathUrl, 'http://localhost');
    let out;
    if (u.pathname === '/api/decks') {
      out = { decks: server.listDecks(), binExists: fs.existsSync(server.BIN) };
    } else if (u.pathname === '/api/reference-exists') {
      out = { exists: false, path: null, suboptimal: false, suboptimalPath: null };
    } else if (u.pathname === '/api/step') {
      out = server.runStep(body, null);
    } else if (u.pathname === '/api/validate') {
      out = server.runValidate(body, String(body.line == null ? '' : body.line));
    } else if (u.pathname === '/api/ai-hint' || u.pathname === '/api/keep-hint') {
      out = { kind: u.pathname.slice(5), hasSidecar: false };   // benign; browser treats as fire-and-forget
    } else {
      out = { kind: 'error', error: 'unrouted ' + u.pathname };
    }
    return Promise.resolve({ ok: true, json: () => Promise.resolve(out) });
  };
}

// ---- build a jsdom window running index.html's real script -----------------------
function buildDom() {
  let html = fs.readFileSync(path.join(PLAY, 'index.html'), 'utf8');
  const lb = fs.readFileSync(path.join(PLAY, 'linebuild.js'), 'utf8');
  // Inline linebuild.js (browser loads it via <script src="/linebuild.js">); jsdom won't fetch it.
  html = html.replace('<script src="/linebuild.js"></script>', '<script>\n' + lb + '\n</script>');
  const dom = new JSDOM(html, {
    runScripts: 'dangerously',
    url: 'http://localhost/',
    beforeParse(window) {
      window.fetch = makeFetch();
      // scryfall image loads / localStorage are already inert-safe under jsdom; nothing else to stub.
    },
  });
  const win = dom.window;
  // index.html declares `let S = {...}` at script top level — a lexical binding, NOT a window
  // property, so it's invisible from outside. Inject a classic script into the SAME realm to capture
  // it (function declarations like newGame/commitTurn/undo already ARE window properties). This is the
  // only seam we need: the client's own bookkeeping state, read exactly as the code mutates it.
  const acc = win.document.createElement('script');
  acc.textContent = 'window.__getS = function(){ return S; };'
    + 'window.__fb = { panel: firebreathePanelHtml, commit: commitFirebreathe, rollback: rollbackStep };'
    + 'window.__sh = { panel: storageHoldPanelHtml, commit: commitStorageHold, rollback: rollbackStep };'
    + 'window.__co = { apply: applyAccepted, rollback: rollbackStep };';
  win.document.body.appendChild(acc);
  return win;
}

// #4 firebreathe GUI bookkeeping (isolated — a real firebreathing combat is hard to drive via the
// line-building viewer). Verifies the modal renders and, crucially, that committing an amount rides the
// TURN-keyed side-channel (S.firebreathe) WITHOUT consuming a --choices slot, and that undo drops it —
// i.e. the new decision type does not perturb the positional stream (the whole point of #4's design).
function testFirebreatheBookkeeping(win) {
  const S = win.__getS(), fb = win.__fb, fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const d = { type: 'firebreathe', turn: 5, decision_index: 9, max_count: 5, heuristic_default: 5,
              attackers: [{ name: 'Scourge of Valkas' }] };
  const html = fb.panel(d);
  chk((html.match(/data-opt="\d+"/g) || []).length === 6, 'panel renders max+1=6 amount buttons (0..5)');
  // Seed a plausible mid-game state, then commit a held-back amount.
  S.choices = [1, 0, 0]; S.steps = [{ n: 1 }, { n: 1 }, { n: 1 }];
  S.checkpoints = [{ histLen: 0 }, { histLen: 0 }, { histLen: 0 }, { histLen: 0 }];
  S.history = []; S.firebreathe = {}; S.decision = d; S.busy = false;
  const choicesBefore = S.choices.length;
  fb.commit(d, 2);
  chk(S.firebreathe[5] === 2, 'commit records side-channel firebreathe[5]=2');
  chk(S.choices.length === choicesBefore, 'commit consumes NO --choices slot');
  const last = S.steps[S.steps.length - 1];
  chk(last && last.n === 0 && last.fb === 5, 'commit pushes a zero-int step keyed by turn (fb=5)');
  S.busy = false;
  fb.rollback();
  chk(!(5 in S.firebreathe), 'undo drops the side-channel entry');
  return fails;
}

// #6 storage tap-vs-charge GUI bookkeeping (isolated — reaching a charged storage land at a live decision
// is hard to drive here). Mirrors the firebreathe test: the answer rides the (turn,land#)-keyed side-channel
// (S.storageHold) WITHOUT consuming a --choices slot, and undo drops it, so the new decision type never
// perturbs the positional stream.
function testStorageHoldBookkeeping(win) {
  const S = win.__getS(), sh = win.__sh, fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const d = { type: 'storage_hold', turn: 6, land: 'Dwarven Hold', land_idx: 5, counters: 3,
              heuristic_default: 0, pre_draw: true };
  const html = sh.panel(d);
  chk((html.match(/data-opt="[01]"/g) || []).length === 2, 'panel renders the two hold/allow buttons');
  chk(/UPKEEP/.test(html), 'pre_draw panel notes the pre-draw (UPKEEP) commitment');
  S.choices = [1, 0, 0]; S.steps = [{ n: 1 }, { n: 1 }, { n: 1 }];
  S.checkpoints = [{ histLen: 0 }, { histLen: 0 }, { histLen: 0 }, { histLen: 0 }];
  S.history = []; S.storageHold = {}; S.decision = d; S.busy = false;
  const choicesBefore = S.choices.length;
  sh.commit(d, 1);
  chk(S.storageHold['6:5'] === 1, "commit records side-channel storageHold['6:5'] (turn:land_idx)=1");
  chk(S.choices.length === choicesBefore, 'commit consumes NO --choices slot');
  const last = S.steps[S.steps.length - 1];
  chk(last && last.n === 0 && last.sh === '6:5', 'commit pushes a zero-int step keyed by (turn,land_idx) (sh=6:5)');
  S.busy = false;
  sh.rollback();
  chk(!('6:5' in S.storageHold), 'undo drops the side-channel entry');
  return fails;
}

// #10 cast-order GUI bookkeeping: committing a main plan whose non-sac casts were QUEUED in a non-canonical
// order pins that order in the main-ordinal-keyed side-channel (S.castOrder) WITHOUT an extra --choices slot
// (the plan int is the only positional entry), and undo drops it. A canonical (or single-cast) order pins
// NOTHING -> references stay byte-identical. Mirrors the engine's cast_order_canonical + main_ordinal diff.
function testCastOrderBookkeeping(win) {
  const S = win.__getS(), co = win.__co, fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  // A main_phase decision whose accepted plan (index 0) canonically casts [A, B]; the human queued [B, A].
  // Carries empty board context so the async render (computeDiff over S.prev after applyAccepted's step())
  // can't throw on the seeded state — we assert the synchronous bookkeeping, not the round-trip.
  const dec = { type: 'main_phase', turn: 4, main_ordinal: 3,
                me: { life: 20, battlefield: [] }, opponent: { life: 20, battlefield: [] },
                plans: [{ index: 0, casts: ['Spell A', 'Spell B'], cast_order_canonical: ['Spell A', 'Spell B'] }] };
  function seed(planOrder) {
    S.choices = [1, 1]; S.steps = [{ n: 1 }, { n: 1 }];
    S.checkpoints = [{ histLen: 0 }, { histLen: 0 }, { histLen: 0 }];
    S.history = []; S.castOrder = {}; S.decision = dec; S.prev = null; S.busy = false;
    S.plan = planOrder.map(n => ({ name: n, kind: 'spell' }));
  }
  // Reordered queue [B, A] vs canonical [A, B] -> pins main_ordinal 3.
  seed(['Spell B', 'Spell A']);
  co.apply(0, 'reordered');
  chk(JSON.stringify(S.castOrder['3']) === JSON.stringify(['Spell B', 'Spell A']), 'reordered queue pins castOrder[3]=[B,A]');
  chk(S.choices[S.choices.length - 1] === 0, 'commit pushes exactly the plan int (0) to --choices');
  const last = S.steps[S.steps.length - 1];
  chk(last && last.n === 1 && last.co === '3', 'step marks n=1 + co=3 (cast-order key)');
  S.busy = false; co.rollback();
  chk(!('3' in S.castOrder), 'undo drops the cast-order side-channel entry');
  // Canonical queue [A, B] -> pins NOTHING (byte-identical / reference-clean).
  seed(['Spell A', 'Spell B']);
  co.apply(0, 'canonical');
  chk(Object.keys(S.castOrder).length === 0, 'canonical queue pins no castOrder (references stay byte-identical)');
  return fails;
}
// The live client state. newGame() rebinds `let S` to a fresh object, so always re-read through the
// captured accessor rather than caching a stale reference.
function S(win) { return win.__getS(); }

// Wait until the client is at a genuine rest point: not mid-request (S.busy false) AND the async
// auto-advance chain (advanceTo -> commitLine -> step, each detached via its own await) has fully
// unwound. step() sets S.busy=true synchronously on entry, so a false reading between two timer
// ticks means no spawn is in flight and no chained step was scheduled.
function settle(win) {
  const tick = () => new Promise(r => win.setTimeout(r, 0));
  return (async () => {
    for (let i = 0; i < 20000; i++) {
      await tick();
      const st = S(win);
      if (st && !st.busy) { await tick(); const s2 = S(win); if (s2 && !s2.busy) return; }
    }
    throw new Error('settle timeout');
  })();
}

// A stable, comparable snapshot of the rendered history panel (the user-visible artifact). Reads the
// live DOM #hist list so it reflects exactly what the user sees, not just the S.history model.
function histSnapshot(win) {
  return Array.from(win.document.querySelectorAll('#hist li')).map(li => {
    const t = (li.querySelector('.t') || {}).textContent || '';
    const lab = (li.querySelector('.lab') || {}).textContent || '';
    return `${t}|${lab}|${li.className.trim()}`;
  });
}
function diff(a, b) {
  const n = Math.max(a.length, b.length), rows = [];
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) rows.push(`  [${i}] before=${JSON.stringify(a[i])}  after=${JSON.stringify(b[i])}`);
  return rows;
}

// Drive one forward pass at the current decision, then settle. Returns false if there was nothing to
// do (game over / no decision). Decisions are driven through the SAME client entry points the GUI
// buttons call, so the bookkeeping (steps/checkpoints/history) is exercised exactly as in the browser:
//   - mulligan  -> KEEP the hand (commitMulligan(d,1)); durdling never wins, so we just want to play
//   - bottom    -> follow the AI's fill and commit the batch (commitBottomBatch)
//   - main      -> empty "Commit turn" (auto-passes the rest of the turn — the #2 auto-advance path)
//   - other sub -> the heuristic default via pushChoice
async function stepForward(win) {
  const st = S(win);
  if (st.over || !st.decision) return false;
  const d = st.decision, t = d.type;
  if (t === 'main_phase') { st.plan = []; win.commitTurn(); }
  else if (t === 'mulligan') { win.commitMulligan(d, 1); }        // keep
  else if (t === 'bottom') { win.followAiBottom(d); win.commitBottomBatch(d); }
  else if (t === 'firebreathe') { win.commitFirebreathe(d, d.heuristic_default == null ? (d.max_count || 0) : d.heuristic_default); }
  else if (t === 'storage_hold') { win.commitStorageHold(d, d.heuristic_default == null ? 0 : d.heuristic_default); }
  else { const def = (d.heuristic_default == null ? 0 : d.heuristic_default); win.pushChoice(d, def, `${t}:auto`); }
  await settle(win);
  return true;
}

async function startGame(win, sc) {
  const opt = Array.from(win.document.getElementById('deck').options).find(o => o.value.replace(/\.[^.]+$/, '') === sc.deck);
  if (!opt) throw new Error('deck not listed: ' + sc.deck);
  win.document.getElementById('deck').value = opt.value;
  win.document.getElementById('seed').value = String(sc.seed);
  win.document.getElementById('maxturns').value = String(sc.turns);
  await win.newGame();
  await settle(win);
}

async function playScenario(sc) {
  const win = buildDom();
  await settle(win);                              // loadDecks resolved
  await startGame(win, sc);

  // Forward pass through the game, snapshotting (choices, history) at every rest point BEFORE each
  // pass. We drive "Commit turn" (auto-passes the rest of the turn) since that is precisely the
  // auto-advance path issue #2 fingers.
  const rests = [];   // { choices, hist } captured at each user rest point, in order
  let guard = 0;
  while (!S(win).over && guard++ < 80) {
    if (!S(win).decision) break;
    rests.push({ choices: S(win).choices.slice(), hist: histSnapshot(win) });
    if (!(await stepForward(win))) break;
  }
  if (VERBOSE) console.log(`  [${sc.deck} s${sc.seed}] played ${rests.length} rest points, over=${S(win).over}`);

  // Property: from rest R, take ONE more forward pass and undo it — the undo must reproduce rest R's
  // history EXACTLY (stateless replay ⇒ undo-then-nothing == never having taken the step). A fresh DOM
  // per R isolates each check from prior-undo residue.
  const failures = [];
  for (let r = rests.length - 1; r >= 1; r--) {
    const fresh = buildDom();
    await settle(fresh);
    await startGame(fresh, sc);
    let g2 = 0;
    while (!S(fresh).over && g2++ < 80 && S(fresh).choices.length < rests[r].choices.length) {
      if (!(await stepForward(fresh))) break;
    }
    if (S(fresh).choices.length !== rests[r].choices.length) continue;   // couldn't reach this rest deterministically
    const atR = histSnapshot(fresh);
    if (S(fresh).over || !S(fresh).decision) continue;
    await stepForward(fresh);           // one more forward pass
    await fresh.undo();                  // then undo it
    await settle(fresh);
    const afterUndo = histSnapshot(fresh);
    const rows = diff(atR, afterUndo);
    if (rows.length) failures.push({ r, atR, afterUndo, rows });
  }
  return { deck: sc.deck, seed: sc.seed, rests: rests.length, over: S(win).over, failures };
}

(async () => {
  let anyFail = false;
  // #4 firebreathe GUI bookkeeping (fast, DOM-only — no game needed).
  {
    const win = buildDom(); await settle(win);
    const fbFails = testFirebreatheBookkeeping(win);
    if (fbFails.length) { anyFail = true; console.log(`✗ firebreathe bookkeeping: ${fbFails.length} fail`); fbFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ firebreathe GUI bookkeeping (side-channel + zero-int step + undo)'); }
    // #6 storage tap-vs-charge GUI bookkeeping (fast, DOM-only).
    const shFails = testStorageHoldBookkeeping(win);
    if (shFails.length) { anyFail = true; console.log(`✗ storage_hold bookkeeping: ${shFails.length} fail`); shFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ storage_hold GUI bookkeeping (side-channel + zero-int step + undo)'); }
    // #10 cast-order GUI bookkeeping (fast, DOM-only).
    const coFails = testCastOrderBookkeeping(win);
    if (coFails.length) { anyFail = true; console.log(`✗ cast_order bookkeeping: ${coFails.length} fail`); coFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ cast_order GUI bookkeeping (canonical diff + side-channel + undo)'); }
  }
  for (const sc of SCENARIOS) {
    let res;
    try { res = await playScenario(sc); }
    catch (e) { console.error(`✗ ${sc.deck} s${sc.seed}: harness error: ${e.stack || e}`); process.exit(2); }
    if (res.failures.length) {
      anyFail = true;
      console.log(`✗ ${res.deck} s${res.seed}: ${res.failures.length} undo-divergence(s) over ${res.rests} rest points`);
      for (const f of res.failures.slice(0, 4)) {
        console.log(`  at rest ${f.r} — history after undo != history before the undone step:`);
        f.rows.slice(0, 8).forEach(row => console.log(row));
      }
    } else {
      console.log(`✓ ${res.deck} s${res.seed}: ${res.rests} rest points, undo reproduces history exactly`);
    }
  }
  process.exit(anyFail ? 1 : 0);
})();
