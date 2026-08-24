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
// It DOES render the decision panels: renderBoard() draws the modal for each sub-decision on the live
// path, and stepForward additionally calls it explicitly (PANEL_ERRORS) because a throw on the live
// path arrives as a rejected promise inside an un-awaited handler — i.e. silently. That is the only
// layer that can see a *PanelHtml function that throws; the ReferenceError shipped in lackeyPanelHtml
// (92c7ce07) froze the viewer the moment Goblin Lackey connected while every other check stayed green.
//
// BLIND SPOT: it only renders the types the chosen decks/seeds actually PLAY INTO (the tally is
// printed at the end). A type nobody plays into is unguarded here — test/viewer_decision_types_check.js
// covers the complementary static question ("is the type in the SUBDECISIONS whitelist at all?").
// Keep both. Runtime ~2m: the undo property is quadratic in a game's rest points.
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
// `main` = how a scenario answers a main-phase decision, which decides WHICH decision types the
// scenario can ever reach:
//   'pass' — empty "Commit turn" (the #2 auto-advance path). Develops NO board, so it never reaches
//            a decision that needs permanents in play (combat triggers, upkeep costs).
//   'ai'   — submit plan index 0 (the model's own line) via applyAccepted, the same client entry
//            point the "click an enumerated plan" button uses. The board actually develops, which is
//            the only way in-game decisions like lackey_put (a combat-damage trigger) are reached.
// Both are real GUI paths and both are worth guarding, so scenarios carry the mode per-entry.
const SCENARIOS = argVal('deck', null)
  ? [{ deck: argVal('deck', ''), seed: parseInt(argVal('seed', '4'), 10),
       turns: parseInt(argVal('turns', '8'), 10), main: argVal('main', 'pass') }]
  : [
      { deck: 'treasure_hunt', seed: 4, turns: 8, main: 'pass' },
      { deck: 'Dragonstorm',   seed: 1, turns: 8, main: 'pass' },
      { deck: 'burn',          seed: 2, turns: 8, main: 'pass' },
      // Goblins on the model's own line: develops a board and connects with Goblin Lackey, so this is
      // the scenario that drives a lackey_put modal through the client's history/undo bookkeeping.
      { deck: 'Goblins',       seed: 1, turns: 8, main: 'ai' },
      // NOT here: KittyEquipment. Its turns are almost entirely board ACTIVATIONS, each committed as
      // its own segment, so one game reaches ~80 rest points -- and the undo property is quadratic in
      // rests (each one replays the game from scratch), i.e. ~6400 binary spawns for one deck. The
      // undo/history bookkeeping being tested is deck-AGNOSTIC, so the four above cover it; the
      // equipment-specific path has its own linear walk in testEquip().
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
    + 'window.__co = { apply: applyAccepted, rollback: rollbackStep };'
    // Top-level `const`s are lexical bindings, not window properties — expose the two the harness
    // reads. __subdecisions/__panel are what let stepForward render each frame's panel explicitly
    // (see PANEL_ERRORS): renderBoard already does it in the live flow, but a throw there surfaces
    // as a rejected promise inside an un-awaited handler, i.e. silently.
    + 'window.__MAX_TURNS = MAX_TURNS;'
    + 'window.__subdecisions = SUBDECISIONS;'
    + 'window.__panel = function(d, dec){ return renderDecisionPanel(d, dec); };'
    + 'window.__actpick = activationPickerHtml;';
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
// (the plan int is the only positional entry), and undo drops it.
// The pin is UNCONDITIONAL for a multi-cast commit (USER 2026-08-21: "the user's order in the viewer
// should be respected regardless of the deck order") — a queue that happens to MATCH canonical is still
// pinned, because under MTG_UNPRUNED the matched plan's execution order is its vector order, not
// canonical, so "human == canonical" could still execute a different order (see applyAccepted). This
// check used to assert the older send-only-if-it-differs contract and had been red ever since.
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
  // Canonical queue [A, B] -> pinned TOO (the queued order is authoritative, see above).
  seed(['Spell A', 'Spell B']);
  co.apply(0, 'canonical');
  chk(JSON.stringify(S.castOrder['3']) === JSON.stringify(['Spell A', 'Spell B']),
      'canonical queue is pinned as well (queued order is authoritative)');
  // A SINGLE-cast commit still pins nothing: there is no order to respect, and pinning would put a
  // --cast-order on every reference for no reason.
  const dec1 = { type: 'main_phase', turn: 4, main_ordinal: 3,
                 me: { life: 20, battlefield: [] }, opponent: { life: 20, battlefield: [] },
                 plans: [{ index: 0, casts: ['Spell A'] }] };
  S.choices = [1, 1]; S.steps = [{ n: 1 }, { n: 1 }];
  S.checkpoints = [{ histLen: 0 }, { histLen: 0 }, { histLen: 0 }];
  S.history = []; S.castOrder = {}; S.decision = dec1; S.prev = null; S.busy = false;
  S.plan = [{ name: 'Spell A', kind: 'spell' }];
  co.apply(0, 'single');
  chk(Object.keys(S.castOrder).length === 0, 'a single-cast commit pins no castOrder');
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
// Which decision types a scenario actually reached. A scenario only guards the types it PLAYS INTO,
// so without this the pass tells you nothing about coverage (an empty "Commit turn" line develops no
// board, so it may never reach e.g. a combat-damage lackey_put). --verbose prints the tally.
const TYPES_SEEN = new Map();
// Every panel-render error hit while walking a game, as "<type>: <message>". renderBoard() renders
// the modal for each sub-decision, but it is reached through an un-awaited async chain, so a throw
// inside a *PanelHtml function became a swallowed promise rejection and the check stayed green while
// the browser froze on a blank board (the lackey_put ReferenceError, shipped in 92c7ce07). Rendering
// it here as well makes that a hard, attributable failure.
const PANEL_ERRORS = [];
const RENDERED_TYPES = new Set();
async function stepForward(win, mainMode) {
  const st = S(win);
  if (st.over || !st.decision) return false;
  const d = st.decision, t = d.type;
  TYPES_SEEN.set(t, (TYPES_SEEN.get(t) || 0) + 1);
  if (t !== 'main_phase' && (win.__subdecisions || []).includes(t)) {
    try {
      win.__panel(d, t);
      const rendered = win.document.getElementById('decpanel');
      if (rendered && !rendered.innerHTML) PANEL_ERRORS.push(`${t}: panel rendered EMPTY`);
      else RENDERED_TYPES.add(t);
    } catch (e) { PANEL_ERRORS.push(`${t}: ${e && e.message ? e.message : e}`); }
  }
  // 'ai': play the model's own plan (index 0) through applyAccepted — the same entry point the
  // "click an enumerated plan" button uses — so the board develops and in-game decisions are reached.
  if (t === 'main_phase' && mainMode === 'ai') { st.plan = []; win.applyAccepted(0, 'AI plan'); }
  else if (t === 'main_phase') { st.plan = []; win.commitTurn(); }
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
  // NO #maxturns input: ce487708 removed the box because max_turns keys the search's horizon, so
  // it is a FIXED const (MAX_TURNS = 8) rather than a user setting. This line used to write it and
  // threw `Cannot set properties of null` — which the runner catches with process.exit(2), so the
  // WHOLE check died here and not one scenario ever ran (that is why the lackey_put panel's
  // ReferenceError shipped: this is the only check that renders index.html's panels). A scenario's
  // `turns` is now an assertion about the page's constant, not an input.
  if (sc.turns != null && win.__MAX_TURNS !== sc.turns) {
    throw new Error(`scenario wants ${sc.turns} turns but index.html pins MAX_TURNS=${win.__MAX_TURNS}`);
  }
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
  TYPES_SEEN.clear();   // tally THIS scenario's forward pass only (the undo passes replay it)
  let guard = 0;
  while (!S(win).over && guard++ < 80) {
    if (!S(win).decision) break;
    rests.push({ choices: S(win).choices.slice(), hist: histSnapshot(win) });
    if (!(await stepForward(win, sc.main))) break;
  }
  if (VERBOSE) {
    console.log(`  [${sc.deck} s${sc.seed}] played ${rests.length} rest points, over=${S(win).over}`);
    console.log(`  [${sc.deck} s${sc.seed}] decision types reached: ` +
                [...TYPES_SEEN].map(([t, n]) => `${t}×${n}`).join(' ')); }

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
      if (!(await stepForward(fresh, sc.main))) break;
    }
    if (S(fresh).choices.length !== rests[r].choices.length) continue;   // couldn't reach this rest deterministically
    const atR = histSnapshot(fresh);
    if (S(fresh).over || !S(fresh).decision) continue;
    await stepForward(fresh, sc.main);   // one more forward pass
    await fresh.undo();                  // then undo it
    await settle(fresh);
    const afterUndo = histSnapshot(fresh);
    const rows = diff(atR, afterUndo);
    if (rows.length) failures.push({ r, atR, afterUndo, rows });
  }
  return { deck: sc.deck, seed: sc.seed, rests: rests.length, over: S(win).over, failures };
}

