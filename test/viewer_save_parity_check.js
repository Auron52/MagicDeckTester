#!/usr/bin/env node
// SAVE-PATH PARITY for the play viewer (tools/play/server.js).
// =====================================================================================
// What the OTHER viewer layers guard, and what none of them guard:
//   * test/interactive_parity_check.py  -- the --interactive child emits the same FRAMES a stateless
//     respawn chain emits for the same growing prefix. That is the /api/step seam.
//   * test/viewer_protocol_check.py     -- a SAVED reference replays with a valid index at every step.
//     That is the file, after it exists.
//   * test/viewer_client_check.js       -- the browser's own bookkeeping (history / undo / panels).
// NONE of them run /api/save. And /api/save is not a read: it RE-RUNS the whole accumulated choice
// stream in a FRESH process and publishes the result as the game the human played. If that re-run
// resolves any decision differently, every later plan index lands on a different plan and the file
// records a game nobody played -- under the right name, with no error.
//
// THAT IS NOT HYPOTHETICAL (user, 2026-09-10 11:21, EldraziDisplacerFlicker seed 9 gi 8): "This line
// was really messed up... it just skipped to the turn 7." 36 decisions, win_turn 7, on a session
// still mid-turn-4. The choice stream was intact -- those 49 picks replay perfectly on the session's
// own engine image -- but /api/save spawned build/Release/mtg, which another agent had REBUILT two
// minutes earlier. Same choices, different enumerator: 3528 plans vs 1206 at decision 27, 14112 vs
// 423 at decision 28, so pick 3526 fell off the end of the menu and the replay veered.
//
// So this check drives the SAVE, not the step, and asserts three separate things:
//
//   A. auditTrace (pure) -- the detector itself. A faithful trace passes; an out-of-range pick, a
//      board that differs from the live frame, and a truncated replay each fail. No binary needed.
//   B. LIVE SESSION -> SAVE parity -- play a real game through server.runStepCached (the same
//      interactive child the browser drives), exercising a cast-order pin, a sub-decision
//      resolution pick and a MULTI-INT line (London bottoming), then save it and require every
//      replayed decision to match the frame the session was actually shown, decision for decision.
//      Plus a NEGATIVE CONTROL: re-save with the cast-order side channel DROPPED. If the pin was
//      load-bearing, the audit must REFUSE that save -- which is the property "a carrier that stops
//      threading through is caught", stated as a test rather than as a hope.
//   C. BINARY PINNING -- start a session, then DESTROY the binary the session started from, and
//      require the save to still succeed. It can only do that by running the pinned copy, which is
//      the whole fix for the 2026-09-10 incident.
//
// Run:  node test/viewer_save_parity_check.js            (needs a built build/Release/mtg)
//       MTG_BIN=<path> node test/viewer_save_parity_check.js
//       node test/viewer_save_parity_check.js --verbose
// Exit 0 = every property held; 1 = a real break; 2 = harness/setup error.

const fs = require('fs');
const os = require('os');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const VERBOSE = process.argv.includes('--verbose');

// ---- setup: a THROWAWAY copy of the engine, so layer C can destroy it -------------
// server.js resolves its binary once at require() time, so both env vars must be set first.
function resolveBin() {
  if (process.env.MTG_BIN) return process.env.MTG_BIN;
  for (const n of ['mtg', 'mtg.exe']) {
    const p = path.join(ROOT, 'build', 'Release', n);
    if (fs.existsSync(p)) return p;
  }
  return null;
}
// Absolute: viewer_checks.sh passes MTG_BIN as ./build/Release/mtg, and server.js spawns with
// cwd=ROOT, so a relative path here would resolve against whatever cwd the check was launched from.
const REAL_BIN = (() => { const b = resolveBin(); return b ? path.resolve(ROOT, b) : null; })();
if (!REAL_BIN || !fs.existsSync(REAL_BIN)) {
  console.error('setup: no engine binary (build Release first, or set MTG_BIN).');
  process.exit(2);
}
const SCRATCH = fs.mkdtempSync(path.join(os.tmpdir(), 'mtg-saveparity-'));
const THROWAWAY = path.join(SCRATCH, 'mtg' + path.extname(REAL_BIN));
fs.copyFileSync(REAL_BIN, THROWAWAY);
fs.chmodSync(THROWAWAY, 0o755);
process.env.MTG_BIN = THROWAWAY;
process.env.PLAY_PIN_BIN = '1';        // pinning is off for require()d checks unless asked for

