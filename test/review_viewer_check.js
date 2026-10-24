#!/usr/bin/env node
// Headless check for the reference REVIEW viewer (tools/replay/review.html + review_server.js).
//
// Drives the REAL page script in jsdom against the REAL server routes (makeServer, listening on an
// ephemeral port), over a synthetic fixture -- so it needs no engine build and never touches the
// user's logs/ref_review/ or references/review/verdicts.json, and the refsafe `protect` step (backup +
// autosave commit + push) is stubbed out. Pins:
//   * the index route merges verdicts and grades them unreviewed / current / stale (a verdict on a
//     search line the engine no longer plays must NOT read as reviewed);
//   * the page lists the game, lays both games out turn by turn, folds identical turns, and marks the
//     lost turn;
//   * a verdict click writes the stamped record (with the judged search_digest) and calls protect;
//     Clear removes it; the retired kinds are refused;
//   * Promote sends the RIGHT-HAND line's source (the hand-off at the lost turn by default), protects
//     every file the promotion wrote, and shows a refusal as NOT promoted (scripts/ref_promote.py is
//     stubbed -- its own replay gates are exercised by `ref_promote.py --check-all`);
//   * /review/ cannot escape the review directory.
//
// Run:  node test/review_viewer_check.js        (needs jsdom: test/node_modules)
const fs = require('fs');
const os = require('os');
const path = require('path');
let JSDOM;
try { ({ JSDOM } = require('jsdom')); }
catch (e) { console.error('jsdom not installed — add it to the container (npm i -D jsdom in test/).'); process.exit(2); }

const ROOT = path.resolve(__dirname, '..');
const { makeServer } = require(path.join(ROOT, 'tools', 'replay', 'review_server.js'));

let fails = 0;
const ok = (cond, msg) => { console.log((cond ? '  ok   ' : '  FAIL ') + msg); if (!cond) fails++; };

// ---- fixture -------------------------------------------------------------------------------
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'review_check_'));
const reviewDir = path.join(tmp, 'ref_review');
const verdicts = path.join(tmp, 'verdicts.json');
const gdir = path.join(reviewDir, 'Deck', 'claude_s1_gi0');
fs.mkdirSync(gdir, { recursive: true });
const step = (turn, phase, actions, bf, hand) => ({ turn, phase, actions,
  boardAfter: { battlefield: bf, hand, playerLife: 20, opponentLife: 20 } });
const land = (card, name) => ({ card, cardName: name, isLand: true, tapped: false });
const T1 = [step(1, 'MAIN_1', [{ type: 'PLAY_LAND', card: 1, cardName: 'Mountain' }], [land(1, 'Mountain')], [2, 3])];
const log = (turns, win) => ({ cardNumbering: { Mountain: [1], 'Lightning Bolt': [2], Island: [3] }, seed: 1,
                               gameNumber: 0, result: { winner: 'player', turn: win }, turns });
// The human log carries boards but NO plays, exactly as the engine writes it for a replayed choice
// stream; the page must read the plays back out of reference.json.
const human = log([step(1, 'MAIN_1', [], [land(1, 'Mountain')], [2, 3]),
                   step(2, 'MAIN_1', [], [land(1, 'Mountain'), land(3, 'Island')], [2])], 4);
const mainDec = (turn, summary) => ({ chosen: 0, decision: { type: 'main_phase', turn, phase: 'pre_main',
  plans: [{ index: 0, summary }] } });
fs.writeFileSync(path.join(gdir, 'reference.json'), JSON.stringify({ seed: 1, game_index: 0, win_turn: 4, won: true,
  decisions: [mainDec(1, 'land=Mountain; cast: (nothing)'), mainDec(2, 'land=Island; cast: (nothing)'),
              { chosen: -1, decision: { type: 'main_phase', turn: 2, phase: 'post_main', plans: [] } }] }));
const search = log(T1.concat([step(2, 'MAIN_1', [{ type: 'CAST_SPELL', card: 2, cardName: 'Lightning Bolt', manaPaid: '{R}' }],
                                   [land(1, 'Mountain')], [3])]), 3);
