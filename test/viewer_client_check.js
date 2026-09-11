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
      // serverApi from the REAL server module, so the client's stale-server handshake
      // (checkServerFresh) is exercised on the same value a live server would send -- and so a
      // bump to one side without the other shows up here rather than as a banner in the user's face.
      out = { decks: server.listDecks(), binExists: fs.existsSync(server.BIN),
              serverApi: server.SERVER_API };
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
    // CLIENT_API is a top-level `const` (lexical, not a window property) -- the stale-server
    // handshake check needs the client's own number, not a copy of it that could drift.
    + 'window.__clientApi = CLIENT_API;'
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
    + 'window.__actpick = activationPickerHtml;'
    + 'window.__blink = { arm: toggleActivate, at: blinkAtTarget };';
  // NB renderBoard/renderHand are function declarations, so they already are window properties --
  // the MDFC test below drives them directly against a synthetic decision.
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

  // ---- FULL LINE ORDER: casts AND board activations, marked with a leading "*" -----------------
  // (2026-09-10.) `cast_order_canonical` lists hand casts only, so a queued board ACTIVATION was
  // filtered out of the pin entirely and the engine ran it in its trailing pass, ordered by
  // battlefield index. When the queued entries and the matched plan's ACTIONS are the same
  // multiset, the whole sequence is pinned instead, with the "*" marker that tells
  // ReorderPlanCasts to widen its reorderable slot set. Every plan above carries no `actions`
  // array, which is exactly the fallback case -- those assertions are the back-compat half.
  const decF = { type: 'main_phase', turn: 6, main_ordinal: 7,
                 me: { life: 20, battlefield: [] }, opponent: { life: 20, battlefield: [] },
                 plans: [{ index: 0, casts: ['Wild Growth', 'Clue Token', 'Emiel the Blessed'],
                           cast_order_canonical: ['Wild Growth', 'Clue Token'],
                           actions: [{ card: 'Wild Growth' },
                                     { card: 'Emiel the Blessed', activate: true, verb: 'blink' },
                                     { card: 'Clue Token', activate: true }] }] };
  function seedF(planOrder) {
    S.choices = [1, 1]; S.steps = [{ n: 1 }, { n: 1 }];
    S.checkpoints = [{ histLen: 0 }, { histLen: 0 }, { histLen: 0 }];
    S.history = []; S.castOrder = {}; S.decision = decF; S.prev = null; S.busy = false;
    S.plan = planOrder.map(n => ({ name: n, kind: n === 'Wild Growth' ? 'permanent' : 'activate' }));
  }
  // "crack the Clue to draw, THEN blink" -- the order the enumerated vector never offers.
  seedF(['Clue Token', 'Wild Growth', 'Emiel the Blessed']);
  co.apply(0, 'full order');
  chk(JSON.stringify(S.castOrder['7'])
      === JSON.stringify(['*', 'Clue Token', 'Wild Growth', 'Emiel the Blessed']),
      'a queue matching the plan ACTIONS pins the full order with the * marker');
  S.busy = false; co.rollback();
  chk(!('7' in S.castOrder), 'undo drops the full-order side-channel entry too');
  // A queued LAND and a Land's Edge discard are not actions and must not enter the pin (nor break
  // the multiset match): plan0.land / the landsedge count carry them.
  seedF(['Clue Token', 'Wild Growth', 'Emiel the Blessed']);
  S.plan.unshift({ name: 'Aether Hub', kind: 'land' });
  S.plan.push({ name: 'Mountain', kind: 'le' });
  co.apply(0, 'full order + land');
  chk(JSON.stringify(S.castOrder['7'])
      === JSON.stringify(['*', 'Clue Token', 'Wild Growth', 'Emiel the Blessed']),
      'the land drop and a Land’s Edge discard stay out of the pinned sequence');
  // A queue that does NOT name every action (an implicit sac-for-mana, a Vial deploy) says nothing
  // about those actions, so it falls back to the historical cast-only pin -- never a partial
  // full-order list that would shuffle an action the human never sequenced.
  seedF(['Clue Token', 'Wild Growth']);
  co.apply(0, 'partial');
  chk(JSON.stringify(S.castOrder['7']) === JSON.stringify(['Clue Token', 'Wild Growth']),
      'a queue that misses an action falls back to the cast-only pin (no * marker)');
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

// CLUE FUSE, the GLOBAL option (docs/design/viewer-line-macros.md). USER, after the first cut:
// "I think I would much prefer to have the clue creation as a global option. It's kind of a pain to
// constantly have pop-ups."
//
// This is the only layer that can see the regression that prompted it. The line-build check drives
// linebuild.js, which knows nothing about options or pickers; the protocol and validate checks see
// the committed LINE, which is identical either way. The defect lived entirely in how many entries
// the click path offers: offering "Investigate & crack" beside the plain Investigate made
// `opts.length` 2, and toggleActivate opens a modal on `> 1` -- so a one-click activation became a
// dialog on every single investigate, on the deck that investigates most.
//
// PINS: the pair collapses to exactly ONE option in BOTH states (no dialog either way, which is what
// "global option" has to mean); the surviving option is the right flavour; the default is ON; the
// pref round-trips through localStorage (it must outlive a game AND a server restart); a source with
// no investigate is untouched; and clicking a fused Investigate OFF takes its deferred crack with it
// rather than orphaning it.
function testClueFuseOption(win) {
  const S = win.__getS(), fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  let di = 9000;
  // A synthetic main-phase frame whose land offers ONE activation: an Investigate. This is the exact
  // shape the regression hit -- the engine enumerates one action, the viewer used to offer two.
  const frame = (extraActions) => {
    S.decision = {
      type: 'main_phase', decision_index: ++di, turn: 3, phase: 'pre_main',
      me: { hand: [], battlefield: [{ name: 'Conservatory', num: 13, is_land: true }],
            land_drops_left: 0 },
      opponent: { life: 20 },
      plans: [{ index: 0, summary: 'investigate',
                actions: [{ card: 'Conservatory', activate: true, makes_clue: true }]
                          .concat(extraActions || []) }],
    };
    S.plan = []; S.over = false;
    return S.decision;
  };

  // ---- the default, and the collapse in BOTH states -------------------------------------------
  try { win.localStorage.removeItem('mdt_queue'); } catch (e) { /* jsdom always has it */ }
  chk(win.clueFuseEnabled() === true, 'clue fuse does not default ON (the user asked for it ON)');

  frame();
  let opts = win.clickActivationOptions('Conservatory');
  chk(opts.length === 1, `default ON: click offers ${opts.length} options, expected 1 (>1 opens the modal)`);
  chk(opts.length === 1 && !!opts[0].fuseClue, 'default ON: the surviving option is not the FUSED one');

  win.setQueueOpt('clue_fuse', false);
  chk(win.clueFuseEnabled() === false, 'setQueueOpt(false) did not take effect');
  frame();
  opts = win.clickActivationOptions('Conservatory');
  chk(opts.length === 1, `OFF: click offers ${opts.length} options, expected 1 (OFF must not pop a dialog either)`);
  chk(opts.length === 1 && !opts[0].fuseClue && !!opts[0].makesClue,
      'OFF: the surviving option is not the PLAIN investigate');

  // ---- persistence: it has to outlive a reload, not just a render -----------------------------
  // Asserted through the STORE, because that is what a new page load reads. A pref kept only in S
  // would pass every in-session check and still be gone after a server restart.
  let raw = null;
  try { raw = JSON.parse(win.localStorage.getItem('mdt_queue') || '{}'); } catch (e) { /* below */ }
  chk(raw && raw.clue_fuse === false, `pref did not persist to localStorage (got ${JSON.stringify(raw)})`);
  win.setQueueOpt('clue_fuse', true);
  try { raw = JSON.parse(win.localStorage.getItem('mdt_queue') || '{}'); } catch (e) { raw = null; }
  chk(raw && raw.clue_fuse === true, 'flipping the pref back did not persist');

  // ---- a source with no investigate is untouched ------------------------------------------------
  S.decision = {
    type: 'main_phase', decision_index: ++di, turn: 3, phase: 'pre_main',
    me: { hand: [], battlefield: [{ name: 'Emiel the Blessed', num: 21 }], land_drops_left: 0 },
    opponent: { life: 20 },
    plans: [{ index: 0, summary: 'blink', actions: [
      { card: 'Emiel the Blessed', activate: true, verb: 'blink', blink_target: 10,
        blink_target_name: 'Cloud of Faeries', blink_count: 1, repeatable: true }] }],
  };
  S.plan = [];
  const blinkOpts = win.clickActivationOptions('Emiel the Blessed');
  chk(blinkOpts.length === 1 && blinkOpts[0].verb === 'blink',
      `a non-investigate source was disturbed: ${JSON.stringify(blinkOpts.map(o => o.verb))}`);

  // ---- clicking a fused Investigate OFF takes its crack with it ---------------------------------
  // The cap path in toggleActivate used a bare splice, which removed the Investigate and left the
  // DEFERRED crack queued as an orphan segment -- a line that sacrifices a Clue nothing made.
  frame();
  win.toggleActivate('Conservatory');
  const queued = S.plan.length;
  chk(queued === 2, `a fused click queued ${queued} entries, expected 2 (investigate + deferred crack)`);
  chk(S.plan.length === 2 && S.plan[0].fuse === 'clue' && S.plan[1].fused === 'clue'
      && S.plan[1].defer === true, 'the fused pair is not (investigate, deferred crack)');
  win.toggleActivate('Conservatory');            // at the cap -> this removes it again
  chk(S.plan.length === 0,
      `clicking the fused Investigate off left ${S.plan.length} entr${S.plan.length === 1 ? 'y' : 'ies'} `
      + `(an orphaned crack), expected 0: ${JSON.stringify(S.plan.map(p => p.name))}`);
  return fails;
}