// Board-activated abilities (Krenko's "{T}: create X Goblins"; a Siege-Gang / Skirk Prospector sac
// outlet). The engine enumerates these INSIDE its plans and commits them with the ordinary
// `cast=<name>` verb — but the card is on the BATTLEFIELD, and the GUI's cast route only reaches the
// HAND, so for a long time there was no way for a human to use one at all: clicking Krenko did
// nothing. This drives the real board click and asserts the ability actually resolves, which is the
// only way to catch the affordance silently disappearing again (the plan-driven scenarios below
// commit plan indices directly, so they would never notice).
async function testBoardActivation() {
  const fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const win = buildDom(); await settle(win);
  await startGame(win, { deck: 'Goblins', seed: 2, turns: 8 });
  const st = () => S(win);
  let guard = 0;
  while (guard++ < 40 && !(st().decision.type === 'main_phase' && st().decision.turn === 3)) {
    await stepForward(win, 'ai');
  }
  chk(st().decision && st().decision.turn === 3, 'reached Goblins s2 turn 3 (Krenko in play via a Lackey hit)');
  if (fails.length) return fails;

  const thumb = (n) => [...win.document.getElementById('board').querySelectorAll('.thumb[data-name]')]
                        .find(t => t.dataset.name === n);
  const krenko = thumb('Krenko, Mob Boss');
  chk(!!krenko, 'Krenko is rendered on the battlefield');
  chk(krenko && krenko.hasAttribute('data-activate'), 'Krenko is CLICKABLE (data-activate) — the ability is reachable');
  chk(thumb('Goblin Lackey') && !thumb('Goblin Lackey').hasAttribute('data-activate'),
      'a permanent with no activated ability stays non-clickable');
  if (!krenko || !krenko.hasAttribute('data-activate')) return fails;

  krenko.dispatchEvent(new win.MouseEvent('click', { bubbles: true }));
  chk(st().plan.length === 1 && st().plan[0].kind === 'activate', 'one click queues an activate entry');
  krenko.dispatchEvent(new win.MouseEvent('click', { bubbles: true }));
  chk(st().plan.length === 0, 'clicking again (past the cap) removes it');
  krenko.dispatchEvent(new win.MouseEvent('click', { bubbles: true }));

  const goblinsBefore = (st().decision.me.battlefield || [])
    .filter(p => !p.is_land && !/Token/i.test(p.name)).length;
  await win.commitLine(); await settle(win);
  chk(!S(win).hadReject, 'the activation line is ACCEPTED by the engine (not a reject)');
  const after = st().decision || st().result;
  const tokens = (((after || {}).me || {}).battlefield || []).filter(p => /Goblin Token/i.test(p.name)).length;
  // Krenko: X = Goblins controlled at resolution (itself + the other Goblins, tokens not yet there).
  chk(tokens === goblinsBefore, `the ability RESOLVED: ${tokens} tokens for ${goblinsBefore} Goblins controlled`);
  return fails;
}

