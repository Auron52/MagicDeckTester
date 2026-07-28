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
// breakpoint the recording predates (e.g. #12 Commit Line's same-turn sac re-prompt --
// treasure_hunt/claude_s4_gi3 T7/T8), that inserted decision desyncs the recording's later choices,
// so downstream lines validate against a shifted state. Verify such a fail is a stream-shift (not a
// real CheckLine bug) by reverting the engine change and re-running: it should vanish. A robust fix
// (drive the engine decision-by-decision and auto-pass extra breakpoints) is deferred; see
// docs/design/viewer-fixes-2026-07-27.md (batch 4).
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

function runValidate(dk, seed, gi, maxTurns, choices, line, extra) {
  const args = [dk.deckPath];
  if (dk.profilePath) args.push('--profile', dk.profilePath);
  args.push('--cards-json', CARDS, '--claude-play', '--seed', String(seed),
            '--game-index', String(gi), '--max-turns', String(maxTurns), '--depth', '0',
            '--choices', choices.join(','), '--validate-line', line);
  if (extra && extra.length) args.push(...extra);
  let out;
  try { out = execFileSync(BIN, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 }); }
  catch (e) { out = (e.stdout || '') + (e.stderr || ''); }   // exit 71 is expected (validation)
  const m = /<<<CLAUDE_VALIDATION>>>([\s\S]*?)<<<END_VALIDATION>>>/.exec(out);
  if (!m) return { verdict: 'NO_VALIDATION_BLOCK' };
  try { return JSON.parse(m[1]); } catch (e) { return { verdict: 'PARSE_ERROR' }; }
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
  for (const { deck, file } of refs) {
    const dk = resolveDeck(deck);
    if (!dk) { console.log(`  SKIP  ${deck} (deck file not found)`); continue; }
    const ref = JSON.parse(fs.readFileSync(file, 'utf8'));
    const maxTurns = Math.max(8, (ref.win_turn || 8) + 1);
    const rel = path.relative(REFROOT, file);
    const extra = sideChannelArgs(ref.decisions || []);   // --firebreathe / --storage-hold / --cast-order the ref used
    const choices = [];
    for (const de of ref.decisions || []) {
      const d = de.decision || {};
      const ch = de.chosen;
      if (SIDE_CHANNEL.has(d.type)) { continue; }   // side-channel: no --choices slot, no line to validate
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
          const v = runValidate(dk, ref.seed, ref.game_index, maxTurns, choices, line, extra);
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
              `${fails.length} known-fail(v1 limits), ${regressions.length} REGRESSION  (${refs.length} refs)`);
  if (regressions.length) {
    console.log('  REGRESSION = a played line that USED TO validate now fails (not in the baseline) -> a real');
    console.log('  CheckLine regression. If intended, inspect then rebaseline: node test/viewer_validate_check.js --update-baseline');
  }
  if (fixed.length) { console.log(`  (${fixed.length} baseline known-fail(s) now pass -- rebaseline to drop them.)`); }
  return regressions.length ? 1 : 0;
}

process.exit(main());
