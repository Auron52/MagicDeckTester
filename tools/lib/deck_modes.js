// THE ONE JS READER of the per-deck modes marker decks/<Name>/<Name>.modes.json (USER 2026-11-26).
// Its Python twin is deck_modes() in scripts/deck_registry.py; keep the two in step.
//
//   {"goldfish": false, "note": "<why>"}
//
// A MISSING file means every mode keeps its default, so a deck without one is unchanged. "goldfish": false
// marks a deck that must not be scored by goldfishing (a lock / control deck such as Enchanted Evening): the
// play viewer (tools/play/server.js listDecks) and the mulligan app's deck list (tools/mulligan/build_deck_list.js)
// do not offer it. The file is meant to grow: a future control-scoring mode will be switched on per deck here.
//
// A marker that does not parse (or is not a JSON object) reads as goldfish DISABLED with an `error` field: a
// typo in a file whose only job is to switch goldfishing off must not quietly switch it back on. A variant /
// archived list (decks/<Name>/<Variant>/) inherits its parent folder's marker.
'use strict';
const fs = require('fs');
const path = require('path');

const MODES_SUFFIX = '.modes.json';

// -> the marker path in directory `dir`, or null: <folder>.modes.json (case-insensitive) first, else the
// folder's ONE *.modes.json.
function modesFileIn(dir) {
  let entries;
  try { entries = fs.readdirSync(dir); } catch (e) { return null; }
  const want = (path.basename(dir) + MODES_SUFFIX).toLowerCase();
  let hit = entries.includes(path.basename(dir) + MODES_SUFFIX) ? path.basename(dir) + MODES_SUFFIX : null;
  if (!hit) {
    const ci = entries.filter(e => e.toLowerCase() === want);
    if (ci.length === 1) hit = ci[0];
  }
  if (!hit) {
    const marks = entries.filter(e => e.toLowerCase().endsWith(MODES_SUFFIX));
    if (marks.length === 1) hit = marks[0];
  }
  return hit ? path.join(dir, hit) : null;
}

// -> the modes object for the deck at `p` (a deck folder, or any file inside one). {} when there is no
// marker. Only folders BELOW a `decks` directory are consulted, never `decks` itself.
function deckModes(p) {
  let dir = path.resolve(p);
  try { if (!fs.statSync(dir).isDirectory()) dir = path.dirname(dir); } catch (e) { dir = path.dirname(dir); }
  const cands = [dir];
  const parent = path.dirname(dir);
  if (path.basename(path.dirname(parent)) === 'decks') cands.push(parent);   // decks/<Name>/<Variant>
  for (const c of cands) {
    const f = modesFileIn(c);
    if (!f) continue;
    try {
      const m = JSON.parse(fs.readFileSync(f, 'utf8'));
      if (!m || typeof m !== 'object' || Array.isArray(m)) throw new Error('not a JSON object');
      return Object.assign({}, m, { file: f });
    } catch (e) {
      return { goldfish: false, error: 'unreadable modes marker ' + f + ': ' + e.message, file: f };
    }
  }
  return {};
}

function goldfishEnabled(p) { return deckModes(p).goldfish !== false; }

module.exports = { MODES_SUFFIX, deckModes, goldfishEnabled };
