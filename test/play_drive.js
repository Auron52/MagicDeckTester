#!/usr/bin/env node
// Headless driver for the play GUI's protocol — the same runStep/runValidate the browser talks to,
// driven from a script instead of clicks. Used to REPRODUCE a reported viewer issue (a line the user
// played by hand) without a browser, and to TIME each commit so "committing is slow" becomes a number.
//
//   node test/play_drive.js --deck "Mirrorwing Dragon.cod" --seed 22 --gi 21 \
//        --lines "land=Forest;cast=Elvish Mystic" --lines "..." [--maxturns 8]
//
// Non-main-phase decisions (mulligan / bottom / target / firebreathe …) are answered with the
// frame's own heuristic_default (or ai_choice), exactly like viewer_protocol_check.py does, so the
// script only has to spell out the main-phase LINES the human assembled.
const { runStep, runValidate } = require('../tools/play/server.js');

const argv = process.argv.slice(2);
function opt(name, dflt) { const i = argv.indexOf('--' + name); return i < 0 ? dflt : argv[i + 1]; }
function optAll(name) { const out = []; argv.forEach((a, i) => { if (a === '--' + name) out.push(argv[i + 1]); }); return out; }

const p = {
  deck: opt('deck', 'Mirrorwing Dragon.cod'),
  seed: parseInt(opt('seed', '1'), 10),
  gameIndex: parseInt(opt('gi', String(parseInt(opt('seed', '1'), 10) - 1)), 10),
  maxTurns: parseInt(opt('maxturns', '8'), 10),
  depth: 0,
  choices: [],
};
// --pre "a,b,c": a fixed choice prefix (the recorded mulligan/bottom picks of a session being
// reproduced) consumed before any --lines, instead of answering those frames with the default.
const pre = (opt('pre', '') || '').split(',').filter(s => s !== '').map(Number);
const lines = optAll('lines');
const verbose = argv.includes('-v');
let li = 0;

function ms(t) { return (Number(process.hrtime.bigint() - t) / 1e6).toFixed(0) + 'ms'; }

// --prefer <substr> (repeatable): on a `target` decision, take the option whose label contains the
// substring (the human's "always aim the trick at the copy magnet"), else the frame's own default.
const prefer = optAll('prefer');
function defaultFor(d) {
  if (d.type === 'target' && prefer.length) {
    const hit = (d.options || []).find(o => prefer.some(s => (o.label || '').includes(s)));
    if (hit) return hit.index;
  }
  if (d.ai_choice !== undefined && d.ai_choice !== null) return d.ai_choice;
  if (d.heuristic_default !== undefined && d.heuristic_default !== null) return d.heuristic_default;
  return 0;
}

for (let guard = 0; guard < 400; guard++) {
  let t = process.hrtime.bigint();
  const r = runStep(p, null);
  if (r.kind === 'error') { console.log('ERROR', r.error, (r.raw || '').slice(0, 800)); break; }
  if (r.kind === 'result') { console.log(`RESULT won=${r.result.won} win_turn=${r.result.win_turn} decisions=${r.result.decisions_made} (step ${ms(t)})`); break; }
  const d = r.decision;
  if (argv.includes('--dump')) {
    const bf = (d.me && d.me.battlefield || []).map(b => `${b.name}${b.tapped ? '(T)' : ''}`).join(', ');
    console.log(`     board t${d.turn}: ${bf}  | float ${JSON.stringify(d.me && d.me.floating_mana || null)}`);
  }
  if (d.type !== 'main_phase') {
    const c = p.choices.length < pre.length ? pre[p.choices.length] : defaultFor(d);
    console.log(`  [${d.type}] -> default ${c}   (step ${ms(t)})`);
    p.choices.push(c);
    continue;
  }
  if (li >= lines.length) {
    console.log(`  [main_phase t${d.turn}] no more --lines; ${(d.plans || []).length} plans offered (step ${ms(t)})`);
    if (verbose) (d.plans || []).slice(0, 30).forEach(pl => console.log('      ', pl.index, pl.summary));
    break;
  }
  const line = lines[li++];
  t = process.hrtime.bigint();
  const v = runValidate(p, line);
  const vms = ms(t);
  if (v.kind !== 'validation') { console.log('  non-validation reply:', v.kind, v.error || ''); break; }
  console.log(`  [main_phase t${d.turn}] "${line}" -> ${v.verdict} (validate ${vms}, ${(v.variants || []).length} variants)`);
  if (v.verdict === 'accept') p.choices.push(v.plan_index);
  else if (v.verdict === 'choose') {
    console.log('     variants:'); (v.variants || []).slice(0, 12).forEach(x => console.log('       ', x.plan_index, x.label));
    p.choices.push(v.variants[0].plan_index);
  } else { console.log('     reason:', v.reason, JSON.stringify(v.failed_action || null)); break; }
}
console.log('choices:', p.choices.join(','));