// LOOP THE LAST K COMMITTED LINES (docs/design/viewer-line-macros.md, feature 3).
// USER 2026-09-11, mid-game on EldraziDisplacerFlicker: "it would be nice if loops like kitchen
// activate -> untap were possible to repeat".
//
// The EXPANSION is linebuild's and is pinned there (viewer_linebuild_check.js); what only a DOM can
// see is the half that decides whether the human can reach it at all: is the control offered, is it
// offered only when it is real, does the central dialog wire through to the queue, and does the
// committed-segment log it reads survive an undo. Every one of those fails SILENTLY -- a control
// that never appears looks exactly like a feature that was never built, which is how the loop the
// user asked for would ship inert.
//
// DOM-only, off synthetic frames: the loop's own arithmetic is engine truth and is driven for real
// against the user's saved game in test/viewer_line_macros_check.py.
function testLoopMacro(win) {
  const S = win.__getS(), fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  let di = 7000;
  const frame = (turn) => {
    S.decision = {
      type: 'main_phase', decision_index: ++di, turn: turn || 4, phase: 'pre_main', main_ordinal: 9,
      me: { hand: [], battlefield: [{ name: 'Kitchen', num: 29, is_land: true }], land_drops_left: 0 },
      opponent: { life: 20 }, plans: [{ index: 0, summary: 'pass', actions: [] }],
    };
    S.plan = []; S.over = false; S.loopPick = null; S.macroRun = null;
    return S.decision;
  };
  // The two lines one EDF iteration commits as: the fused Investigate, then the deferred crack
  // together with the blink that untaps the land it just tapped.
  const segA = () => [{ name: 'Kitchen', src: 'Kitchen', kind: 'activate', verb: 'cast', fuse: 'clue' }];
  const segB = () => [{ name: 'Clue Token', src: 'Clue Token', kind: 'activate', verb: 'cast',
                        defer: true, fused: 'clue' },
                      { name: 'Eldrazi Displacer', src: 'Eldrazi Displacer', kind: 'activate',
                        verb: 'blink', repeatable: true, blinkTarget: 42, blinkCount: 1 }];
  const commit = (entries, turn, at) => ({ at, turn: turn || 4, entries });

  // ---- not offered when there is nothing to loop ------------------------------------------------
  frame(); S.steps = []; S.committed = [];
  chk(win.loopAvailable() === false, 'the loop control is offered with no committed line');

  // ---- offered once the loop has been played once -----------------------------------------------
  S.steps = [{ n: 1 }, { n: 1 }];
  S.committed = [commit(segA(), 4, 1), commit(segB(), 4, 2)];
  chk(win.loopAvailable() === true, 'the loop control is NOT offered after committing a loopable line');
  chk(win.loopTail().length === 2, `loopTail saw ${win.loopTail().length} lines, expected 2`);

  // ...and it is a real BOARD control, not a history-panel affordance (the play-viewer decision
  // principle: central dialog or board clicks, never the history panel).
  win.renderBoard();
  chk(!!win.document.getElementById('loopp'), 'the ⟲ Loop button does not render in the plan bar');
  chk(!win.document.getElementById('hist').querySelector('[id^=loop]'),
      'a loop control leaked into the history panel');

  // ---- the two-step central dialog reaches the queue --------------------------------------------
  win.document.getElementById('loopp').click();
  chk(S.loopPick && S.loopPick.k === null, 'clicking ⟲ Loop did not open the "how many lines?" step');
  let panel = win.document.getElementById('decpanel');
  chk(panel.className.indexOf('modal') >= 0 && panel.querySelectorAll('[data-loopk]').length === 2,
      `step 1 offered ${panel.querySelectorAll('[data-loopk]').length} line counts, expected 2 (k=1,2)`);
  panel.querySelector('[data-loopk="2"]').click();
  chk(S.loopPick && S.loopPick.k === 2, 'picking k did not advance to the "how many times?" step');
  panel = win.document.getElementById('decpanel');
  const ns = panel.querySelectorAll('[data-loopn]');
  chk(ns.length > 0, 'step 2 offered no repeat counts');
  // Pick ×3 and assert the QUEUE, which is the whole product: 2 lines x 3 = 6 committable segments,
  // each still the two separate lines a fused iteration has to be.
  const three = panel.querySelector('[data-loopn="3"]');
  chk(!!three, 'step 2 has no ×3');
  if (three) {
    three.click();
    chk(S.loopPick === null, 'the loop dialog stayed open after the pick');
    const segs = win.LineBuild.encodeSegments(S.plan);
    chk(segs.length === 6, `loop ×3 queued ${segs.length} lines, expected 6`);
    chk(segs.every((s, i) => s === (i % 2 ? 'cast=Clue Token;blink=Eldrazi Displacer@42*1' : 'cast=Kitchen')),
        `loop ×3 queued the wrong lines: ${JSON.stringify(segs)}`);
    chk(S.macroRun && S.macroRun.kind === 'loop' && S.macroRun.segs === 6,
        `macroRun not set for the honest stop: ${JSON.stringify(S.macroRun)}`);
  }

  // ---- not offered while a queue is half-built (that is ⟲ Repeat's job) -------------------------
  chk(win.loopAvailable() === false, 'the loop control is still offered with a queue already built');

  // ---- THE HONEST STOP. A macro that meets a line the board cannot pay must say how much of it
  // really happened, and must not leave the rest queued to re-commit on the next click. Driven
  // through handleVerdict's own reject branch (the live path), with two of six lines landed.
  S.macroRun = { kind:'loop', k:2, n:3, segs:6, perIter:2, done:2 };
  S.plan = win.LineBuild.loopBlock([segA(), segB()], 3);
  win.handleVerdict({ verdict:'illegal', reason:"'Clue Token' is not in hand",
                      decision:{ plans:[] } }, 'cast=Kitchen;cast=Clue Token');
  const vtxt = win.document.getElementById('verdict').textContent;
  chk(/2 of 6 lines committed/.test(vtxt), `the reject panel does not say what landed: ${vtxt.slice(0, 160)}`);
  chk(/1 complete iteration\b/.test(vtxt), `the reject panel does not count iterations: ${vtxt.slice(0, 200)}`);
  chk(/stay played/.test(vtxt), 'the reject panel does not say the committed lines stay played');
  chk(S.plan.length === 0, `the stale rest of the macro stayed queued (${S.plan.length} entries)`);
  chk(S.macroRun === null, 'macroRun survived the stop and would mis-count the next one');
  // ...and an ordinary (non-macro) rejection is untouched: no count, no queue wipe.
  S.plan = segA().concat(); S.macroRun = null;
  win.handleVerdict({ verdict:'illegal', reason:'nope', decision:{ plans:[] } }, 'cast=Kitchen');
  chk(!/lines committed/.test(win.document.getElementById('verdict').textContent),
      'a plain rejection grew a macro count');
  chk(S.plan.length === 1, 'a plain rejection cleared the queue');
  S.plan = [];

  // ---- a committed line from ANOTHER TURN is not part of this turn's loop ------------------------
  frame(5); S.steps = [{ n: 1 }, { n: 1 }];
  S.committed = [commit(segA(), 4, 1), commit(segB(), 4, 2)];
  chk(win.loopAvailable() === false, "last turn's lines are offered as this turn's loop");

  // ---- a once-per-turn resource is refused rather than offered and then rejected -----------------
  frame(4); S.steps = [{ n: 1 }, { n: 1 }];
  S.committed = [commit([{ name: 'Kitchen', kind: 'land' }], 4, 1), commit(segB(), 4, 2)];
  chk(win.loopTail().length === 1,
      `loopTail crossed a land drop (${win.loopTail().length} lines, expected 1)`);

  // ---- UNDO drops the records whose step is gone --------------------------------------------------
  // Keyed on S.steps.length, so this needs no undo hook -- which is the point: the castOrder and
  // firebreathe side channels each grew a bug from a hook that fell out of step.
  frame(4); S.steps = [{ n: 1 }, { n: 1 }];
  S.committed = [commit(segA(), 4, 1), commit(segB(), 4, 2)];
  S.steps.pop();
  chk(win.pruneCommitted().length === 1,
      `pruneCommitted kept ${win.pruneCommitted().length} records after an undo, expected 1`);
  S.steps = []; S.committed = []; S.plan = []; S.macroRun = null;
  return fails;
}

