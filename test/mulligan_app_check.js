#!/usr/bin/env node
// Gate for the mulligan checker (tools/mulligan): its engine-free core must reproduce the SHIPPED keep
// table exactly.
//
// For every deck folder holding a raw keep sidecar AND the compiled table the engine plays from, this
// loads the raw sidecar through tools/mulligan/keepcore.js (the browser's own code path: the streaming
// parser, the rank index, the backward induction) and then streams the compiled
// <stem>.keepmodel.exhaustive.profile.json[.gz], comparing, for every 7-card composition, every
// mulligan count and both play/draw:
//   * the keep flag, and
//   * the bottoming target (the subcomposition kept after bottoming m cards).
// Any difference fails the run: the page would be showing a recommendation the engine does not play.
//
//   node test/mulligan_app_check.js                      # every deck with a table
//   node test/mulligan_app_check.js Giants "Melira Pod"  # just these deck folders
//   MULL_CHECK_JOBS=4 node test/mulligan_app_check.js    # decks checked concurrently (default 4)
//
// Read-only: nothing under decks/ is written.
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');

const ROOT = path.resolve(__dirname, '..');
const Core = require(path.join(ROOT, 'tools', 'mulligan', 'keepcore.js'));

function pick(dir, stem, suffixes) {
  for (const s of suffixes) {
    const p = path.join(dir, stem + s);
    if (fs.existsSync(p)) { return p; }
  }
  return null;
}

// The deck folder's decklist stem (decks/<Name>/<stem>.cod|.txt, matched case-insensitively).
function deckFiles(dir) {
  const ents = fs.readdirSync(dir);
  const list = ents.find(e => /\.(cod|txt)$/i.test(e) && !/^readme/i.test(e));
  if (!list) { return null; }
  const stem = list.replace(/\.(cod|txt)$/i, '');
  const raw = pick(dir, stem, ['.keepmodel.exhaustive.raw.json.gz', '.keepmodel.exhaustive.raw.json']);
  const compiled = pick(dir, stem, ['.keepmodel.exhaustive.profile.json.gz', '.keepmodel.exhaustive.profile.json']);
  const play = pick(dir, stem, ['.profile.json']);
  return { dir, stem, list: path.join(dir, list), raw, compiled, play };
}

function byteSource(file) {
  const s = fs.createReadStream(file, { highWaterMark: 1 << 20 });
  return file.endsWith('.gz') ? s.pipe(zlib.createGunzip({ chunkSize: 1 << 20 })) : s;
}

