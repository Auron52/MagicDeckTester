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
// ALIGNMENT (fixed 2026-08-08 -- this was the old "positional --choices replay" caveat): the
// prefix for each line is no longer the recording's raw indices. It comes from
// viewer_protocol_check.py --emit-resolved, which walks the reference against the CURRENT engine
// and hands back the content-resolved pick stream plus each recorded main-phase frame's offset
// into it (see resolveAlignment). That makes this check immune to added human-play breakpoints --
// MTG_PLAY_SEGMENT_ALWAYS's intra-phase re-prompt shifted every later choice by one and produced
// ~290 bogus `illegal` / NO_VALIDATION_BLOCK verdicts before this.
//
// The two checks therefore share ONE alignment implementation. Keep it that way: the divergence
// between them is precisely how this check sat at 141 stale failures unnoticed
// (docs/design/viewer-validate-stream-alignment.md).
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

// Ask viewer_protocol_check.py for the CONTENT-RESOLVED replay of each reference: the pick stream
// that actually reproduces the recorded game, plus where every recorded main-phase frame sits in it.
//
// Why borrow instead of reimplementing: the positional `--choices` stream assumes the engine still
// presents exactly the frames the reference recorded, and that stops being true the moment a
// human-play decision point is added. MTG_PLAY_SEGMENT_ALWAYS re-prompts inside a main phase, so a
// pre-segment recording of "T2: play Mountain" is followed by a SECOND T2/pre_main frame nobody
// answered, and every later choice slips by one -- Goblins/claude_s22_gi21's T3 line reported
// "land drop unavailable" purely because the prefix had landed on T2/post_main. The protocol
// checker already solves this (frame_ident alignment: pass an unaligned main_phase, answer any
// other unaligned frame as the unattended engine would, re-anchor stale indices by plan content),
// so this check consumes that walk rather than growing a second, divergent copy -- two independent
// alignment implementations is exactly how this check drifted to 141 stale failures unnoticed.
// One python process for the whole set; each line of stdout is one reference.
function resolveAlignment(files) {
  const out = new Map();
  if (!files.length) return out;
  let stdout;
  try {
    stdout = execFileSync('python3', [path.join(__dirname, 'viewer_protocol_check.py'),
                                      '--emit-resolved', ...files],
                          { encoding: 'utf8', maxBuffer: 512 * 1024 * 1024, cwd: ROOT });
  } catch (e) {
    stdout = (e.stdout || '');
    if (!stdout.trim()) {
      console.log('FAIL: could not run viewer_protocol_check.py --emit-resolved: ' + (e.message || e));
      return out;
    }
  }
  for (const ln of stdout.split('\n')) {
    const t = ln.trim();
    if (!t.startsWith('{')) continue;
    try { const o = JSON.parse(t); out.set(o.path, o); } catch (e) { /* not a result line */ }
  }
  return out;
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
  let unaligned = 0;   // refs/frames the resolver could not place (reported, never silent)
  const aligned = resolveAlignment(refs.map(r => r.file));
  for (const { deck, file } of refs) {
    const ref = JSON.parse(fs.readFileSync(file, 'utf8'));
    const maxTurns = Math.max(8, (ref.win_turn || 8) + 1);
    const rel = path.relative(REFROOT, file);
    const extra = sideChannelArgs(ref.decisions || []);   // --firebreathe / --storage-hold / --cast-order the ref used
    const force = forceArg(ref);   // reconstruct the recorded opening hand (see forceArg)

    const al = aligned.get(file);
    // Deck + profile come from the RESOLVER, not from a second name->path guess here. The
    // references/<dir> name need not match decks/<dir> (references/Creature_Giving vs
    // "decks/Creature Giving"), and the local guess silently skipped that whole deck.
    const dk = al && al.deck ? { deckPath: al.deck, profilePath: al.prof || null } : resolveDeck(deck);
    if (!dk) { console.log(`  SKIP  ${deck} (deck file not found)`); unaligned++; continue; }
    if (!al || !al.ok) {
      console.log(`  SKIP  ${rel} (protocol replay could not resolve it: ${al ? al.detail : 'no alignment'})`);
      unaligned++;
      continue;
    }
    // kept: the decisions that occupy a positional slot -- MUST match the resolver's own filter,
    // which is asserted per frame below (turn/phase agreement) rather than assumed.
    const kept = (ref.decisions || []).filter(de => {
      const t = (de.decision || {}).type;
      return !SIDE_CHANNEL.has(t) && !(force !== null && FORCED_MULLIGAN_TYPES.has(t));
    });

    for (const fr of al.frames) {
      const de = kept[fr.ri];
      const d = (de && de.decision) || {};
      if (d.turn !== fr.turn || d.phase !== fr.phase) {
        console.log(`  SKIP  ${rel} | T${fr.turn}/${fr.phase} (frame filter disagrees with the ` +
                    `resolver -- SIDE_CHANNEL/FORCED_MULLIGAN_TYPES have diverged)`);
        unaligned++;
        continue;
      }
      const ch = fr.recorded_index;
      if (!(typeof ch === 'number' && ch >= 0 && ch < (d.plans || []).length)) { continue; }  // a pass
      const plan = d.plans[ch];
      const casts = plan.casts || [];
      const hand = (d.me && d.me.hand) || [];
      // Non-hand casts (retrace from yard) go through a different GUI path than queueCard ->
      // out of scope for validate v1 (same exclusion as the linebuild check).
      if (casts.some(nm => !hand.some(c => c.name === nm))) { tally.skipped++; continue; }
      // An Aether Vial deploy IS in hand but is NOT cast -- no mana is paid. Encoding it as
      // `cast=` asks CheckLine to pay a cost that never existed, which is why
      // Goblins/claude_s18_gi17 T4 read `illegal (can't pay {1}{R}{R} for 'Goblin Chieftain')`
      // on a line that was played by putting it onto the battlefield with a vial. The plan JSON
      // carries no structured flag for this (unlike `activate`/`sacout`), so key off the summary
      // tag the emitter appends -- exact-match per cast name, since names contain commas
      // ("Krenko, Mob Boss") and a regex over the joined summary would mis-split them.
      // Walk the summary's cast section in plan order with a cursor: a plan can hold TWO copies of
      // one card with only the second vialled ("cast: Marshal of Zhalfir, Marshal of Zhalfir
      // (vial)"), and a bare `summary.includes(name + " (vial)")` would mark both. Cursor-matching
      // whole names also survives commas inside names ("Krenko, Mob Boss"), which splitting on ", "
      // would tear in half.
      const summary = plan.summary || '';
      const ci = summary.indexOf('cast:');
      const castSec = ci >= 0 ? summary.slice(ci + 5) : '';
      let cursor = 0;
      const vialFlag = casts.map(nm => {
        const at = castSec.indexOf(nm, cursor);
        if (at < 0) { return false; }
        cursor = at + nm.length;
        const v = castSec.startsWith(' (vial)', cursor);
        if (v) { cursor += ' (vial)'.length; }
        return v;
      });
      let built = [];
      if (plan.land) built = LB.queueCard(d, built, plan.land, 'land');
      casts.forEach((nm, i) => {
        const hc = hand.find(c => c.name === nm);
        built = LB.queueCard(d, built, nm, vialFlag[i] ? 'vial' : hc.kind);
      });
      const line = LB.encodeLine(built);
      // The resolver's prefix: replaying it leaves the engine offering exactly THIS frame.
      const choices = al.resolved.slice(0, fr.prefix_len);
      const v = runValidate(dk, ref.seed, ref.game_index, maxTurns, choices, line, extra, force);
      const verdict = v.verdict;
      if (verdict === 'accept' || verdict === 'choose' || verdict === 'unsupported') { tally[verdict]++; }
      else {
        const sig = `${rel} | T${d.turn}/${d.phase} | ${line}`;
        fails.push({ sig, verdict, reason: v.reason || '' });
      }
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
              (unaligned ? `  [${unaligned} unaligned -- see SKIP lines]` : ''));
  if (regressions.length) {
    console.log('  REGRESSION = a played line that USED TO validate now fails (not in the baseline) -> a real');
    console.log('  CheckLine regression. If intended, inspect then rebaseline: node test/viewer_validate_check.js --update-baseline');
  }
  if (fixed.length) { console.log(`  (${fixed.length} baseline known-fail(s) now pass -- rebaseline to drop them.)`); }
  return regressions.length ? 1 : 0;
}

process.exit(main());