// THE PRIMARY BUTTON THAT CHANGES MEANING UNDER YOUR FINGER (USER 2026-09-11, seed 16 T4:
// "it goes to the 2nd phase for no reason and I can't finish the combo").
// =================================================================================================
// `#commit` is ONE button labelled `empty ? 'Pass phase' : 'Commit Line'`. The instant a line
// commits, S.plan empties, renderPlanbar redraws, and the same element -- same id, same position,
// same class -- becomes a PHASE PASS that discards the floating pool. Grinding a 60-segment
// Emiel/Cloud loop means clicking that one pixel over and over, so the click that lands just after a
// commit resolves ends the main phase instead of committing anything.
//
// DRIVEN AT THE USER'S OWN FRAME (seed 16 / gi 15, turn 4, main_ordinal 62 -- the two-Aura line
// `Overgrowth → Brushland #1, Wild Growth → Brushland #1`, plan 742). Pre-fix this appended
// `[742, -1]` and landed post_main with the pool gone; the engine replay of `...,742` alone stays in
// pre_main at ordinal 63 with {G:6,C:2}, so the extra pass was purely the client's.
//
// ONLY A DOM CAN SEE IT. The protocol sweep replays plan indices, the save audit replays a finished
// stream, and the line-builder knows nothing of buttons -- none of them can produce a click that
// lands on a button whose label changed a moment earlier. The four assertions below are the guard's
// whole contract, and three of them are ways the guard could be worse than useless:
//   * the rapid click is refused AND SAYS WHY (the bug);
//   * a genuine pass still works in two deliberate clicks (not a lockout);
//   * a DOUBLE-click never passes, however fast -- a naive arm/confirm would let click 1 arm and
//     click 2 confirm, reproducing the bug through the fix;
//   * a pass with an EMPTY pool stays ONE click (the common case must not grow a confirmation).
const AURA_PREFIX = ('1,0,-1,-1,24,-1,-1,55,-1,-1,4,0,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,'
  + '2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,6,13,12,29,30,62,125,364,365,366')
  .split(',').map(Number);

