#!/usr/bin/env node
// PER-CLICK latency of the play viewer, measured through server.js's REAL step entry point.
//
// WHY THIS EXISTS AND WHY IT IS NOT test/edf_viewer_latency.py: the python tool times ONE stateless
// engine invocation for a given --choices prefix, which is what a click costs when the persistent
// --interactive child cannot be reused. Whether it CAN be reused is a server.js question -- the
// session key is every argv except --choices, so anything that changes an argv (a new --cast-order
// pin, a firebreathe answer) silently forces a full-prefix respawn. That is invisible to the engine
// and invisible to any check that calls the engine directly; only driving runStepCached with a
// realistic, GROWING side-channel map can see it.
//
// It replays a saved reference click-by-click exactly as the browser does: after committing a
// main-phase pick the client records the applied cast order under that decision's main_ordinal
// (index.html, S.castOrder) and sends the whole map with the NEXT step. This harness reproduces
// that from the reference's own recorded `cast_order` entries.
//
// Reports per click: wall ms, engine spawns, and whether the interactive session survived.
//
//   node test/viewer_click_latency.js references/<Deck>/claude_s9_gi8.json [--no-cast-order]
//   MTG_BIN=... PLAY_INTERACTIVE=0 node test/viewer_click_latency.js <ref>     # stateless arm
//
// --no-cast-order drops the side channel entirely, which isolates how much of the cost is the
// cache being defeated rather than the frames themselves.
//
// --validate models the REAL commit flow rather than a bare plan click: `Commit Line` POSTs
// /api/validate first and /api/step only after the verdict, so a committed line costs two engine
// round-trips. Leaving it out understates a queue-driven deck (which EldraziDisplacerFlicker is)
// by roughly half.

const fs = require('fs');
const path = require('path');

// Patch BEFORE requiring server.js: it destructures { spawnSync, spawn } at load time, so the
// counters have to be in place first. This is the only way to count the engine processes a click
// really costs -- the number the user pays for and the one no engine-side timer can see.
const cp = require('child_process');
let nSpawn = 0, nSpawnSync = 0;
const origSpawn = cp.spawn, origSpawnSync = cp.spawnSync;
cp.spawn = function (...a) { nSpawn++; return origSpawn.apply(this, a); };
cp.spawnSync = function (...a) { nSpawnSync++; return origSpawnSync.apply(this, a); };

const ROOT = path.resolve(__dirname, '..');
const server = require(path.join(ROOT, 'tools', 'play', 'server.js'));

const SIDE_CHANNEL_TYPES = new Set(['firebreathe', 'storage_hold']);

function flattenChoices(decisions) {
  const out = [];
  for (const d of decisions) {
    if (SIDE_CHANNEL_TYPES.has((d.decision || {}).type)) continue;
    const c = d.chosen;
    if (Array.isArray(c)) { for (const x of c) out.push(Number(x)); }
    else out.push(Number(c));
  }
  return out;
}

// A parseable --validate-line for a pending main-phase frame: its rank-best plan, re-encoded.
// Not the line the human really queued (references do not record the string), but the cost of a
// validation is the prefix replay, not the line -- and the same encoding is used on both arms.
function encodeLine(dec) {
  if (!dec || dec.type !== 'main_phase') return null;
  const p0 = (dec.plans || [])[0];
  if (!p0) return null;
  const parts = [];
  if (p0.land) parts.push('land=' + p0.land);
  for (const n of (p0.cast_order_canonical || p0.casts || [])) parts.push('cast=' + n);
  return parts.length ? parts.join(';') : 'pass';
}