const server = require(path.join(ROOT, 'tools', 'play', 'server.js'));

const DEST = fs.mkdtempSync(path.join(os.tmpdir(), 'mtg-savedest-'));
let fails = 0;
const fail = (m) => { console.log('  FAIL: ' + m); fails++; };
const ok = (m) => { if (VERBOSE) console.log('  ok: ' + m); };

// =====================================================================================
// A. auditTrace, as a pure function
// =====================================================================================
// A trace entry the way --log-dir writes it: { chosen, decision: { ... } }.
function entry(i, turn, chosen, nPlans, extra) {
  const plans = [];
  for (let k = 0; k < nPlans; k++) plans.push({ index: k, summary: 'p' + k });
  return { chosen, decision: Object.assign({
    decision_index: i, turn, type: 'main_phase', phase: 'pre_main', plans,
    me: { life: 20, library_size: 50 - i, hand: [], battlefield: [], graveyard: [], floating_mana: null },
    opponent: { life: 20, battlefield: [] },
  }, extra || {}) };
}

function testAudit() {
  console.log('--- A. auditTrace (pure) ---');
  const decs = [entry(0, 1, 0, 5), entry(1, 1, 3, 9), entry(2, 2, 2, 4)];
  const frames = new Map();
  for (const e of decs) frames.set(e.decision.decision_index, server.frameFingerprint(e.decision));

  let a = server.auditTrace({ decisions: decs }, frames);
  if (!a.ok) fail('a faithful trace was flagged: ' + a.problems.join(' | '));
  else if (a.checked !== 3) fail(`a faithful trace verified ${a.checked} of 3 decisions`);
  else ok('faithful trace: 3/3 verified, no problems');

  // (1) OUT-OF-RANGE PICK -- the 2026-09-10 signature, and the one detector that needs no ledger.
  const oor = [entry(0, 1, 0, 5), entry(1, 1, 3526, 181)];
  a = server.auditTrace({ decisions: oor }, new Map());
  if (a.ok) fail('an out-of-range pick (3526 of a 181-plan menu) was NOT flagged');
  else ok('out-of-range pick flagged: ' + a.problems[0].slice(0, 80));
  // ...and it must not fire on a legitimately CAPPED menu, where the writer re-emits the chosen
  // plan beyond the display cap (that is why the bound is the highest emitted index, not the length).
  const capped = [{ chosen: 3526, decision: Object.assign({}, entry(0, 4, 3526, 0).decision,
                   { plans: [{ index: 0 }, { index: 1 }, { index: 3526 }], plans_total: 3528 }) }];
  a = server.auditTrace({ decisions: capped }, new Map());
  if (!a.ok) fail('a capped menu that re-emitted its chosen plan was flagged: ' + a.problems.join(' | '));
  else ok('capped menu with the chosen plan re-emitted: clean');

  // (2) BOARD MISMATCH -- same picks, different game.
  const drift = [entry(0, 1, 0, 5), Object.assign({}, entry(1, 1, 3, 9))];
  drift[1].decision.me.life = 17;
  a = server.auditTrace({ decisions: drift }, frames);
  if (a.ok) fail('a replay whose board differs from the live frame was NOT flagged');
  else ok('board mismatch flagged: ' + a.problems[0].slice(0, 80));

  // (3) TRUNCATION -- the replay's game ended before the session's did.
  a = server.auditTrace({ decisions: decs.slice(0, 2) }, frames);
  if (a.ok) fail('a truncated replay (2 of 3 decisions) was NOT flagged');
  else ok('truncation flagged: ' + a.problems[a.problems.length - 1].slice(0, 90));

  // An EMPTY ledger must not read as a pass: everything is `unverified`, nothing is `checked`.
  a = server.auditTrace({ decisions: decs }, new Map());
  if (a.checked !== 0 || a.unverified !== 3) fail(`no-ledger audit reported checked=${a.checked} unverified=${a.unverified}, want 0/3`);
  else ok('no ledger: 0 checked, 3 unverified (never silently "verified")');
}

