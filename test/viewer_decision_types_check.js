#!/usr/bin/env node
// Static check: the play viewer can ANSWER every decision type the engine can emit.
// =====================================================================================
// The viewer gates its central decision panel on a hand-maintained whitelist in
// tools/play/index.html:
//
//     const dec = (!S.over && SUBDECISIONS.includes(d.type)) ? d.type : null;
//
// A decision type missing from that list renders `dec === null`, which hides the panel —
// the board sits there with no dialog and no way to reply, and the game is stuck. This
// has now shipped three times (lackey_put + echo in 45985fe, land_entry in fc3d1c61): in
// each case the panel HTML, the wiring, and the commit handler were all written, and only
// the whitelist line was forgotten, so the feature was dead on arrival with no error.
//
// Nothing else catches it: the engine-side checks (viewer_protocol_check.py,
// viewer_validate_check.js) never load index.html, and viewer_client_check.js only drives
// decks/seeds that happen to reach a given decision — a type nobody plays into stays dead.
//
// This check is static (no binary, no jsdom, milliseconds): it extracts every decision type
// the engine can write from src/main.cpp and asserts the viewer whitelists each one.
//
// Run:  node test/viewer_decision_types_check.js  [--verbose]
// Exit: 0 = every engine decision type is answerable; 1 = a type is unreachable; 2 = setup error.

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const MAIN_CPP = path.join(ROOT, 'src', 'main.cpp');
const INDEX_HTML = path.join(ROOT, 'tools', 'play', 'index.html');
const VERBOSE = process.argv.includes('--verbose');

function fail(msg) { console.error(msg); process.exit(2); }

let cpp, html;
try { cpp = fs.readFileSync(MAIN_CPP, 'utf8'); } catch (e) { fail(`cannot read ${MAIN_CPP}: ${e.message}`); }
try { html = fs.readFileSync(INDEX_HTML, 'utf8'); } catch (e) { fail(`cannot read ${INDEX_HTML}: ${e.message}`); }

// ---- engine side: every string a `.Type(...)` call can write ------------------------
// Handles the three shapes in main.cpp: a literal (`.Type("dragon")`), a ternary
// (`.Type(sacrifice ? "sacrifice" : "bounce")`), and a variable (`.Type(kindstr)`, whose
// candidates come from that variable's own declaration — the scry/surveil/reorder trio).
const engineTypes = new Set();
const lines = cpp.split('\n');
const literalsIn = (s) => (s.match(/"([a-z_]+)"/g) || []).map(q => q.slice(1, -1));

lines.forEach((line, i) => {
  const at = line.indexOf('.Type(');
  if (at < 0) return;
  const arg = line.slice(at + 6);
  const lits = literalsIn(arg.slice(0, arg.indexOf(')') >= 0 ? arg.indexOf(')') + 1 : arg.length));
  if (lits.length) { lits.forEach(t => engineTypes.add(t)); return; }
  // `.Type(<identifier>)` — resolve the identifier from its declaration elsewhere in the file.
  const ident = (arg.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)/) || [])[1];
  if (!ident) { fail(`main.cpp:${i + 1}: cannot resolve .Type() argument: ${line.trim()}`); }
  const decl = lines.find(l => new RegExp(`\\b${ident}\\s*=`).test(l) && /"/.test(l));
  if (!decl) { fail(`main.cpp:${i + 1}: .Type(${ident}) — no declaration with string literals found`); }
  const declLits = literalsIn(decl);
  if (!declLits.length) { fail(`main.cpp:${i + 1}: .Type(${ident}) — declaration has no type literals`); }
  declLits.forEach(t => engineTypes.add(t));
});

if (engineTypes.size < 5) fail(`only ${engineTypes.size} decision types found in main.cpp — extraction is broken`);

// ---- viewer side: the whitelist ----------------------------------------------------
const m = html.match(/const SUBDECISIONS = \[([^\]]*)\]/);
if (!m) fail('cannot find `const SUBDECISIONS = [...]` in tools/play/index.html');
const whitelist = m[1].split(',').map(s => s.trim().replace(/^'|'$/g, '')).filter(Boolean);
const listed = new Set(whitelist);

// main_phase is the full board interaction (isMain), not a panel overlay — it is answered
// by the line builder, so it is deliberately NOT in SUBDECISIONS.
const expected = [...engineTypes].filter(t => t !== 'main_phase').sort();
const missing = expected.filter(t => !listed.has(t));
// A whitelisted type that the engine never emits is dead weight (a rename left behind), and a
// duplicate entry means someone edited the line twice — both are worth surfacing, neither fails.
const stale = whitelist.filter(t => !engineTypes.has(t));
const dupes = whitelist.filter((t, i) => whitelist.indexOf(t) !== i);

if (VERBOSE) {
  console.log(`engine emits (${expected.length}): ${expected.join(' ')}`);
  console.log(`viewer lists (${whitelist.length}): ${whitelist.join(' ')}`);
}
if (stale.length) console.log(`note: whitelisted but never emitted by main.cpp: ${stale.join(', ')}`);
if (dupes.length) console.log(`note: duplicate SUBDECISIONS entries: ${dupes.join(', ')}`);

if (missing.length) {
  console.error(`FAIL: ${missing.length} engine decision type(s) missing from SUBDECISIONS in ` +
                `tools/play/index.html — the viewer hides the panel and the game stalls unanswerable:`);
  missing.forEach(t => {
    // Point at the panel that already exists but is unreachable, when there is one.
    const hasPanel = new RegExp(`dec\\s*===\\s*'${t}'`).test(html);
    console.error(`  - ${t}${hasPanel ? "  (its panel + wiring EXIST in index.html — just add it to the list)" : ''}`);
  });
  process.exit(1);
}

console.log(`OK: all ${expected.length} engine decision types are answerable by the viewer.`);
process.exit(0);