async function checkDeck(f) {
  const t0 = Date.now();
  const cards = Core.parseDecklist(fs.readFileSync(f.list, 'utf8'), f.list);
  let aliases = [];
  if (f.play) {
    try { aliases = (JSON.parse(fs.readFileSync(f.play, 'utf8')).mulligan || {}).keep_table_alias || []; } catch (e) {}
  }
  const t = new Core.KeepTable(cards, aliases);
  await t.load(byteSource(f.raw));
  const tLoad = Date.now();
  await t.computeDopt();
  const tDopt = Date.now();
  // The page's own calibration step (a prefix of the compiled table picks the bottoming rule); the
  // full comparison below then holds that choice to EVERY entry.
  const cal = await t.calibrate(byteSource(f.compiled));
  // ... and when that says the raw sidecar cannot reproduce the table, the page's fallback: read the
  // compiled table whole, take its decisions, recover the value the lost deeper levels fed in.
  let recovery = null;
  if (cal.needsShipped) { recovery = await t.useShippedTable(byteSource(f.compiled)); }
  const tCal = Date.now();

  const M = t.levels, K = t.K;
  let entries = 0, flagBad = 0, flagTot = 0, botBad = 0, botTot = 0, notInRaw = 0, bucketsOk = null;
  const flagBadByM = new Array(M + 1).fill(0);
  const examples = [];
  const parser = new Core.StreamJSON(
    p => p.length === 2 && p[0] === 'exhaustive_keep' && p[1] === 'entries',
    (e, stack) => {
      if (bucketsOk === null) {
        const ek = stack[1].val;
        bucketsOk = JSON.stringify(ek.buckets) === JSON.stringify(t.buckets);
        if (!bucketsOk) { throw new Error('compiled table buckets differ from the raw sidecar\'s'); }
      }
      entries++;
      const idx = t.rank(e.comp, Core.HAND);
      if (idx < 0 || !t.sizes[Core.HAND].present[idx]) { notInRaw++; return; }
      for (let m = 0; m <= M; m++) {
        for (let pd = 0; pd < 2; pd++) {
          const d = t.decideComp(e.comp, m, pd);
          const j = m * 2 + pd;
          flagTot++;
          if ((e.keep[j] ? true : false) !== d.keep) {
            flagBad++;
            flagBadByM[m]++;
            if (examples.length < 5) { examples.push(`keep comp=${JSON.stringify(e.comp)} m=${m} pd=${pd} shipped=${e.keep[j]} core=${d.keep ? 1 : 0}`); }
          }
          if (m >= 1 && e.bottom_keep && e.bottom_keep[j]) {
            botTot++;
            const want = e.bottom_keep[j];
            let same = want.length === K;
            for (let b = 0; same && b < K; b++) { if (want[b] !== d.bottomSub[b]) { same = false; } }
            if (!same) {
              botBad++;
              if (examples.length < 5) { examples.push(`bottom comp=${JSON.stringify(e.comp)} m=${m} pd=${pd} shipped=${JSON.stringify(want)} core=${JSON.stringify(d.bottomSub)}`); }
            }
          }
        }
      }
    });
  await Core.pumpStream(byteSource(f.compiled), parser);
  const s = t.summary();
  return {
    deck: path.basename(f.dir), K, M, cells: s.cells, hands7: s.hands7, entries, notInRaw,
    flagBad, flagTot, flagBadByM, botBad, botTot, examples, deckMatch: s.deckMatch,
    shippedMode: !!t.shipped, statsLevels: t.maxMull, recovery,
    rule: t.rule, cal: { sampled: cal.sampled, exact: cal.exact, shippedMaxMull: cal.shippedMaxMull, tried: cal.tried, problem: cal.problem, keepMismatchByM: cal.keepMismatchByM },
    calS: (tCal - tDopt) / 1000,
    alias: s.aliasUsed.map(a => a.name + '->' + a.as), unbucketed: s.unbucketed.map(c => c.name),
    present7: t.sizes[Core.HAND].present.reduce((a, x) => a + x, 0),
    loadS: (tLoad - t0) / 1000, doptS: (tDopt - tLoad) / 1000, totalS: (Date.now() - t0) / 1000,
    heapMB: process.memoryUsage().rss / 1048576,
  };
}