// =====================================================================================
// B/C. driving a real session
// =====================================================================================
const SIDE_PROMPTS = { firebreathe: 'firebreathe', jitte: 'jitte', storage_hold: 'storageHold' };

// The engine's own default for a decision -- the same fields interactive_parity_check.py trusts.
// Returns an ARRAY, because several decision types answer with one int per candidate.
function answer(d, plan) {
  if (d.type === 'bottom') {
    // London bottoming: K ints, descending hand index (commitBottomBatch). The MULTI-INT line.
    const K = d.bottom_total || 1;
    const set = Array.isArray(d.ai_set) && d.ai_set.length === K
      ? d.ai_set.slice() : Array.from({ length: K }, (_, i) => i);
    return set.slice().sort((a, b) => b - a);
  }
  if (d.type === 'main_phase') return [plan == null ? -1 : plan];
  const ac = d.ai_choice;
  if (typeof ac === 'number') return [ac];
  if (ac && typeof ac.index === 'number') return [ac.index];
  if (typeof d.heuristic_default === 'number') return [d.heuristic_default];
  return [0];
}

// A main-phase decision at depth 0 carries no ai_choice (there is no search to ask), so the driver
// has to pick a line itself. It takes the BUSIEST enumerated plan -- most actions, ties to the lowest
// index. Two reasons, both about coverage: an empty "commit turn" walk develops no board, so it never
// reaches an in-game sub-decision at all (viewer_client_check.js hit the same wall and added its 'ai'
// mode for it), and a one-action plan can carry no cast ORDER, so the pin this check exists to
// exercise would never be constructible.
function mainPick(d) {
  const ac = d.ai_choice;
  if (typeof ac === 'number') return ac;
  if (ac && typeof ac.index === 'number') return ac.index;
  let best = -1, bestN = 0;
  for (const pl of (d.plans || [])) {
    const n = ((pl && pl.actions) || []).length;
    if (n > bestN) { bestN = n; best = pl.index; }
  }
  if (best >= 0) return best;
  if (typeof d.heuristic_default === 'number') return d.heuristic_default;
  return -1;
}

// Play one game to its end through the SERVER's own step entry point, recording each frame exactly
// as the live bridge does. Returns the finished session body + what it managed to exercise.
async function playGame(base, opts) {
  const p = Object.assign({ choices: [], firebreathe: {}, jitte: {}, storageHold: {}, castOrder: {} }, base);
  // multiExtra: the ints a MULTI-int answer contributes beyond its first. Each one is its own engine
  // decision (London bottoming emits one frame per bottom_step) but the human answered the batch in
  // one click, so those frames are never served and legitimately have no ledger entry.
  const seen = { types: new Set(), multi: 0, multiExtra: 0, pins: 0, decisions: 0 };
  let mulliganed = false;
  for (let n = 0; n < 600; n++) {
    const out = await server.runStepCached(p, null);
    if (out.kind === 'result') return { p, seen, result: out.result };
    if (out.kind !== 'decision') return { p, seen, error: out.error || 'unexpected ' + out.kind };
    const d = out.decision;
    server.recordFrame(p, d);
    seen.types.add(d.type);
    seen.decisions++;

    // The three side-channel PROMPT frames answer by KEYED arg, never by a --choices slot.
    if (d.type in SIDE_PROMPTS) {
      const key = SIDE_PROMPTS[d.type];
      const v = typeof d.heuristic_default === 'number' ? d.heuristic_default : 0;
      p[key][d.type === 'storage_hold' ? `${d.turn}:${d.land_num != null ? d.land_num : 0}` : String(d.turn)] = v;
      continue;
    }
    // ONE mulligan when asked for, so the game reaches London bottoming (the multi-int line).
    if (d.type === 'mulligan' && opts.mulligan && !mulliganed) {
      mulliganed = true;
      p.choices = p.choices.concat([0]);
      continue;
    }

    let plan = null;
    if (d.type === 'main_phase') {
      plan = mainPick(d);
      // CAST-ORDER PIN, built exactly the way index.html builds the "*" full-order form: the plan's
      // own action sequence, REVERSED, so the pin is load-bearing rather than a no-op restatement.
      if (opts.pin && typeof d.main_ordinal === 'number' && d.main_ordinal >= 0) {
        const chosen = (d.plans || []).find(pl => pl.index === plan);
        const acts = ((chosen && chosen.actions) || []).map(a => a.card).filter(Boolean);
        if (acts.length >= 2 && !p.castOrder[String(d.main_ordinal)]) {
          p.castOrder[String(d.main_ordinal)] = ['*'].concat(acts.slice().reverse());
          seen.pins++;
        }
      }
    }
    const picks = answer(d, plan);
    if (picks.length > 1) { seen.multi++; seen.multiExtra += picks.length - 1; }
    p.choices = p.choices.concat(picks);
  }
  return { p, seen, error: 'game did not terminate in 600 steps' };
}