async function main() {
  const args = process.argv.slice(2);
  const refPath = args.find(a => !a.startsWith('--'));
  const noCastOrder = args.includes('--no-cast-order');
  const withValidate = args.includes('--validate');
  const limit = (() => { const i = args.indexOf('--limit'); return i >= 0 ? parseInt(args[i + 1], 10) : Infinity; })();
  if (!refPath) { console.error('usage: node test/viewer_click_latency.js <reference.json> [--no-cast-order] [--limit N]'); process.exit(2); }

  const ref = JSON.parse(fs.readFileSync(refPath, 'utf8'));
  const deckName = path.basename(path.dirname(path.resolve(refPath)));
  let deckFile = null;
  for (const ext of ['.cod', '.txt']) {
    if (fs.existsSync(path.join(ROOT, 'decks', deckName, deckName + ext))) { deckFile = deckName + ext; break; }
  }
  if (!deckFile) { console.error('no decklist for ' + deckName); process.exit(2); }

  const decisions = ref.decisions;
  const choices = flattenChoices(decisions);

  // main_ordinal -> cast_order, exactly as the reference recorded it. The client only knows a pin
  // AFTER committing that decision, so it is added to the map at the step that follows.
  const pinAt = new Map();   // choices-stream position -> [ordinalKey, entries]
  {
    let pos = 0;
    for (const d of decisions) {
      const dec = d.decision || {};
      if (SIDE_CHANNEL_TYPES.has(dec.type)) continue;
      const n = Array.isArray(d.chosen) ? d.chosen.length : 1;
      if (dec.type === 'main_phase' && d.cast_order && dec.main_ordinal != null) {
        pinAt.set(pos, [String(dec.main_ordinal), d.cast_order]);
      }
      pos += n;
    }
  }

  const base = { deck: deckFile, version: null, seed: ref.seed, gameIndex: ref.game_index, maxTurns: 8, depth: 0 };
  const castOrder = {};
  const n = Math.min(choices.length, limit);
  console.log(`# ref=${refPath} deck=${deckFile} picks=${choices.length} castOrderPins=${pinAt.size}`
            + ` interactive=${process.env.PLAY_INTERACTIVE !== '0'} castOrder=${!noCastOrder}`
            + ` validate=${withValidate}`);
  console.log(`${'click'.padStart(5)} ${'wall_ms'.padStart(8)} ${'spawns'.padStart(7)}  frame`);
  let totalMs = 0, totalSpawn = 0, worst = 0;
  let pending = null;                            // the decision the next click answers
  for (let k = 0; k <= n; k++) {
    const s0 = nSpawn + nSpawnSync;
    const t0 = process.hrtime.bigint();
    // THE BROWSER'S ORDER, and it matters: commitLine() POSTs /api/validate with the choices and
    // cast-order map AS THEY STAND (no pin for the decision being committed -- applyAccepted
    // records that only after the verdict), and /api/step follows. Validating with the pin already
    // in the map would make the harness miss the cached validate path the real client hits.
    if (withValidate && pending) {
      const line = encodeLine(pending);
      // runValidateCached is the post-2026-09-11 entry point; fall back to the stateless
      // runValidate so this harness can also drive an OLDER server.js as the A/B's before-arm.
      const validate = server.runValidateCached || server.runValidate;
      if (line) await validate({ ...base, choices: choices.slice(0, k - 1),
                                 castOrder: noCastOrder ? {} : castOrder }, line);
    }
    if (!noCastOrder && k > 0 && pinAt.has(k - 1)) {      // applyAccepted records the pin
      const [key, entries] = pinAt.get(k - 1);
      castOrder[key] = entries;
    }
    const p = { ...base, choices: choices.slice(0, k), castOrder: noCastOrder ? {} : castOrder };
    const out = await server.runStepCached(p, null);
    const t1 = process.hrtime.bigint();
    const ms = Number(t1 - t0) / 1e6;
    const sp = (nSpawn + nSpawnSync) - s0;
    totalMs += ms; totalSpawn += sp; if (ms > worst) worst = ms;
    let frame = out.kind;
    pending = out.kind === 'decision' ? out.decision : null;
    if (out.kind === 'decision') {
      const d = out.decision;
      frame = `idx=${d.decision_index} t${d.turn} ${d.phase} ${d.type} plans=${(d.plans || []).length}/${d.plans_total != null ? d.plans_total : (d.plans || []).length}`;
    } else if (out.kind === 'error') { frame = 'ERROR: ' + String(out.error).slice(0, 120); }
    console.log(`${String(k).padStart(5)} ${ms.toFixed(1).padStart(8)} ${String(sp).padStart(7)}  ${frame}`);
  }
  console.log(`# total ${totalMs.toFixed(0)} ms over ${n + 1} clicks, ${totalSpawn} engine spawns,`
            + ` worst click ${worst.toFixed(1)} ms`);
  process.exit(0);
}

main().catch(e => { console.error(e); process.exit(1); });
