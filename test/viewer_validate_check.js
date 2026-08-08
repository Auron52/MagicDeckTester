#!/usr/bin/env node
// Viewer VALIDATE-LINE regression check (engine CheckLine layer).
// ============================================================================
// The two existing viewer checks have a blind spot: neither exercises `--validate-line`
// (TurnSolver::CheckLine). viewer_protocol_check.py feeds chosen plan INDICES; the
// linebuild check drives linebuild.js and asserts reconstruction — but nobody validates
// the reconstructed line against the engine. So a CheckLine regression (e.g. the payability
// accept-gate wrongly rejecting a payable line, or the affordability sim false-flagging a
// flexible mana source like Lotus Bloom as unpayable) would pass both checks silently.
//
// This check closes that gap. References under references/<deck>/ are CLEAN accepted games
// (no rejects), so EVERY main-phase line in them was accepted at play time. For each such
// line we rebuild it exactly as the GUI would (linebuild.js queueCard + encodeLine), replay
// the deterministic choices prefix, and run `--validate-line`. The verdict MUST be
// accept / choose (or unsupported for an X/tutor line the v1 checker declines) — an
// `illegal` or `legal_not_enumerated` verdict on a line the user actually played is a
// CheckLine regression (a payable/enumerated line now judged unpayable/unenumerated).
//
// v1 CheckLine has KNOWN limitations (X / tutor / alt-cost lines it declines, and a few refs whose
// deterministic choices prefix lands the validator on a sub-decision -> NO_VALIDATION_BLOCK). Those
// are not regressions, so this check is BASELINE-GATED: test/viewer_validate_baseline.txt records the
// known-fail signatures (deck/file | turn/phase | encoded line), and the check fails ONLY on a fail
// whose signature is NOT in the baseline -- a genuine CheckLine regression. Regenerate the baseline
// with --update-baseline after an intended CheckLine change (inspect the diff first).
//
// CAVEAT (positional --choices replay): a FEW baseline entries are NOT CheckLine limitations but
// CHOICE-STREAM SHIFTS from adding a human-play decision point. This check rebuilds each line by
// replaying the recorded `chosen` indices positionally; if the CURRENT engine emits a human-play
// breakpoint the recording predates (e.g. MTG_PLAY_SEGMENT_ALWAYS's intra-phase re-prompt), that
// inserted decision desyncs the recording's later choices, so downstream lines validate against a
// shifted state. Verify such a fail is a stream-shift (not a real CheckLine bug) by reverting the
// engine change and re-running: it should vanish.
//
// PARTIALLY FIXED (2026-08-08): `alignPrefix` below implements the auto-pass half of the deferred
// robust fix -- it probes what the engine is really offering and passes extra MAIN-PHASE segments
// until the recorded frame lines up. What it still cannot absorb is a SUB-DECISION (lackey_put /
// tutor_etb / echo) interleaved at the shift point: the probe stops there, so the line reports
// NO_VALIDATION_BLOCK. That residue is Goblins-shaped (its lines create new play mid-phase, so it
// is the deck A1 re-prompts most) -- see docs/design/viewer-validate-stream-alignment.md for the
// remaining work and the two cheaper alternatives.
//
// Usage:  node test/viewer_validate_check.js [--update-baseline] [deckFilter ...]
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const LB = require(path.join(ROOT, 'tools', 'play', 'linebuild.js'));
const REFROOT = path.join(ROOT, 'references');
const DECKS = path.join(ROOT, 'decks');
const CARDS = path.join(ROOT, 'src', 'cards', 'data', 'cards.json');
const BIN = process.env.MTG_BIN || path.join(ROOT, 'build', 'Release', 'mtg');
const BASELINE = path.join(__dirname, 'viewer_validate_baseline.txt');
const args = process.argv.slice(2);
const UPDATE = args.includes('--update-baseline');
const FILTER = args.filter(a => a !== '--update-baseline');