function destDirFor(tag) {
  const dir = path.join(DEST, tag);
  fs.mkdirSync(dir, { recursive: true });
  return dir;
}

async function testGame(label, base, opts, coverage) {
  console.log(`--- B. live session -> save parity: ${label} ---`);
  const { p, seen, result, error } = await playGame(base, opts);
  if (error) { fail(`${label}: ${error}`); return; }
  seen.types.forEach(t => coverage.types.add(t));
  coverage.multi += seen.multi;
  coverage.pins += seen.pins;

  // --- the SAVE, audited ---
  const dir = destDirFor(label.replace(/[^A-Za-z0-9]+/g, '_'));
  const r = server.saveTrace(p, dir);
  if (!r.savedAs) {
    fail(`${label}: the save was refused for a session that never diverged -- ${r.error || 'no trace written'}`);
    return;
  }
  // Every replayed decision must be covered by the ledger EXCEPT the follow-on steps of a batched
  // multi-int answer (see seen.multiExtra). More than that means the ledger and the trace do not line
  // up, and the audit would be blind exactly where a divergence could hide.
  if (r.unverified > seen.multiExtra) {
    fail(`${label}: ${r.unverified} of ${r.verified + r.unverified} replayed decisions had no live `
       + `frame, but only ${seen.multiExtra} are accounted for by batched multi-int answers`);
  }
  if (r.verified < 3) fail(`${label}: only ${r.verified} decisions were verified`);
  // The published log must ALSO be the game we played: same win turn, same decision count.
  const saved = JSON.parse(fs.readFileSync(path.join(ROOT, r.savedAs), 'utf8'));
  if (!!saved.won !== !!result.won || saved.win_turn !== (result.won ? result.win_turn : -1)) {
    fail(`${label}: saved log says won=${saved.won} win_turn=${saved.win_turn}, the session ended `
       + `won=${result.won} win_turn=${result.win_turn}`);
  }
  ok(`${label}: ${r.verified} decisions verified, won=${saved.won} win_turn=${saved.win_turn}, `
   + `pins=${seen.pins} multi-int=${seen.multi} types=${seen.types.size}`);

  // --- NEGATIVE CONTROL: drop the cast-order carrier and re-save ---
  // This is the (a)-class failure stated as a test: if a side channel stops reaching the save
  // re-run, the replay applies a different line and the audit must say so instead of publishing it.
  if (seen.pins > 0) {
    const bare = Object.assign({}, p, { castOrder: {} });
    const r2 = server.saveTrace(bare, destDirFor('control_' + label.replace(/[^A-Za-z0-9]+/g, '_')));
    if (r2.savedAs) {
      coverage.controlInert++;
      if (VERBOSE) console.log(`  note: ${label}: dropping --cast-order changed nothing here `
        + '(the pinned order happened to be the one the engine would run anyway) -- control inconclusive');
    } else {
      coverage.controlLive++;
      ok(`${label}: dropping the cast-order carrier is REFUSED (${(r2.diverged || [])[0] || ''})`.slice(0, 160));
    }
    // The quarantine is for a USER's diverged save, not for this check's deliberate ones.
    if (r2.divergedAs) { try { fs.unlinkSync(path.join(ROOT, r2.divergedAs)); } catch (e) {} }
  }
  return { p, savedAs: r.savedAs };
}