// The QUEUE-time activation picker (a source offering several distinct activations). It is not an
// engine decision type, so viewer_decision_types_check.js cannot see it and it renders only on
// boards a walked game may never reach — exactly the shape of the lackey_put ReferenceError. Render
// it directly against each verb so a break is caught whatever the deck does.
function testActivationPicker(win) {
  const fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const cases = [
    { src: 'Stoneforge Mystic', opts: [{ verb: 'sfput', mode: null, name: 'Colossus Hammer' },
                                       { verb: 'sfput', mode: null, name: 'Bonesplitter' }] },
    { src: "Umezawa's Jitte",   opts: [{ verb: 'jittemode', mode: 1, name: "Umezawa's Jitte" },
                                       { verb: 'jittemode', mode: 2, name: "Umezawa's Jitte" }] },
    { src: 'Balan, Wandering Knight', opts: [{ verb: 'attachall', mode: null, name: 'Balan, Wandering Knight' },
                                             { verb: 'cast', mode: null, name: 'Balan, Wandering Knight' }] },
  ];
  cases.forEach(pick => {
    let html;
    try { html = win.__actpick(pick); }
    catch (e) { fails.push(`${pick.src}: threw ${e && e.message ? e.message : e}`); return; }
    chk(html && html.length > 0, `${pick.src}: picker rendered EMPTY`);
    const n = (html.match(/data-actopt="\d+"/g) || []).length;
    chk(n === pick.opts.length, `${pick.src}: rendered ${n} option tiles, expected ${pick.opts.length}`);
    chk(/data-actopt="-1"/.test(html), `${pick.src}: no Cancel button`);
  });
  return fails;
}