async function testPassGuard() {
  const fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const sleep = (ms) => new Promise(r => setTimeout(r, ms));
  const win = buildDom(); await settle(win);
  const $ = (id) => win.document.getElementById(id);
  const opt = Array.from($('deck').options).find(o => o.value.replace(/\.[^.]+$/, '') === 'EldraziDisplacerFlicker');
  if (!opt) { console.log('  SKIP pass guard: EldraziDisplacerFlicker not listed'); return fails; }
  $('deck').value = opt.value; $('seed').value = '16'; win.fillVersions();
  const st = S(win);
  // Inject the prefix rather than replay 65 clicks: the bug is about ONE click on ONE frame, and
  // the frame is reached identically either way (the protocol is a stateless replay from choices).
  st.choices = AURA_PREFIX.slice();
  st.steps = AURA_PREFIX.map(() => ({ n: 1 }));
  await win.step(); await settle(win);
  const d0 = st.decision;
  if (!d0 || d0.type !== 'main_phase' || d0.main_ordinal !== 62) {
    console.log('  SKIP pass guard: seed 16 no longer reaches T4 ordinal 62 at this prefix'
                + (d0 ? ` (got ${d0.type} ord=${d0.main_ordinal})` : ' (no decision)'));
    return fails;
  }
  const bru = (d0.me.battlefield || []).find(o => o.name === 'Brushland');
  if (!bru) { console.log('  SKIP pass guard: no Brushland on that board'); return fails; }
  win.tryEnchantDrop('Overgrowth', 'permanent', bru.num);
  win.tryEnchantDrop('Wild Growth', 'permanent', bru.num);
  chk(st.plan.length === 2, `the two Auras queued (${st.plan.length} entries)`);
  chk($('commit').textContent === 'Commit Line', 'the primary button reads "Commit Line" with a line queued');
  const n0 = st.choices.length;
  await win.commitLine(); await settle(win);
  chk(st.choices.length === n0 + 1 && st.choices[n0] === 742,
      `the two-Aura line committed as plan 742 (appended ${JSON.stringify(st.choices.slice(n0))})`);
  const d1 = st.decision;
  chk(d1 && d1.phase === 'pre_main', `the commit stays in pre_main (got ${d1 && d1.phase})`);
  const pool1 = win.floatingTotal(d1);
  chk(pool1 > 0, `there is still a floating pool to lose (${pool1})`);
  // ...and the button has silently become a phase pass. This is the setup, not the bug.
  chk($('commit').textContent === 'Pass phase',
      `the same button now reads "${$('commit').textContent}" -- the meaning flip this guards`);

  // ADJACENT, FOUND WHILE REPRODUCING: `decision.plans` is a RANKED TOP SLICE past the emit cap,
  // so array POSITION and `.index` stop agreeing -- this frame emits 200 plans carrying indices up
  // to 771. applyAccepted used to read `plans[planIndex]`, i.e. some OTHER plan, and decide the
  // human's full-order pin against a line the engine was not applying. Pinned on the real frame
  // because a synthetic one always has position == index and can never show it.
  chk(d0.plans_total && d0.plans.length < d0.plans_total,
      `this frame no longer overflows the emit cap (${d0.plans.length}/${d0.plans_total}) -- `
      + 'the positional-lookup hazard is not being exercised');
  chk(d0.plans.some((p, i) => p.index !== i),
      'the emitted plan list is in index order here, so position==index and the lookup is untested');
  chk(d0.plans[742] === undefined && !d0.plans.some(p => p.index === 742),
      'plan 742 is unexpectedly present; the cap-overflow case moved');
  // ...and the fix itself, on this real frame. The emitted list is a contiguous head followed by
  // ranked EXTRAS with high indices (measured here: positions 0..176 match, then 177 holds index
  // 216, 178 holds 432, ...). That gives both failure modes of a positional read, and both are
  // pinned:
  const pos = d0.plans.findIndex((p, i) => p.index !== i);
  chk(pos > 0, 'the emitted plan list is wholly in index order here -- the hazard is not exercised');
  if (pos > 0) {
    const extra = d0.plans[pos];
    //  (a) RECOVERED: a ranked extra's index is past the array, so `plans[idx]` read undefined and
    //      NO pin was emitted for it. planByIndex finds it, so the pin fires again.
    chk(d0.plans[extra.index] === undefined,
        `plans[${extra.index}] is addressable positionally -- the recovery case moved`);
    chk(win.planByIndex(d0, extra.index) === extra,
        `planByIndex(${extra.index}) did not return the plan carrying that index`);
    //  (b) WRONG PLAN: position `pos` holds a plan whose index is NOT `pos`, so committing plan
    //      `pos` used to read that stranger's actions and decide the pin against them.
    chk(d0.plans[pos].index !== pos, `plans[${pos}].index is ${pos} -- the wrong-plan case moved`);
    chk(win.planByIndex(d0, pos) === null,
        `planByIndex(${pos}) must be null (that index is not emitted), not a stranger`);
  }
  chk(win.planByIndex(d0, 742) === null, 'planByIndex must return null for a plan past the cap');
  if (fails.length) return fails;

  // 1) THE BUG: a click landing right after the commit must not pass, and must say why.
  $('commit').click(); await settle(win);
  chk(st.choices.length === n0 + 1,
      `the immediate click PASSED THE PHASE (choices ${JSON.stringify(st.choices.slice(n0))})`);
  chk(st.decision.phase === 'pre_main', `the immediate click advanced to ${st.decision.phase}`);
  chk(win.floatingTotal(st.decision) === pool1, 'the immediate click discarded the floating pool');
  chk(/right after your line committed/.test($('verdict').textContent || ''),
      'the refusal does not explain itself (a silent no-op reads as a frozen viewer)');

  // 2) A DOUBLE-click, past the dead time, must STILL not pass: the confirming click has to dwell.
  await sleep(800);
  $('commit').click(); $('commit').click(); await settle(win);
  chk(st.choices.length === n0 + 1,
      `a double-click passed the phase (choices ${JSON.stringify(st.choices.slice(n0))})`);
  chk(st.decision.phase === 'pre_main', 'a double-click advanced the phase');

  // 3) A DELIBERATE pass still works: arm, dwell, confirm.
  chk($('commit').textContent.indexOf('lose') > 0,
      `the armed button does not say what is at stake (reads "${$('commit').textContent}")`);
  await sleep(500);
  $('commit').click(); await settle(win);
  chk(st.choices.length === n0 + 2 && st.choices[n0 + 1] === -1,
      `a deliberate second click did NOT pass (choices ${JSON.stringify(st.choices.slice(n0))})`);
  chk(st.decision && st.decision.phase === 'post_main',
      `the deliberate pass did not reach post_main (got ${st.decision && st.decision.phase})`);

  // 4) A pass with an EMPTY pool stays ONE click -- the common case must not grow a confirmation.
  const d3 = st.decision;
  if (win.floatingTotal(d3) === 0 && d3.type === 'main_phase') {
    const n1 = st.choices.length;
    await sleep(800);
    $('commit').click(); await settle(win);
    chk(st.choices.length > n1, 'a pass with NO floating mana was needlessly guarded (one click must do)');
  } else {
    console.log('  note: post-pass frame still holds mana; the empty-pool arm is covered by unit logic only');
  }
  return fails;
}

// STALE SERVER HANDSHAKE (tools/play/server.js SERVER_API <-> index.html CLIENT_API).
// index.html is re-read from disk on every load; `node server.js` is not, so a viewer left open
// across a server.js change runs a NEW client against an OLD server -- same routes, same payloads,
// different behaviour underneath. That is not hypothetical: a server started before the per-game
// engine pin (cee518af) runs build/Release/mtg unpinned, and a rebuild by anything else mid-session
// changes the engine under the game AND under the save, which re-runs the whole choice stream.
//
// The load-bearing case is the MISSING field: a server too old to know about the handshake cannot
// report a version, so `undefined` has to read as stale rather than as fine. Asserted here because
// getting it backwards makes the guard inert against precisely the servers it exists to catch.
function testStaleServerHandshake(win) {
  const fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const el = win.document.getElementById('staleserver');
  chk(!!el, 'there is no #staleserver banner element to warn in');
  chk(typeof win.__clientApi === 'number', 'index.html declares no CLIENT_API to compare against');
  // The two constants must be in step on THIS tree, or the banner fires for every user on every
  // load -- a false alarm is how a real one gets ignored.
  chk(win.__clientApi === server.SERVER_API,
      `CLIENT_API (${win.__clientApi}) != SERVER_API (${server.SERVER_API}) -- bump both together`);
  if (!el) return fails;
  const shown = () => el.style.display !== 'none' && el.innerHTML.length > 0;

  el.style.display = 'none'; el.innerHTML = '';
  win.checkServerFresh({ decks: [], binExists: true, serverApi: win.__clientApi });
  chk(!shown(), 'a CURRENT server was reported as stale');

  el.style.display = 'none'; el.innerHTML = '';
  win.checkServerFresh({ decks: [], binExists: true });          // the pre-handshake server
  chk(shown(), 'a server with NO serverApi was treated as current -- the guard is inert against '
             + 'exactly the servers it exists to catch');
  chk(/restart/i.test(el.textContent), 'the stale banner does not tell the user to restart');

  el.style.display = 'none'; el.innerHTML = '';
  win.checkServerFresh({ decks: [], binExists: true, serverApi: win.__clientApi - 1 });
  chk(shown(), 'an OLDER reported serverApi was treated as current');
  // Dismissible, never modal: the game underneath is still playable and the user may be mid-turn.
  const x = win.document.getElementById('stalex');
  chk(!!x, 'the stale banner cannot be dismissed');
  if (x) { x.click(); chk(!shown(), 'dismissing the stale banner did not hide it'); }
  el.style.display = 'none'; el.innerHTML = '';
  return fails;
}