// =====================================================================================
// C. binary pinning: destroy the binary mid-session; the save must still be right
// =====================================================================================
async function testBinaryPin(base) {
  console.log('--- C. session-pinned binary (a mid-session rebuild must not change the replay) ---');
  const pinned = server.sessionBin(Object.assign({ choices: [] }, base));
  if (pinned === server.BIN) { fail('the session did not pin the binary at all'); return; }
  if (path.resolve(pinned).indexOf(path.resolve(server.SESSION_DIR)) !== 0) {
    fail(`the pin lives outside ${path.relative(ROOT, server.SESSION_DIR)}: ${pinned}`);
  }
  const { p, result, error } = await playGame(base, { mulligan: false, pin: false });
  if (error) { fail('pin scenario: ' + error); return; }

  // THE REBUILD, in its most hostile form: the binary the session started from is replaced by one
  // that cannot run at all. Every fresh spawn from MTG_BIN would now fail; only the pinned copy works.
  fs.writeFileSync(THROWAWAY, '#!/bin/sh\necho "this is not the engine you played on" >&2\nexit 3\n');
  fs.chmodSync(THROWAWAY, 0o755);
  let r;
  try { r = server.saveTrace(p, destDirFor('pinned')); }
  finally { fs.copyFileSync(REAL_BIN, THROWAWAY); fs.chmodSync(THROWAWAY, 0o755); }

  if (!r.savedAs) {
    fail('the save failed after the shared binary was replaced -- it did NOT use the session pin: '
       + (r.error || '').slice(0, 200));
    return;
  }
  const saved = JSON.parse(fs.readFileSync(path.join(ROOT, r.savedAs), 'utf8'));
  if (!!saved.won !== !!result.won || saved.win_turn !== (result.won ? result.win_turn : -1)) {
    fail(`the pinned save recorded won=${saved.won} win_turn=${saved.win_turn}, the session ended `
       + `won=${result.won} win_turn=${result.win_turn}`);
  } else {
    ok(`binary replaced mid-session; save still ran the pin and matched the session `
     + `(won=${saved.won} win_turn=${saved.win_turn}, ${r.verified} decisions verified)`);
  }

  // And a NEW game must pick the NEW binary up -- pinning freezes a game, never the tool.
  const next = server.sessionBin(Object.assign({}, base, { seed: (base.seed || 1) + 97, gameIndex: 0 }));
  if (next === pinned) fail('a new game reused the previous game\'s pin (a rebuild would never be picked up)');
  else ok('a new game re-pins from the current binary');
}

// =====================================================================================
// E. the ROUTES, over a real socket
// =====================================================================================
// Layers B-D call the helpers directly, which cannot see the WIRING -- and the wiring is the fix.
// Delete `recordFrame` from /api/step and the ledger is empty at save time (every decision reads
// "unverified", nothing is checked, and the audit is blind); point /api/save back at the old
// unstaged runStep and a diverged replay is published again. Both would leave B-D green. So this
// layer drives http://127.0.0.1:<ephemeral> exactly as the browser does.
function post(port, route, body) {
  return new Promise((resolve, reject) => {
    const data = JSON.stringify(body);
    const req = require('http').request(
      { host: '127.0.0.1', port, path: route, method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) } },
      (res) => {
        let out = '';
        res.on('data', (c) => { out += c; });
        res.on('end', () => { try { resolve(JSON.parse(out)); } catch (e) { reject(e); } });
      });
    req.on('error', reject);
    req.end(data);
  });
}