function resolveDeck(deck) {
  const dir = path.join(DECKS, deck);
  for (const ext of ['cod', 'txt']) {
    const f = path.join(dir, deck + '.' + ext);
    if (fs.existsSync(f)) {
      const prof = path.join(dir, deck + '.profile.json');
      return { deckPath: f, profilePath: fs.existsSync(prof) ? prof : null };
    }
  }
  return null;
}

function flatten(chosen) { return Array.isArray(chosen) ? chosen.slice() : [chosen]; }

// Decision types on a keyed SIDE-CHANNEL (not the positional --choices stream): skipped from --choices,
// reconstructed as --firebreathe / --storage-hold / --cast-order (keyed by turn / land# / main-ordinal).
const SIDE_CHANNEL = new Set(['firebreathe', 'storage_hold']);

// Types --force-mulligan resolves INTERNALLY: under force the engine never calls the external
// chooser for keep/bottom, so they consume no --choices slot. Mirrors viewer_protocol_check.py's
// FORCED_MULLIGAN_TYPES -- flatten and forceArg MUST agree or the positional stream desyncs.
const FORCED_MULLIGAN_TYPES = new Set(['mulligan', 'bottom']);

// Reconstruct the recorded opening hand. WITHOUT this the check replays the LIVE mulligan, so a
// reference recorded after mulliganing to 3 is validated against a 7-card game -- every positional
// choice then indexes a different decision and the verdicts are meaningless. That was this check's
// dominant failure mode: references carrying mulligan FRAMES accidentally compensated (their
// recorded keep/bottom answers were replayed positionally AS the live mulligan), so the breakage
// stayed hidden until ref_regenerate.py folded those frames into the `mulligan` header and removed
// the accident. Measured on treasure_hunt/claude_s4_gi3 T2: `illegal (land drop unavailable)`
// without force, `accept` with it.
function forceArg(ref) {
  const m = ref.mulligan;
  if (!m) { return null; }   // predates mulligan recording -> the engine's live mulligan is used
  return `${m.count || 0}:` + (m.bottom || []).join(',');
}

function sideChannelArgs(decisions) {
  const fb = [], sh = [], co = [];
  for (const de of decisions) {
    const d = de.decision || {}, t = d.type;
    if (t === 'firebreathe') fb.push(`${d.turn}:${de.chosen}`);
    else if (t === 'storage_hold') sh.push(`${d.turn}:${d.land_idx}:${de.chosen}`);
    else if (t === 'main_phase' && Array.isArray(de.cast_order) && de.cast_order.length) co.push(`${d.main_ordinal}:${de.cast_order.join('|')}`);
  }
  const extra = [];
  if (fb.length) extra.push('--firebreathe', fb.join(','));
  if (sh.length) extra.push('--storage-hold', sh.join(','));
  if (co.length) extra.push('--cast-order', co.join(';'));
  return extra;
}

function runValidate(dk, seed, gi, maxTurns, choices, line, extra, force) {
  const args = [dk.deckPath];
  if (dk.profilePath) args.push('--profile', dk.profilePath);
  args.push('--cards-json', CARDS, '--claude-play', '--seed', String(seed),
            '--game-index', String(gi), '--max-turns', String(maxTurns), '--depth', '0',
            '--choices', choices.join(','), '--validate-line', line);
  if (force !== null && force !== undefined) args.push('--force-mulligan', force);
  if (extra && extra.length) args.push(...extra);
  let out;
  try { out = execFileSync(BIN, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 }); }
  catch (e) { out = (e.stdout || '') + (e.stderr || ''); }   // exit 71 is expected (validation)
  const m = /<<<CLAUDE_VALIDATION>>>([\s\S]*?)<<<END_VALIDATION>>>/.exec(out);
  if (!m) return { verdict: 'NO_VALIDATION_BLOCK' };
  try { return JSON.parse(m[1]); } catch (e) { return { verdict: 'PARSE_ERROR' }; }
}