// MDFC LAND BACK on a nonland front (Turntimber Symbiosis // Turntimber, Serpentine Wood). The hand
// card's `kind` is the FRONT's ("nonpermanent"), so every route on the thumb -- double-click, drag --
// casts the {4}{G}{G}{G} sorcery, and the land drop the engine enumerates as `land=<front name>` had
// no affordance at all: user-reported 2026-08-24 with a saved rejection artifact (StompySurprise s5
// gi4 t2). DOM-only, driven off a synthetic decision, because the bug is entirely in the palette --
// the engine accepted `land=Turntimber Symbiosis;cast=Priest of Titania` all along.
// BLINK TARGETING ON THE BOARD (2026-09-07). The choice a blink outlet asks is "which creature",
// and that creature is on the board -- so clicking the outlet ARMS it and clicking the creature
// commits, instead of a grid of card images in a modal (USER: "Can we make the flicker decision
// targeting on the board rather than a dialog?"). Pins: arming highlights exactly the legal
// targets; a board click queues the right target's option; a repeatable outlet STAYS armed so
// repeat clicks stack; clicking the outlet again disarms; and a ONE-target outlet needs no arming
// at all (it queues directly -- "if there is no choice, just choose the only option").
function testBlinkBoardTargeting(win) {
  const S = win.__getS(), fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const blinkAct = (tgtNum, tgtName) => ({
    card: 'Emiel the Blessed', activate: true, verb: 'blink', repeatable: true,
    blink_target: tgtNum, blink_target_name: tgtName, blink_count: 1,
  });
  const perm = (idx, num, name) => ({ idx, num, name, is_land: false, tapped: false });
  const mk = (idx, acts) => ({
    type: 'main_phase', decision_index: idx, turn: 4, phase: 'pre_main', on_the_play: true,
    me: { life: 20, library_size: 40, land_drops_left: 0, graveyard: [], hand: [],
          battlefield: [perm(0, 21, 'Emiel the Blessed'), perm(1, 42, 'Peregrine Drake'),
                        perm(2, 43, 'Cloud of Faeries')] },
    opponent: { life: 20, battlefield: [] },
    plans: acts.map((a, i) => ({ index: i, summary: 'land=none; cast: Emiel the Blessed: blink',
                                 land: null, casts: [], actions: [a] })),
  });
  const reset = d => { S.decision = d; S.prev = null; S.plan = []; S.over = false; S.busy = false;
                       S.handOrder = []; S.leMode = false; S.vialMode = null; S.blinkMode = null;
                       S._actSrcFor = null; S._actSrc = null; win.renderBoard(); };

  // TWO targets -> arming, not a modal.
  reset(mk(5, [blinkAct(42, 'Peregrine Drake'), blinkAct(43, 'Cloud of Faeries')]));
  win.__blink.arm('Emiel the Blessed');
  chk(!!S.blinkMode && S.blinkMode.src === 'Emiel the Blessed',
      'clicking a multi-target blink outlet ARMS board targeting');
  chk(!S.actPick, 'arming must NOT also open the modal picker');
  const lit = [...win.document.querySelectorAll('#playfield .thumb.selectable[data-num]')]
                .map(t => +t.dataset.num).sort((a, b) => a - b);
  chk(JSON.stringify(lit) === JSON.stringify([42, 43]),
      `exactly the legal targets light up, got ${JSON.stringify(lit)}`);

  // Clicking a target queues THAT target, and the repeatable outlet stays armed so clicks stack.
  const drake = win.document.querySelector('#playfield .thumb[data-num="42"]');
  chk(!!drake, 'the blink target has a clickable board thumb');
  if (drake) {
    drake.dispatchEvent(new win.MouseEvent('click', { bubbles: true }));
    chk(S.plan.length === 1 && S.plan[0].blinkTarget === 42,
        `a board click queues that target, got ${JSON.stringify(S.plan)}`);
    chk(!!S.blinkMode, 'a REPEATABLE outlet stays armed so repeat clicks stack');
    win.document.querySelector('#playfield .thumb[data-num="42"]')
       .dispatchEvent(new win.MouseEvent('click', { bubbles: true }));
    chk(S.plan.length === 2, `a second click stacks a second activation, got ${S.plan.length}`);
    chk(win.LineBuild.encodeSegments(S.plan).length === 2,
        'two stacked activations encode as TWO segments');
  }
  // Clicking the outlet again disarms without queueing anything more.
  const before = S.plan.length;
  win.__blink.arm('Emiel the Blessed');
  chk(!S.blinkMode, 'clicking the armed outlet again disarms it');
  chk(S.plan.length === before, 'disarming queues nothing');

  // ONE target -> no arming and no modal: it just queues (there is no choice to make).
  reset(mk(6, [blinkAct(42, 'Peregrine Drake')]));
  win.__blink.arm('Emiel the Blessed');
  chk(!S.blinkMode && !S.actPick, 'a single-target blink asks nothing');
  chk(S.plan.length === 1 && S.plan[0].blinkTarget === 42,
      `a single-target blink queues directly, got ${JSON.stringify(S.plan)}`);
  return fails;
}