async function testRoutes(base) {
  console.log('--- E. the real /api/step + /api/save routes over a socket ---');
  const srv = server.httpServer;
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  const port = srv.address().port;
  try {
    const p = Object.assign({ choices: [], firebreathe: {}, jitte: {}, storageHold: {}, castOrder: {} }, base);
    let multiExtra = 0, result = null;
    for (let n = 0; n < 400 && !result; n++) {
      const out = await post(port, '/api/step', p);
      if (out.kind === 'result') { result = out.result; break; }
      if (out.kind !== 'decision') { fail('route walk: ' + (out.error || out.kind)); return; }
      const d = out.decision;
      if (d.type in SIDE_PROMPTS) {
        const v = typeof d.heuristic_default === 'number' ? d.heuristic_default : 0;
        p[SIDE_PROMPTS[d.type]][d.type === 'storage_hold'
          ? `${d.turn}:${d.land_num != null ? d.land_num : 0}` : String(d.turn)] = v;
        continue;
      }
      const picks = answer(d, d.type === 'main_phase' ? mainPick(d) : null);
      multiExtra += picks.length - 1;
      p.choices = p.choices.concat(picks);
    }
    if (!result) { fail('route walk: the game did not finish'); return; }
    const r = await post(port, '/api/save', p);
    if (!r.savedAs) { fail('the /api/save route refused a clean session: ' + (r.error || '')); return; }
    // THE POINT OF THIS LAYER: the route must have ledgered as it served, so the save is verified
    // against real frames rather than waved through with an empty ledger.
    if (!(r.verified > 0)) {
      fail('/api/save reported 0 verified decisions -- /api/step is not ledgering its frames, so '
         + 'the audit has nothing to compare against');
    } else if (r.unverified > multiExtra) {
      fail(`/api/save left ${r.unverified} decisions unverified, only ${multiExtra} explained by batched answers`);
    } else {
      ok(`routes: ${r.verified} decisions verified end-to-end, saved to ${r.savedAs}`);
    }
    // /api/save's destination is the REAL logs/play/, so this layer runs on a (seed, game-index)
    // pair the viewer itself can never produce -- it always sends gameIndex = seed-1, so
    // claude_s990001_gi0.json cannot be anyone's saved game. Nothing of the user's is at risk, and
    // this file is removed on the way out.
    try { fs.unlinkSync(path.join(ROOT, r.savedAs)); } catch (e) {}
  } finally {
    await new Promise((r) => srv.close(r));
  }
}

// =====================================================================================
// D. a save that DID diverge must be refused, not published
// =====================================================================================
function testRefusal(sessions) {
  console.log('--- D. a diverged save is refused, not published ---');
  for (const s of sessions) { if (tryRefusal(s)) return; }
  console.log('  SKIP: no finished session had an early main-phase pick to tamper with');
}

function tryRefusal(sess) {
  if (!sess || !sess.savedAs) return false;
  const p = sess.p;
  // Tamper exactly the way a stale plan index does: point a MAIN-PHASE pick at a plan the replay's
  // menu does not have. `decision_index` IS the position in the choice stream (it is the cursor), so
  // the clean run's own trace tells us which slot to corrupt. An EARLY one, so the rest of the game
  // diverges behind it -- which is what the 2026-09-10 incident actually looked like.
  let trace;
  try { trace = JSON.parse(fs.readFileSync(path.join(ROOT, sess.savedAs), 'utf8')); }
  catch (e) { return false; }
  const tampered = Object.assign({}, p, { choices: p.choices.slice() });
  let hit = -1;
  for (const e of (trace.decisions || [])) {
    const d = e.decision || {};
    if (d.type === 'main_phase' && typeof e.chosen === 'number' && e.chosen >= 0
        && typeof d.decision_index === 'number'
        && d.decision_index < tampered.choices.length - 2) { hit = d.decision_index; break; }
  }
  if (hit < 0) return false;
  tampered.choices[hit] = 999999;
  const dir = destDirFor('refused');
  const r = server.saveTrace(tampered, dir);
  const seed = parseInt(p.seed, 10) || 1, gi = parseInt(p.gameIndex, 10) || 0;
  const published = path.join(dir, `claude_s${seed}_gi${gi}.json`);
  if (r.savedAs) fail(`a tampered choice stream (index ${hit} -> 999999) was PUBLISHED as ${r.savedAs}`);
  else if (fs.existsSync(published)) fail('the save was refused but a file was written to the destination anyway');
  else if (!r.divergedAs) fail('the refusal did not quarantine the diverged replay for triage');
  else ok(`refused and quarantined at ${r.divergedAs}: ${(r.diverged || [])[0] || ''}`.slice(0, 170));
  if (r.divergedAs) { try { fs.unlinkSync(path.join(ROOT, r.divergedAs)); } catch (e) {} }
  return true;
}