// Which decision does the engine actually present after `choices`? Returns {type,turn,phase} or null.
function probeFrame(dk, seed, gi, maxTurns, choices, extra, force) {
  const args = [dk.deckPath];
  if (dk.profilePath) args.push('--profile', dk.profilePath);
  args.push('--cards-json', CARDS, '--claude-play', '--seed', String(seed),
            '--game-index', String(gi), '--max-turns', String(maxTurns), '--depth', '0',
            '--choices', choices.join(','));
  if (force !== null && force !== undefined) args.push('--force-mulligan', force);
  if (extra && extra.length) args.push(...extra);
  let out;
  try { out = execFileSync(BIN, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 }); }
  catch (e) { out = (e.stdout || '') + (e.stderr || ''); }   // exit 70 is expected (decision)
  const m = /<<<CLAUDE_DECISION>>>([\s\S]*?)<<<END_DECISION>>>/.exec(out);
  if (!m) return null;
  try { const d = JSON.parse(m[1]); return { type: d.type, turn: d.turn, phase: d.phase }; }
  catch (e) { return null; }
}

// Absorb main-phase frames the RECORDING does not have an answer for, by passing them (-1).
//
// The positional --choices stream assumes the engine presents exactly the frames the reference
// recorded. MTG_PLAY_SEGMENT_ALWAYS breaks that for references recorded before it: Commit Line now
// re-prompts inside a main phase, so a pre-segment recording of "T2: play Mountain" is followed by
// a SECOND T2/pre_main frame nobody answered. Every later choice then slips by one and validates
// against the wrong board -- e.g. Goblins/claude_s22_gi21's T3 line reported "land drop
// unavailable" because the prefix had landed on T2/post_main.
//
// Fix (this is the "auto-pass extra breakpoints" the header deferred): before validating a recorded
// frame, probe what the engine is actually offering; while that is a DIFFERENT main-phase frame,
// answer it with a pass and probe again. Pass is the right answer -- the human's recorded line for
// that phase is already committed in the previous segment -- and it is what
// viewer_protocol_check.py does for the same unaligned frame. Bounded, and it only ever inserts
// passes, so a genuinely unenumerated or unpayable line still reports its real verdict.
const MAX_AUTOPASS = 8;
function alignPrefix(dk, ref, maxTurns, choices, want, extra, force) {
  for (let i = 0; i < MAX_AUTOPASS; i++) {
    const f = probeFrame(dk, ref.seed, ref.game_index, maxTurns, choices, extra, force);
    if (!f || f.type !== 'main_phase') { return 0; }             // sub-decision / terminal: leave it
    if (f.turn === want.turn && f.phase === want.phase) { return i; }   // aligned
    choices.push(-1);
  }
  return MAX_AUTOPASS;
}

function collectRefs() {
  const out = [];
  for (const deck of fs.readdirSync(REFROOT)) {
    if (deck === 'suboptimal' || deck === 'optimal') continue;
    const dir = path.join(REFROOT, deck);
    if (!fs.statSync(dir).isDirectory()) continue;
    if (FILTER.length && !FILTER.includes(deck)) continue;
    for (const f of fs.readdirSync(dir)) {
      if (/^claude_s.*_gi.*\.json$/.test(f)) out.push({ deck, file: path.join(dir, f) });
    }
  }
  return out.sort((a, b) => a.file.localeCompare(b.file));
}