function testMdfcLandFace(win) {
  const S = win.__getS(), fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const mk = (idx, plans) => ({
    type: 'main_phase', decision_index: idx, turn: 2, phase: 'pre_main', on_the_play: true,
    me: { life: 20, library_size: 40, land_drops_left: 1, battlefield: [], graveyard: [],
          hand: [{ num: 46, name: 'Turntimber Symbiosis', cost: '{4}{G}{G}{G}', mv: 7,
                   kind: 'nonpermanent', mdfc_land_back: 'Turntimber, Serpentine Wood' }] },
    opponent: { life: 20, battlefield: [] },
    plans,
  });
  const reset = d => { S.decision = d; S.prev = null; S.plan = []; S.over = false; S.busy = false;
                       S.handOrder = []; S.leMode = false; S.vialMode = null; win.renderBoard(); };

  // OFFERED: a plan plays it as this phase's land -> the badge is there and names the back face.
  reset(mk(3, [{ index: 0, summary: 'land=Turntimber Symbiosis', land: 'Turntimber Symbiosis',
                 casts: [], actions: [] }]));
  const badge = win.document.querySelector('#handrow .landface[data-landface]');
  chk(!!badge, 'a nonland MDFC whose land side the engine offers renders the land badge');
  chk(badge && /Turntimber, Serpentine Wood/.test(badge.getAttribute('title') || ''),
      'the badge names the BACK face, so the player knows what they are playing');
  if (badge) {
    badge.dispatchEvent(new win.MouseEvent('click', { bubbles: true }));
    chk(S.plan.length === 1 && S.plan[0].kind === 'land',
        `clicking it queues the LAND face, got ${JSON.stringify(S.plan)}`);
    chk(win.LineBuild.encodeLine(S.plan) === 'land=Turntimber Symbiosis',
        `the line encodes as land=<front name>, got "${win.LineBuild.encodeLine(S.plan)}"`);
    // The front cast must still be reachable from the same thumb -- this adds a route, it does not
    // replace one.
    const thumb = win.document.querySelector('#handrow .thumb[data-card="Turntimber Symbiosis"]');
    chk(thumb && thumb.dataset.kind === 'nonpermanent',
        'the thumb still carries the FRONT kind, so double-click/drag still cast the sorcery');
  }
  // NOT OFFERED (drop already spent / no legal face): no badge -- a plan menu must never contain a
  // silent no-op option, which is why the affordance is gated on the enumerated plans, not on the
  // card being an MDFC.
  reset(mk(4, [{ index: 0, summary: 'cast: Priest of Titania', land: null, casts: ['Priest of Titania'],
                 actions: [{ card: 'Priest of Titania' }] }]));
  chk(!win.document.querySelector('#handrow .landface'),
      'no land badge when the engine enumerated no land drop for that card');
  return fails;
}

// MOVING an ATTACHED Equipment onto a SECOND creature with the SAME NAME (synthetic board, no
// binary). Two defects meet in this one gesture, both user-reported 2026-09-01:
//   * "When equipping, the equipment remains in two places, in the plan and separately on the
//     field" — the queued equip drew stacked on its intended host while the Equipment ALSO kept its
//     old spot (here, its stack under the creature it is being moved OFF).
//   * "It doesn't allow me to equip ... to the second Kor Duelist" — with two same-named hosts the
//     host could not be named at all, because it rode a sub-decision keyed on the host's NAME.
// Synthetic because a board with two same-named creatures AND an attached Equipment is not reliably
// reached by seed-driven play; the engine half is pinned by test/scenarios/kitty_equip_two_same_named_hosts.json.
function testEquipMoveRendersOnce(win) {
  const S = win.__getS(), fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const GREAVES = 23, DUELIST_A = 19, DUELIST_B = 20;
  const dec = {
    type: 'main_phase', decision_index: 7, turn: 5, phase: 'pre_main', on_the_play: true,
    me: { life: 20, library_size: 40, land_drops_left: 1, graveyard: [], hand: [],
          battlefield: [
            { name: 'Kor Duelist', idx: 0, num: DUELIST_A, is_land: false },
            { name: 'Kor Duelist', idx: 1, num: DUELIST_B, is_land: false },
            { name: 'Lightning Greaves', idx: 2, num: GREAVES, is_land: false,
              is_equip: true, attached_to: DUELIST_A },
          ] },
    opponent: { life: 20, battlefield: [] },
    // The engine offers moving THIS Greaves onto the OTHER Duelist. Both hosts are called
    // "Kor Duelist", so only equip_host/equip_src can tell them apart.
    plans: [{ index: 0, summary: 'cast: equip Lightning Greaves', land: null, casts: [],
              actions: [{ card: 'Lightning Greaves', activate: true, verb: 'equip',
                          equip_host: DUELIST_B, equip_host_name: 'Kor Duelist', equip_src: GREAVES }] }],
  };
  S.decision = dec; S.prev = null; S.plan = []; S.over = false; S.busy = false;
  S.handOrder = []; S.leMode = false; S.vialMode = null; win.renderBoard();
  const greaves = () => [...win.document.querySelectorAll('#playfield .thumb[data-name]')]
                          .filter(t => t.dataset.name === 'Lightning Greaves');
  chk(greaves().length === 1, `attached, unqueued: 1 Greaves thumb expected, got ${greaves().length}`);

  const tgts = win.equipTargetsFor('Lightning Greaves', GREAVES);
  chk(Object.keys(tgts).length === 1 && (DUELIST_B in tgts),
      `the offered host is the OTHER Duelist by NUMBER, got ${JSON.stringify(tgts)}`);
  chk(win.tryEquipDrop('Lightning Greaves', DUELIST_B, GREAVES), 'the move is queued');
  const q = S.plan[0];
  chk(S.plan.length === 1 && q && q.srcNum === GREAVES && q.target === DUELIST_B,
      `the queued entry names both numbers, got ${JSON.stringify(S.plan)}`);
  chk(win.LineBuild.encodeLine(S.plan) === `equip=Lightning Greaves#${GREAVES}@${DUELIST_B}`,
      `line should name the second Duelist, got "${win.LineBuild.encodeLine(S.plan)}"`);
  // THE duplication check: it must have LEFT the creature it is being moved off.
  chk(greaves().length === 1, `queued move: 1 Greaves thumb expected, got ${greaves().length}`);
  chk(greaves().length === 1 && greaves()[0].classList.contains('planned'),
      'the one that remains is the PLANNED attachment on the new host');
  const group = greaves()[0].closest('.enchgroup');
  const host  = group && group.querySelector('.thumb.base[data-num]');
  chk(host && +host.dataset.num === DUELIST_B,
      `it is stacked on the SECOND Duelist (#${host && host.dataset.num}, wanted #${DUELIST_B})`);
  return fails;
}