// =====================================================================================
async function main() {
  testAudit();

  const coverage = { types: new Set(), multi: 0, pins: 0, controlLive: 0, controlInert: 0 };
  // Deck-agnostic by construction, but the carriers this check exists for are only reachable on a
  // board that OFFERS them, so the set is chosen for coverage, not for any deck-specific claim:
  //   Goblins s1 -- multi-cast turns, i.e. the cast-order pin and its carrier-drop control;
  //   Goblins s5 -- tutor_etb + target + vial_charge (the tutor/wish target is picked at RESOLUTION
  //                 since 677e0b42, so it is its own --choices slot and its own carrier);
  //   burn s2    -- mulliganed on the way in;
  //   treasure_hunt s4 -- London bottoming, the MULTI-INT answer (K ints from one click);
  //   EldraziDisplacerFlicker s1 -- the deck the 2026-09-10 corruption happened on.
  // Whole set runs in a few seconds; the per-game cost is printed by --verbose.
  const GAMES = [
    { label: 'Goblins s1',       base: { deck: 'Goblins.cod', version: null, seed: 1, gameIndex: 0, maxTurns: 8, depth: 0 },
      opts: { mulligan: false, pin: true } },
    { label: 'Goblins s5',       base: { deck: 'Goblins.cod', version: null, seed: 5, gameIndex: 4, maxTurns: 8, depth: 0 },
      opts: { mulligan: false, pin: true } },
    { label: 'burn s2 (mull)',   base: { deck: 'burn.txt', version: null, seed: 2, gameIndex: 1, maxTurns: 8, depth: 0 },
      opts: { mulligan: true, pin: true } },
    { label: 'treasure_hunt s4', base: { deck: 'treasure_hunt.txt', version: null, seed: 4, gameIndex: 3, maxTurns: 8, depth: 0 },
      opts: { mulligan: false, pin: true } },
    { label: 'EDF s1',           base: { deck: 'EldraziDisplacerFlicker.cod', version: null, seed: 1, gameIndex: 0, maxTurns: 8, depth: 0 },
      opts: { mulligan: false, pin: true } },
  ];
  const sessions = [];
  for (const g of GAMES) {
    try { const s = await testGame(g.label, g.base, g.opts, coverage); if (s) sessions.push(s); }
    catch (e) { fail(`${g.label}: threw -- ${e && e.stack ? e.stack.split('\n')[0] : e}`); }
  }

  await testBinaryPin({ deck: 'treasure_hunt.txt', version: null, seed: 7, gameIndex: 6, maxTurns: 8, depth: 0 });
  testRefusal(sessions);
  await testRoutes({ deck: 'burn.txt', version: null, seed: 990001, gameIndex: 0, maxTurns: 8, depth: 0 });

  console.log('--- coverage ---');
  console.log(`  decision types driven: ${Array.from(coverage.types).sort().join(', ')}`);
  console.log(`  cast-order pins: ${coverage.pins}   multi-int answers: ${coverage.multi}   `
            + `carrier-drop controls: ${coverage.controlLive} live / ${coverage.controlInert} inconclusive`);
  // Coverage shortfalls are NOTES, not failures: which carriers a game reaches is a property of the
  // boards it plays into, and a brittle red gate on that is worse than none (same rule as
  // human_line_order_check.py). A carrier that goes UNEXERCISED is still reported, loudly.
  if (!coverage.pins) console.log('  NOTE: no cast-order pin was reachable -- carrier (a) went unexercised.');
  if (!coverage.types.has('tutor_etb') && !coverage.types.has('tutor') && !coverage.types.has('sac_tutor')) {
    console.log('  NOTE: no tutor resolution pick was reachable -- carrier (b) went unexercised.');
  }
  if (!coverage.multi) console.log('  NOTE: no multi-int answer was reachable -- carrier (c) went unexercised.');
  if (!coverage.controlLive && coverage.pins) {
    console.log('  NOTE: every cast-order pin was inert, so the carrier-drop control proved nothing.');
  }

  console.log(fails ? `viewer save parity: FAIL (${fails})` : 'viewer save parity: PASS');
  return fails ? 1 : 0;
}

main()
  .then((c) => { try { fs.rmSync(SCRATCH, { recursive: true, force: true }); fs.rmSync(DEST, { recursive: true, force: true }); } catch (e) {} process.exit(c); })
  .catch((e) => { console.error('setup/harness error: ' + (e && e.stack || e)); process.exit(2); });