function main() {
  const refs = collectRefs();
  if (!refs.length) { console.log('no references found'); return 0; }
  const tally = { accept: 0, choose: 0, unsupported: 0, skipped: 0 };
  const fails = [];
  let autopassed = 0;   // extra main-phase segments absorbed (see alignPrefix)
  for (const { deck, file } of refs) {
    const dk = resolveDeck(deck);
    if (!dk) { console.log(`  SKIP  ${deck} (deck file not found)`); continue; }
    const ref = JSON.parse(fs.readFileSync(file, 'utf8'));
    const maxTurns = Math.max(8, (ref.win_turn || 8) + 1);
    const rel = path.relative(REFROOT, file);
    const extra = sideChannelArgs(ref.decisions || []);   // --firebreathe / --storage-hold / --cast-order the ref used
    const force = forceArg(ref);   // reconstruct the recorded opening hand (see forceArg)
    const choices = [];
    for (const de of ref.decisions || []) {
      const d = de.decision || {};
      const ch = de.chosen;
      if (SIDE_CHANNEL.has(d.type)) { continue; }   // side-channel: no --choices slot, no line to validate
      // Under force the engine answers keep/bottom itself -> those frames carry no positional slot.
      if (force !== null && FORCED_MULLIGAN_TYPES.has(d.type)) { continue; }
      if (d.type === 'main_phase' && typeof ch === 'number' && ch >= 0 && ch < (d.plans || []).length) {
        const plan = d.plans[ch];
        const casts = plan.casts || [];
        const hand = (d.me && d.me.hand) || [];
        // Non-hand casts (retrace from yard / vial deploy) go through a different GUI path than
        // queueCard -> out of scope for validate v1 (same exclusion as the linebuild check).
        if (casts.some(nm => !hand.some(c => c.name === nm))) { tally.skipped++; }
        else {
          let built = [];
          if (plan.land) built = LB.queueCard(d, built, plan.land, 'land');
          for (const nm of casts) { const hc = hand.find(c => c.name === nm); built = LB.queueCard(d, built, nm, hc.kind); }
          const line = LB.encodeLine(built);
          autopassed += alignPrefix(dk, ref, maxTurns, choices, { turn: d.turn, phase: d.phase }, extra, force);
          const v = runValidate(dk, ref.seed, ref.game_index, maxTurns, choices, line, extra, force);
          const verdict = v.verdict;
          if (verdict === 'accept' || verdict === 'choose' || verdict === 'unsupported') { tally[verdict]++; }
          else {
            const sig = `${rel} | T${d.turn}/${d.phase} | ${line}`;
            fails.push({ sig, verdict, reason: v.reason || '' });
          }
        }
      }
      choices.push(...flatten(ch));
    }
  }
  // --update-baseline: record the current known-fail signatures (only meaningful for a FULL run).
  if (UPDATE) {
    if (FILTER.length) { console.log('refusing to update baseline from a FILTERED run -- run over all decks'); return 1; }
    const lines = fails.map(f => f.sig).sort();
    fs.writeFileSync(BASELINE, lines.join('\n') + (lines.length ? '\n' : ''));
    console.log(`wrote ${lines.length} known-fail signatures to ${path.relative(ROOT, BASELINE)}`);
    return 0;
  }

  const baseline = new Set(
    fs.existsSync(BASELINE) ? fs.readFileSync(BASELINE, 'utf8').split('\n').map(s => s.trim()).filter(Boolean) : []);
  const nowSigs = new Set(fails.map(f => f.sig));
  const regressions = fails.filter(f => !baseline.has(f.sig));
  const fixed = FILTER.length ? [] : [...baseline].filter(s => !nowSigs.has(s));   // only meaningful on a full run

  for (const f of regressions) {
    console.log(`  REGRESSION  ${f.sig}  -> ${f.verdict}` + (f.reason ? `  (${f.reason})` : ''));
  }
  console.log(`\nViewer validate-line: ${tally.accept} accept, ${tally.choose} choose, ` +
              `${tally.unsupported} unsupported, ${tally.skipped} skipped (non-hand cast), ` +
              `${fails.length} known-fail(v1 limits), ${regressions.length} REGRESSION  (${refs.length} refs)` +
              (autopassed ? `  [${autopassed} extra segment(s) auto-passed]` : ''));
  if (regressions.length) {
    console.log('  REGRESSION = a played line that USED TO validate now fails (not in the baseline) -> a real');
    console.log('  CheckLine regression. If intended, inspect then rebaseline: node test/viewer_validate_check.js --update-baseline');
  }
  if (fixed.length) { console.log(`  (${fixed.length} baseline known-fail(s) now pass -- rebaseline to drop them.)`); }
  return regressions.length ? 1 : 0;
}

process.exit(main());