// Equipment: the KittyEquipment deck's central action. The engine enumerates an Equip for every
// legal (Equipment, host) pair, but nothing told the GUI which permanent to click or which LineSpec
// verb to write, so an equipment deck simply could not be played by hand (user-reported 2026-08-23).
// Since 2026-08-24 the gesture is a DRAG onto the creature, not a click (USER: "rather than activate
// them normally you would drag them onto a creature") -- the same gesture Auras have always used, so
// the host is picked by the drop instead of by a post-commit dialog. Drives the whole chain: drag ->
// `equip=<name>` line with the host stamped -> engine accept -> ATTACHED, stacked behind its host.
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
  chk(spear && spear.getAttribute('draggable') === 'true' && spear.hasAttribute('data-equip'),
      'an Equipment in play is DRAGGABLE (draggable + data-equip)');
  chk(spear && !spear.hasAttribute('data-activate'),
      'an equip-only Equipment no longer offers a CLICK activation (it moved to the drag)');
  // ...and the drop targets it advertises are exactly the hosts the engine enumerated an Equip for.
  const hosts = win.equipTargetsFor('Shadowspear');
  chk(Object.keys(hosts).length > 0, 'the engine offers at least one legal host for Shadowspear');
  if (!spear || !Object.keys(hosts).length) return fails;

  const srcNum = +(spear.dataset.equipnum || 0);
  chk(srcNum > 0, 'the drag carries the EQUIPMENT permanent’s own m_number (data-equipnum)');
  const hostNum = +Object.keys(hosts)[0];
  chk(win.tryEquipDrop('Shadowspear', hostNum, srcNum), 'dropping it on a legal creature queues the equip');
  const q = st().plan[0];
  chk(st().plan.length === 1 && q && q.kind === 'activate' && q.verb === 'equip',
      'the drop queues an activate entry carrying the equip verb');
  chk(q && q.target === hostNum && q.targetName === hosts[hostNum],
      'the HOST rides on the queued entry, so no dialog has to re-ask it');
  // ...and it rides on the LINE, not just on the client entry. Leaving the host to the `equip`
  // sub-decision is what made two same-named hosts indistinguishable: the sub's choice is the host
  // NAME, so both variants shared a signature and the dedup dropped one (KittyEquipment seed 6,
  // "it doesn't allow me to equip ... to the second Kor Duelist").
  chk(win.LineBuild.encodeLine(st().plan) === `equip=Shadowspear#${srcNum}@${hostNum}`,
      `the line stamps source+host, got "${win.LineBuild.encodeLine(st().plan)}"`);
  // A queued equip is drawn ONCE -- stacked on the host it is about to attach to, NOT also where it
  // sits now ("the equipment remains in two places, in the plan and separately on the field").
  // Scoped to #playfield: #board also holds the HAND row, where a queued card legitimately stays
  // visible (with its queued count badge) until the commit resolves.
  const spearThumbs = [...win.document.querySelectorAll('#playfield .thumb[data-name]')]
                        .filter(t => t.dataset.name === 'Shadowspear');
  chk(spearThumbs.length === 1,
      `a queued equip renders the Equipment once, got ${spearThumbs.length} Shadowspear thumbs`);
  chk(spearThumbs.length === 1 && spearThumbs[0].classList.contains('planned'),
      'and the one that remains is the PLANNED attachment on its host');
  // Dropping it again MOVES it rather than queueing a second attach (an Equipment has one host).
  const otherNum = Object.keys(hosts).map(Number).find(n => n !== hostNum);
  if (otherNum != null) {
    win.tryEquipDrop('Shadowspear', otherNum, srcNum);
    chk(st().plan.length === 1 && st().plan[0].target === otherNum,
        're-dropping on another creature RE-AIMS the queued equip instead of duplicating it');
    win.tryEquipDrop('Shadowspear', hostNum, srcNum);
  }

  await win.commitLine(); await settle(win);
  // Any remaining dimension (a second Equipment's host, a tutor) is answered the way a human does;
  // the dragged host itself is auto-resolved, so it must NOT be re-asked.
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

// Equipping decided FROM HAND (USER 2026-09-01: "Ideally we would allow equipping to be decided
// from hand or by dragging the equipment on the field. That would mean two operations, playing it
// plus equipping."). The engine has always enumerated the pair as ONE plan ("cast: Bonesplitter,
// equip Bonesplitter -> Kor Duelist"), but it was unreachable from the GUI: the only equip gesture
// was dragging a permanent that does not exist until the cast resolves, and the encoded line could
// not say the host anyway. Now dropping the HAND card on a creature queues both operations.
// KittyEquipment seed 6 T2 is the minimal shape: Kor Duelist in play, Bonesplitter in hand.
async function testEquipFromHand() {
  const fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const win = buildDom(); await settle(win);
  await startGame(win, { deck: 'KittyEquipment', seed: 6, turns: 8 });
  const st = () => S(win);
  let guard = 0;
  while (guard++ < 40 && !(st().decision && st().decision.type === 'main_phase' && st().decision.turn === 2)) {
    if (!(await stepForward(win, 'ai'))) break;
  }
  chk(st().decision && st().decision.turn === 2, 'reached KittyEquipment s6 turn 2');
  if (fails.length) return fails;

  const hosts = win.handEquipTargetsFor('Bonesplitter');
  chk(Object.keys(hosts).length > 0, 'the engine offers a cast-AND-equip plan for the Bonesplitter in hand');
  if (!Object.keys(hosts).length) return fails;
  const hostNum = +Object.keys(hosts)[0];
  // The land first: the combined plan the engine enumerates plays one, and without it the line is
  // a real (correctly reported) mana shortfall rather than the gesture being wrong.
  win.queueCard('Plains', 'land');
  chk(win.tryEquipFromHandDrop('Bonesplitter', 'permanent', hostNum),
      'dropping the HAND Equipment on a creature is accepted as a cast-and-equip');
  const cast = st().plan.find(p => p.name === 'Bonesplitter' && p.kind !== 'activate');
  const eq   = st().plan.find(p => p.kind === 'activate' && p.verb === 'equip');
  chk(!!cast && !!eq, 'it queues BOTH operations: the cast and the equip');
  chk(eq && eq.fromHand === true && eq.target === hostNum,
      'the equip is tagged fromHand and carries the dropped host');
  const line = win.LineBuild.encodeLine(st().plan);
  chk(/^land=Plains;cast=Bonesplitter;equip=Bonesplitter#\d+@\d+$/.test(line),
      `the line encodes cast + host-stamped equip, got "${line}"`);
  // ...and the card is drawn ONCE (on its host), not as a planned cast AND a planned attachment.
  const boneThumbs = [...win.document.querySelectorAll('#playfield .thumb[data-name]')]
                       .filter(t => t.dataset.name === 'Bonesplitter');
  chk(boneThumbs.length === 1,
      `the queued cast-and-equip renders once, got ${boneThumbs.length} Bonesplitter thumbs`);

  await win.commitLine(); await settle(win);
  // No dialog should be needed at all -- the host is in the line, so exactly one plan matches.
  chk(!win.document.querySelector('#decpanel .varpick'),
      'no choose dialog: the stamped host leaves exactly one matching plan');
  chk(!S(win).hadReject, 'the cast-and-equip line is ACCEPTED by the engine');
  const bf = ((st().decision && st().decision.me) || {}).battlefield || [];
  const bone = bf.find(p => p.name === 'Bonesplitter');
  chk(bone && bone.attached_to === hostNum,
      `the Bonesplitter resolved ATTACHED to the dropped host (attached_to=${bone && bone.attached_to}, wanted ${hostNum})`);
  return fails;
}

