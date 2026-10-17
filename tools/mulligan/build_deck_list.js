#!/usr/bin/env node
// Writes tools/mulligan/decks.js: the decks under decks/ that have a mulligan table. A page opened from
// disk cannot read the repo's folders until the user allows it, so this list is what fills the deck
// dropdown the moment the page opens. Run it after a deck gains (or loses) a keep table;
// test/mulligan_app_check.js fails while decks.js is out of date.
//
//   node tools/mulligan/build_deck_list.js           # rewrite decks.js
//   node tools/mulligan/build_deck_list.js --check   # exit 1 if decks.js is out of date
'use strict';
const fs = require('fs');
const path = require('path');
const Core = require('./keepcore.js');

const ROOT = path.resolve(__dirname, '..', '..');
const OUT = path.join(__dirname, 'decks.js');

// The page's folder walk (index.html walkHandle), on the filesystem: two levels below decks/.
function walk(dir, rel, depth, out) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (e.isFile()) { out.push({ path: rel + '/' + e.name, name: e.name }); }
    else if (e.isDirectory() && depth < 2 && !Core.SKIP_DIRS.has(e.name)) { walk(path.join(dir, e.name), rel + '/' + e.name, depth + 1, out); }
  }
}

function render() {
  const entries = [];
  walk(path.join(ROOT, 'decks'), 'decks', 0, entries);
  const decks = Core.discoverDecks(entries).map(d => d.archived ? { key: d.key, label: d.label, archived: true }
                                                                 : { key: d.key, label: d.label });
  return '// The decks under decks/ that have a mulligan table, so tools/mulligan/index.html can list them\n'
    + '// before the browser has been allowed to read the folder. GENERATED: node tools/mulligan/build_deck_list.js\n'
    + 'window.MDT_DECKS = [\n' + decks.map(d => '  ' + JSON.stringify(d)).join(',\n') + '\n];\n';
}

function isCurrent() {
  try { return fs.readFileSync(OUT, 'utf8') === render(); } catch (e) { return false; }
}

module.exports = { render, isCurrent, OUT };

if (require.main === module) {
  if (process.argv.includes('--check')) {
    if (isCurrent()) { process.exit(0); }
    console.error('tools/mulligan/decks.js is out of date: run node tools/mulligan/build_deck_list.js');
    process.exit(1);
  }
  fs.writeFileSync(OUT, render());
  console.log('wrote ' + path.relative(ROOT, OUT));
}