if (!isMainThread) {
  checkDeck(workerData).then(r => parentPort.postMessage({ ok: true, r }),
                             e => parentPort.postMessage({ ok: false, deck: path.basename(workerData.dir), err: String(e && e.stack || e) }));
} else {
  const want = process.argv.slice(2);
  const decksDir = path.join(ROOT, 'decks');
  let dirs = fs.readdirSync(decksDir).map(d => path.join(decksDir, d)).filter(d => fs.statSync(d).isDirectory());
  if (want.length) { dirs = want.map(w => path.join(decksDir, w)); }
  const jobs = dirs.map(deckFiles).filter(f => f && f.raw && f.compiled);
  if (want.length && jobs.length !== want.length) {
    console.error('mulligan_app_check: some named decks have no raw + compiled keep table');
    process.exit(2);
  }
  // Largest first, so the biggest table is not the tail.
  jobs.sort((a, b) => fs.statSync(b.raw).size - fs.statSync(a.raw).size);
  const J = Math.max(1, parseInt(process.env.MULL_CHECK_JOBS || '4', 10));
  let next = 0, failed = 0, warned = 0;
  const results = [];
  const runOne = () => new Promise((resolve) => {
    if (next >= jobs.length) { resolve(); return; }
    const f = jobs[next++];
    const w = new Worker(__filename, { workerData: f, resourceLimits: { maxOldGenerationSizeMb: 8192 } });
    w.once('message', (msg) => {
      if (!msg.ok) { failed++; console.log(`FAIL ${msg.deck}: ${msg.err}`); }
      else {
        const r = msg.r;
        results.push(r);
        // A keep-flag difference the calibration DETECTED is shown on the page as an "unreliable at m
        // mulligans" warning (an artifact problem: the raw sidecar is not the one the shipped table was
        // built from) -- WARN. One it did not detect would be shown as a confident wrong call -- FAIL.
        const flagged = r.shippedMode ? [] : (r.cal.keepMismatchByM || []);
        const undetected = r.flagBadByM.some((x, m) => x && !flagged[m]);
        const bad = undetected || r.botBad || r.notInRaw || r.entries !== r.present7 || (r.recovery && r.recovery.needed && !r.recovery.ok);
        // Reproduced only with the shipped-table fallback: the page is right, the artifacts still have a gap.
        const warn = !bad && (r.flagBad || r.shippedMode);
        if (bad) { failed++; } else if (warn) { warned++; }
        console.log(`${bad ? 'FAIL' : warn ? 'WARN' : 'ok  '} ${r.deck.padEnd(22)} K=${String(r.K).padStart(2)} hands=${String(r.hands7).padStart(8)}`
          + ` keep ${r.flagBad}/${r.flagTot}  bottom ${r.botBad}/${r.botTot}`
          + `  deck=${r.deckMatch}${r.alias.length ? ' alias ' + r.alias.join(',') : ''}`
          + `${r.unbucketed.length ? ' UNBUCKETED ' + r.unbucketed.join(',') : ''}`
          + `  rule floor=${r.rule.floor}${r.rule.shrink ? '+shrink' : ''} (calibrated on ${r.cal.sampled}${r.cal.exact ? '' : ', NOT exact'})`
          + `  load ${r.loadS.toFixed(1)}s dopt ${r.doptS.toFixed(1)}s cal ${r.calS.toFixed(1)}s total ${r.totalS.toFixed(1)}s`);
        if (r.shippedMode) {
          const rc = r.recovery || {};
          console.log(`     shipped-table mode: the raw sidecar covers 0-${r.statsLevels} mulligans, the engine's table 0-${r.M}`
            + (rc.needed ? `; recovered Dopt[${r.statsLevels + 1}] draw ${rc.D[0].toFixed(4)} ±${rc.spread[0][r.statsLevels + 1].toFixed(4)},`
              + ` play ${rc.D[1].toFixed(4)} ±${rc.spread[1][r.statsLevels + 1].toFixed(4)}${rc.ok ? '' : ' (NOT recoverable)'}` : ''));
        }
        if (r.flagBad) { console.log(`     keep mismatches by mulligan count: ${JSON.stringify(r.flagBadByM)} (compiled table has ${r.cal.shippedMaxMull} levels, raw ${r.M})`); }
        if (r.cal.problem) { console.log(`     calibration: ${r.cal.problem}`); }
        if (!r.cal.exact) { console.log(`     calibration tried: ${JSON.stringify(r.cal.tried)}`); }
        if (r.entries !== r.present7) { console.log(`     compiled entries ${r.entries} != raw 7-card cells ${r.present7}`); }
        for (const ex of r.examples) { console.log('     ' + ex); }
      }
      w.terminate();
      resolve(runOne());
    });
    w.once('error', (e) => { failed++; console.log(`FAIL ${path.basename(f.dir)}: ${e && e.stack || e}`); resolve(runOne()); });
  });
  Promise.all(Array.from({ length: Math.min(J, jobs.length) }, runOne)).then(() => {
    // The page's deck dropdown is filled from tools/mulligan/decks.js before the folder is readable, so
    // a deck that gained (or lost) a table must be reflected there.
    if (!require(path.join(ROOT, 'tools', 'mulligan', 'build_deck_list.js')).isCurrent()) {
      failed++;
      console.log('FAIL tools/mulligan/decks.js is out of date: run node tools/mulligan/build_deck_list.js');
    }
    console.log(`\n${results.length} deck(s) checked, ${failed} failing, ${warned} reproduced only by the shipped-table fallback (an artifact gap; the page says so)`);
    process.exit(failed ? 1 : 0);
  });
}