fs.writeFileSync(path.join(gdir, 'human.json'), JSON.stringify(human));
fs.writeFileSync(path.join(gdir, 'search.json'), JSON.stringify(search));
fs.writeFileSync(path.join(gdir, 'handoff_T2.json'), JSON.stringify(search));
const KEY = 'Deck/claude_s1_gi0.json';
const entry = { key: KEY, dir: 'Deck/claude_s1_gi0', ref_dir: 'Deck', deck_key: 'deck',
  seed: 1, gi: 0, human_win: 4, search_win: 3, delta: 1, search_digest: 'aaaa',
  files: { human: 'human.json', search: 'search.json', reference: 'reference.json' },
  replay: { ok: true, kind: 'ok', detail: '' }, human_log_matches: true, human_log_win: 4,
  ladder: [{ turn: 1, phase: 'pre_main', board: '', picks: 0, cv_win: 3, nc_win: 4, cv_log: null },
           { turn: 2, phase: 'pre_main', board: '', picks: 2, cv_win: 3, nc_win: 4, cv_log: 'handoff_T2.json' }],
  lost_turns: [{ turn: 2, cv_from: 3, cv_after: 4, nc_from: 4, nc_after: 4, hint: 'likely-clairvoyance' }],
  hint: 'likely-clairvoyance', hint_detail: 'fixture' };
const writeIndex = e => fs.writeFileSync(path.join(reviewDir, 'index.json'),
  JSON.stringify({ meta: { generated_at: 'now', src: '0123456789ab' }, games: [e] }));
writeIndex(entry);

const protects = [];
const promotes = [];
let promoteReply = null;
const server = makeServer({ reviewDir, verdicts,
  protect: async (rels, msg) => { protects.push({ rels, msg }); return { ok: true, pushed: false }; },
  promote: async (req) => { promotes.push(req); return promoteReply(req); } });

const sleep = ms => new Promise(r => setTimeout(r, ms));
async function until(fn, what, ms = 4000) {
  for (let t = 0; t < ms; t += 25) { const v = fn(); if (v) return v; await sleep(25); }
  ok(false, 'timed out waiting for ' + what);
  return null;
}