// Equipment: the KittyEquipment deck's central action. The engine enumerates an Equip for every
// legal (Equipment, host) pair, but nothing told the GUI which permanent to click or which LineSpec
// verb to write, so an equipment deck simply could not be played by hand (user-reported 2026-08-23).
// Drives the whole chain: board click -> `equip=<name>` line -> engine accept -> host picked in the
// choose dialog -> the Equipment ends up ATTACHED and renders stacked behind its host like an Aura.
async function testEquip() {
  const fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const win = buildDom(); await settle(win);
  await startGame(win, { deck: 'KittyEquipment', seed: 2, turns: 8 });
  const st = () => S(win);
  let guard = 0;
  while (guard++ < 40 && !(st().decision && st().decision.type === 'main_phase' && st().decision.turn === 3)) {
    if (!(await stepForward(win, 'ai'))) break;
  }
  chk(st().decision && st().decision.turn === 3, 'reached KittyEquipment s2 turn 3');
  if (fails.length) return fails;

  const thumb = (n) => [...win.document.getElementById('board').querySelectorAll('.thumb[data-name]')]
                        .find(t => t.dataset.name === n);
  const spear = thumb('Shadowspear');
  chk(!!spear, 'Shadowspear is on the battlefield');
  chk(spear && spear.hasAttribute('data-activate'), 'an Equipment in play is CLICKABLE (data-activate)');
  if (!spear || !spear.hasAttribute('data-activate')) return fails;

  spear.dispatchEvent(new win.MouseEvent('click', { bubbles: true }));
  const q = st().plan[0];
  chk(st().plan.length === 1 && q && q.kind === 'activate' && q.verb === 'equip',
      'one click queues an activate entry carrying the equip verb');
  chk(win.LineBuild.encodeLine(st().plan) === 'equip=Shadowspear',
      `the line encodes as equip=<name>, got "${win.LineBuild.encodeLine(st().plan)}"`);

  await win.commitLine(); await settle(win);
  // A multi-host board resolves as `choose`: walk the dialog by clicking the first option of each
  // dimension until it closes (the same clicks a human makes).
  for (let i = 0; i < 6; i++) {
    const pick = win.document.querySelector('#decpanel .varpick');
    if (!pick) break;
    pick.dispatchEvent(new win.MouseEvent('click', { bubbles: true }));
    await settle(win);
  }
  chk(!S(win).hadReject, 'the equip line is ACCEPTED by the engine (not a reject)');
  const after = st().decision;
  const bf = (after && after.me && after.me.battlefield) || [];
  const eq = bf.find(p => p.name === 'Shadowspear');
  chk(eq && eq.attached_to > 0, `Shadowspear is ATTACHED after the commit (attached_to=${eq && eq.attached_to})`);
  // ...and is drawn the way an Aura is: inside its host's .enchgroup stack, not free-floating.
  const grouped = [...win.document.querySelectorAll('#board .enchgroup .thumb[data-name]')]
                    .some(t => t.dataset.name === 'Shadowspear');
  chk(grouped, 'the attached Equipment renders stacked behind its host (.enchgroup), like an Aura');
  return fails;
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
    // Queue-time activation picker (fast, DOM-only).
    const apFails = testActivationPicker(win);
    if (apFails.length) { anyFail = true; console.log(`✗ activation picker: ${apFails.length} fail`); apFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ activation picker renders for sfput / jittemode / attachall'); }
  }
  // Board-activated ability reachable + resolving (needs a real game walk, so it runs on its own).
  {
    let baFails;
    try { baFails = await testBoardActivation(); }
    catch (e) { console.error(`✗ board activation: harness error: ${e.stack || e}`); process.exit(2); }
    if (baFails.length) { anyFail = true; console.log(`✗ board activation: ${baFails.length} fail`); baFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ board-activated ability (Krenko: clickable → queued → accepted → tokens)'); }
  }
  // Equip reachable + resolving + displayed like an Aura (needs a real game walk).
  {
    let eqFails;
    try { eqFails = await testEquip(); }
    catch (e) { console.error(`✗ equip: harness error: ${e.stack || e}`); process.exit(2); }
    if (eqFails.length) { anyFail = true; console.log(`✗ equip: ${eqFails.length} fail`); eqFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ equip (Equipment: clickable → equip= line → accepted → attached → stacked on its host)'); }
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
  // Panel-render errors collected across every walked frame. Deduped by type+message: a broken panel
  // throws once per time the deck plays into it, and one line per distinct break is what you want.
  {
    const uniq = [...new Set(PANEL_ERRORS)];
    if (uniq.length) {
      anyFail = true;
      console.log(`✗ decision panel render: ${uniq.length} distinct error(s) — the modal cannot be drawn, so the game is stuck on a dead board:`);
      uniq.forEach(m => console.log('  - ' + m));
    } else {
      console.log(`✓ decision panels rendered clean for every type reached (${[...RENDERED_TYPES].sort().join(' ') || 'none'})`);
    }
  }
  process.exit(anyFail ? 1 : 0);
})();