// COLOURLESS-FIRST tap order, driven exactly as the user reported it (StompySurprise seed 9,
// 2026-08-24): T1 land=Forest + Llanowar Elves, then T2 `land=Wirewood Lodge; cast Sol Ring,
// Natural Order`. That line is exactly payable -- the Lodge's {C} pays Sol Ring's {1}, Forest +
// Llanowar make {G}{G}, and Sol Ring's own {C}{C} pays the {2} -- and the engine enumerates it, but
// a colourless-only source and a mono-coloured one both ranked 10 in ManaSourceRank, so the generic
// pip took the Forest and Natural Order was left one green short: silently dropped autonomously, a
// hard "not enough mana" reject in the viewer.
//
// It lives HERE, not in test/scenarios/, on purpose: the defect is in the PAYMENT, and CheckLine
// (which every validate_line fixture asserts on) accepted the line both before and after the fix --
// enumeration is deliberately optimistic. Only a real commit through the apply path can see it, and
// only the GUI turns the silent drop into a visible failure.
async function testColorlessFirstTapOrder() {
  const fails = [];
  const chk = (c, m) => { if (!c) fails.push(m); };
  const win = buildDom(); await settle(win);
  await startGame(win, { deck: 'StompySurprise', seed: 9, turns: 8 });
  const st = () => S(win);
  let guard = 0;
  while (guard++ < 10 && st().decision && st().decision.type === 'mulligan') {
    win.commitMulligan(st().decision, 1); await settle(win);
  }
  chk(st().decision && st().decision.type === 'main_phase' && st().decision.turn === 1,
      'reached StompySurprise s9 turn 1 main phase');
  if (fails.length) return fails;

  st().plan = []; win.queueCard('Forest', 'land'); win.queueCard('Llanowar Elves', 'permanent');
  await win.commitLine(); await settle(win);
  for (let i = 0; i < 6; i++) {
    const p = win.document.querySelector('#decpanel .varpick');
    if (!p) break;
    p.dispatchEvent(new win.MouseEvent('click', { bubbles: true })); await settle(win);
  }
  // Walk to turn 2. Committing a line no longer ENDS the phase (2026-09-04: "Commit Line literally
  // means let me play more things"), so turn 1 hands back one or more further main-phase frames --
  // possibly with nothing in them -- and the way past those is to PASS, i.e. commit an empty line.
  // Anything that is not a main phase is still stepped by the AI. Before that change this loop only
  // had to skip non-main decisions, which is why it read `type !== 'main_phase'`.
  guard = 0;
  while (guard++ < 20 && st().decision
         && !(st().decision.type === 'main_phase' && st().decision.turn === 2)) {
    if (st().decision.type === 'main_phase') { st().plan = []; await win.commitLine(); await settle(win); }
    else if (!(await stepForward(win, 'ai'))) break;
  }
  chk(st().decision && st().decision.turn === 2, `reached turn 2, got ${st().decision && st().decision.turn}`);
  if (fails.length) return fails;

  st().plan = [];
  win.queueCard('Wirewood Lodge', 'land');
  win.queueCard('Sol Ring', 'permanent');
  win.queueCard('Natural Order', 'nonpermanent');
  chk(win.LineBuild.encodeLine(st().plan) === 'land=Wirewood Lodge;cast=Sol Ring;cast=Natural Order',
      `the line encodes as expected, got "${win.LineBuild.encodeLine(st().plan)}"`);
  await win.commitLine(); await settle(win);
  // Natural Order's fetch is a `choose` dimension -- answer it the way a human does.
  for (let i = 0; i < 8; i++) {
    const p = win.document.querySelector('#decpanel .varpick');
    if (!p) break;
    p.dispatchEvent(new win.MouseEvent('click', { bubbles: true })); await settle(win);
  }
  chk(!st().hadReject,
      'Wirewood Lodge + Sol Ring + Natural Order is PAID, not rejected for mana (colourless-first)');
  // Same walk as the turn-1 one above, and for the same reason: the committed line no longer ends
  // the phase, so reaching turn 3 means passing turn 2's remaining main-phase frames rather than
  // just letting the engine carry on. The assertions below still want a turn-3 board.
  guard = 0;
  while (guard++ < 20 && st().decision
         && !(st().decision.type === 'main_phase' && st().decision.turn === 3)) {
    if (st().decision.type === 'main_phase') { st().plan = []; await win.commitLine(); await settle(win); }
    else if (!(await stepForward(win, 'ai'))) break;
  }
  chk(st().decision && st().decision.turn === 3,
      `the phase resolved and play moved on, got turn ${st().decision && st().decision.turn}`);
  const bf = ((st().decision || {}).me || {}).battlefield || [];
  chk(bf.some(p => p.name === 'Sol Ring'), 'Sol Ring resolved');
  chk(!(((st().decision || {}).me || {}).hand || []).some(c => c.name === 'Natural Order'),
      'Natural Order actually left hand (it was silently dropped before the fix)');
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
    // Blink targeting happens on the BOARD, not in a modal (fast, DOM-only).
    const btFails = testBlinkBoardTargeting(win);
    if (btFails.length) { anyFail = true; console.log(`✗ blink board targeting: ${btFails.length} fail`); btFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ blink targets picked on the board (arm → click creature → stacks; 1 target asks nothing)'); }
    // The clue gesture is a GLOBAL option, not a per-click dialog (fast, DOM-only).
    const cfFails = testClueFuseOption(win);
    if (cfFails.length) { anyFail = true; console.log(`✗ clue fuse option: ${cfFails.length} fail`); cfFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ clue fuse is a persisted GLOBAL option (one click, no modal, either state)'); }
    // "Do that loop again ×N", read off the COMMITTED lines (fast, DOM-only).
    const lpFails = testLoopMacro(win);
    if (lpFails.length) { anyFail = true; console.log(`✗ loop macro: ${lpFails.length} fail`); lpFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ loop macro (board control → central 2-step dialog → k×n committable lines; turn-scoped, undo-safe)'); }
    // New client on an OLD server must say so out loud (fast, DOM-only).
    const ssFails = testStaleServerHandshake(win);
    if (ssFails.length) { anyFail = true; console.log(`✗ stale server handshake: ${ssFails.length} fail`); ssFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ stale-server handshake (a missing/older serverApi warns loudly and is dismissible)'); }
    // MDFC land back reachable from the palette (fast, DOM-only).
    const lfFails = testMdfcLandFace(win);
    if (lfFails.length) { anyFail = true; console.log(`✗ mdfc land face: ${lfFails.length} fail`); lfFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ MDFC land back playable from the palette (badge → land= line, gated on the offer)'); }
    // Moving an attached Equipment onto a SECOND same-named creature (fast, DOM-only).
    const emFails = testEquipMoveRendersOnce(win);
    if (emFails.length) { anyFail = true; console.log(`✗ equip move: ${emFails.length} fail`); emFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ equip move (re-host to the SECOND same-named creature; drawn once, not in two places)'); }
  }
  // The primary button must not pass the phase because a commit emptied the queue under it
  // (needs a real frame with a floating pool, so it runs on its own).
  {
    let pgFails;
    try { pgFails = await testPassGuard(); }
    catch (e) { console.error(`✗ pass guard: harness error: ${e.stack || e}`); process.exit(2); }
    if (pgFails.length) { anyFail = true; console.log(`✗ pass guard: ${pgFails.length} fail`); pgFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ pass guard (a click right after a commit cannot discard the floating pool; two deliberate clicks still can)'); }
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
    else { console.log('✓ equip (Equipment: DRAGGED onto a creature → equip= line + stamped host → accepted → attached)'); }
  }
  // Equipping decided from HAND: one drop queues the cast AND the equip (needs a real game walk).
  {
    let ehFails;
    try { ehFails = await testEquipFromHand(); }
    catch (e) { console.error(`✗ equip from hand: harness error: ${e.stack || e}`); process.exit(2); }
    if (ehFails.length) { anyFail = true; console.log(`✗ equip from hand: ${ehFails.length} fail`); ehFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ equip from hand (drop a hand Equipment on a creature → cast + host-stamped equip → attached)'); }
  }
  // Colourless-first tap order: an exactly-payable line must not lose a coloured pip to a generic one.
  {
    let cfFails;
    try { cfFails = await testColorlessFirstTapOrder(); }
    catch (e) { console.error(`✗ colourless-first tap order: harness error: ${e.stack || e}`); process.exit(2); }
    if (cfFails.length) { anyFail = true; console.log(`✗ colourless-first tap order: ${cfFails.length} fail`); cfFails.forEach(m => console.log('  - ' + m)); }
    else { console.log('✓ colourless-first tap order (s9 T2: Lodge {C} pays Sol Ring, Natural Order keeps its {G}{G})'); }
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