server.listen(0, '127.0.0.1', async () => {
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    // ---- routes ------------------------------------------------------------------------------
    let idx = await (await fetch(base + '/api/review')).json();
    ok(idx.games.length === 1 && idx.games[0].verdict_state === 'unreviewed', 'index route: game listed, unreviewed');
    const esc = await fetch(base + '/review/..%2f..%2fverdicts.json');
    ok(esc.status === 404, '/review/ cannot escape the review directory');

    // ---- page --------------------------------------------------------------------------------
    const html = fs.readFileSync(path.join(ROOT, 'tools', 'replay', 'review.html'), 'utf8');
    const dom = new JSDOM(html, { runScripts: 'dangerously', url: base + '/',
      beforeParse(w) { w.fetch = (u, o) => fetch(base + u, o); w.scrollTo = () => {}; w.Element.prototype.scrollIntoView = () => {}; } });
    const doc = dom.window.document;
    const table = await until(() => doc.querySelector('table.cmp'), 'the side-by-side table');
    ok(!!doc.querySelector(`.item[data-key="${KEY}"]`), 'page lists the game');
    const rows = table ? [...table.querySelectorAll('tr')].slice(1) : [];
    ok(rows.length === 2, 'one row per turn (2)');
    ok(rows[0] && rows[0].classList.contains('same') && rows[0].classList.contains('collapsed'), 'identical T1 is folded');
    ok(rows[1] && rows[1].classList.contains('lost') && !rows[1].classList.contains('same'), 'T2 differs and is marked as the lost turn');
    ok(rows[1] && /Lightning Bolt/.test(rows[1].textContent) && /land=Island/.test(rows[1].textContent),
       'T2 shows both lines (yours read from the reference\'s decisions)');
    ok(rows[1] && !/pass/.test(rows[1].cells[1].textContent), 'a pass is not listed as a play');
    const imgs = rows[1] ? [...rows[1].cells[1].querySelectorAll('.endb img')].map(i => i.alt) : [];
    ok(imgs.filter(n => n === 'Mountain' || n === 'Island').length === 2 && imgs.includes('Lightning Bolt'),
       'lands and hand render as card images (' + imgs.join(', ') + ')');
    ok(!!(rows[1] && rows[1].cells[1].querySelector('.thumb.diffcard img[alt="Island"]')),
       'a card only one side has is outlined on its image');
    ok(doc.getElementById('right').value === 'handoff_T2.json', 'right column defaults to the hand-off at the lost turn');

    // ---- verdict -----------------------------------------------------------------------------
    doc.getElementById('note').value = 'needed to know the top card';
    doc.querySelector('#vbar button[data-v="clairvoyance"]').click();
    await until(() => fs.existsSync(verdicts) && JSON.parse(fs.readFileSync(verdicts, 'utf8')).verdicts[KEY], 'the verdict write');
    const v = JSON.parse(fs.readFileSync(verdicts, 'utf8')).verdicts[KEY] || {};
    ok(v.verdict === 'clairvoyance' && v.note === 'needed to know the top card' && v.search_digest === 'aaaa'
       && v.human_win === 4 && v.search_win === 3, 'verdict stamped with the judged line');
    ok(protects.length === 1 && /clairvoyance/.test(protects[0].msg), 'save went through protect()');
    ok(!!await until(() => /Clairvoyance/.test((doc.querySelector('.item.sel') || {}).textContent || ''), 'the list chip'),
       'the marked game stays listed (default filter) and shows its verdict');

    idx = await (await fetch(base + '/api/review')).json();
    ok(idx.games[0].verdict_state === 'current', 'verdict on the same line reads as current');
    writeIndex(Object.assign({}, entry, { search_digest: 'bbbb' }));
    idx = await (await fetch(base + '/api/review')).json();
    ok(idx.games[0].verdict_state === 'stale', 'a changed search line makes the verdict stale');
    writeIndex(entry);

    for (const kind of ['nope', 'search_better', 'promoted']) {
      const bad = await fetch(base + '/api/verdict', { method: 'POST', body: JSON.stringify({ key: KEY, verdict: kind }) });
      ok(bad.status === 400, `verdict kind '${kind}' is refused by /api/verdict`);
    }

    // ---- promote -----------------------------------------------------------------------------
    dom.window.confirm = () => true;
    promoteReply = () => ({ ok: false, error: 'diverged from the search\'s game at the start of T3 pre_main' });
    doc.querySelector('#vbar button[data-act="promote"]').click();
    ok(!!await until(() => /NOT promoted/.test(doc.getElementById('vstatus').textContent), 'the refusal banner'),
       'a refused promotion is shown as NOT promoted');
    ok(promotes.length === 1 && promotes[0].source === 'handoff_T2.json' && promotes[0].key === KEY,
       'Promote sends the right-hand line (the hand-off at the lost turn)');
    ok(protects.length === 1, 'a refused promotion protects nothing');
    promoteReply = (req) => {
      const v = JSON.parse(fs.readFileSync(verdicts, 'utf8'));
      v.verdicts[KEY] = { verdict: 'promoted', note: req.note, search_digest: 'aaaa', reference: 'references/Deck/claude_s1_gi0.json' };
      fs.writeFileSync(verdicts, JSON.stringify(v));
      return { ok: true, win_turn: 3, reference: 'references/Deck/claude_s1_gi0.json', notes: [],
               files: ['references/review/replaced/Deck/human_s1_gi0.json', 'references/Deck/claude_s1_gi0.json', 'references/review/verdicts.json'] };
    };
    doc.querySelector('#vbar button[data-act="promote"]').click();
    ok(!!await until(() => /Promoted:/.test((doc.getElementById('vstatus') || {}).textContent || ''), 'the promoted banner'),
       'a promotion reports the new reference');
    ok(protects.length === 2 && protects[1].rels.length === 3, 'every file the promotion wrote went through protect()');
    ok(/Promoted/.test((doc.querySelector('.item.sel') || {}).textContent || ''), 'the list shows Promoted');
    const badsrc = await fetch(base + '/api/promote', { method: 'POST', body: JSON.stringify({ key: KEY, source: '../x.json' }) });
    ok(badsrc.status === 400, 'an arbitrary promote source is refused');
    const cl = await (await fetch(base + '/api/verdict', { method: 'POST', body: JSON.stringify({ key: KEY, verdict: null }) })).json();
    ok(cl.ok && !(KEY in JSON.parse(fs.readFileSync(verdicts, 'utf8')).verdicts), 'Clear removes the verdict');
    dom.window.close();
  } catch (e) {
    ok(false, 'exception: ' + (e && e.stack || e));
  } finally {
    server.close();
    fs.rmSync(tmp, { recursive: true, force: true });
    console.log(fails ? `review viewer check: ${fails} FAILURE(S)` : 'review viewer check: all passed');
    process.exit(fails ? 1 : 0);
  }
});
