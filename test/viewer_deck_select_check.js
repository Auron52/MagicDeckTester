#!/usr/bin/env node
// Headless jsdom check for the viewer's TWO-CONTROL deck selection (tools/play/index.html).
// =====================================================================================
// A deck's archived lists live at decks/<Deck>/<Version>/<Deck>.cod and are selectable, so they used
// to appear as extra entries in the single Deck dropdown -- one more line per archived version,
// forever, with the shipping list no longer the obvious pick. USER 2026-09-04: "the one thing I
// don't want is them to all be included in the one huge dropdown." They now live in their own
// Version select that appears only when a deck actually has more than one list.
//
// This pins the part that is easy to break silently and impossible to see from the server side: the
// deck list must stay one-row-per-deck, and the (deck, version) pair the rest of the client sends on
// EVERY request -- selDeck() -> {deck, version} -- must still resolve to the right archived list.
// Getting that wrong does not throw; it plays the shipping list while the UI says otherwise, which
// is exactly the bug the entry-id keying was introduced to fix in the first place.
//
// Deck-agnostic: it discovers whichever deck has variants from the live /api/decks payload and
// skips cleanly if none does. No engine binary needed -- only server.js's listDecks().
//
// Run:  node test/viewer_deck_select_check.js          (needs jsdom)
'use strict';
const fs = require('fs');
const path = require('path');

let JSDOM;
try { ({ JSDOM } = require('jsdom')); }
catch (e) { console.log('SKIP: jsdom not installed (npm i jsdom)'); process.exit(0); }

const ROOT = path.join(__dirname, '..');
const PLAY = path.join(ROOT, 'tools', 'play');

// server.js exports nothing, so reuse its listDecks by loading the module in a sandbox would be
// fragile; instead shell out to the same directory scan it performs. Keeping this independent means
// the check fails loudly if the two ever disagree about what a variant is.
function listDecksLikeServer() {
  const DECKS = path.join(ROOT, 'decks');
  const out = [];
  for (const name of fs.readdirSync(DECKS)) {
    const dir = path.join(DECKS, name);
    if (!fs.statSync(dir).isDirectory()) continue;
    let deckFile = null;
    for (const ext of ['.cod', '.txt']) {
      if (fs.existsSync(path.join(dir, name + ext))) { deckFile = name + ext; break; }
    }
    if (!deckFile) continue;
    const hasProfile = fs.existsSync(path.join(dir, name + '.profile.json'));
    out.push({ id: deckFile, deck: deckFile, name, version: null, label: name, hasProfile, tier: 'stable' });
    for (const sub of fs.readdirSync(dir)) {
      const subdir = path.join(dir, sub);
      if (!fs.statSync(subdir).isDirectory()) continue;
      let vFile = null;
      for (const ext of ['.cod', '.txt']) {
        if (fs.existsSync(path.join(subdir, name + ext))) { vFile = name + ext; break; }
      }
      if (!vFile) continue;
      out.push({ id: vFile + '@' + sub, deck: vFile, name, version: sub,
                 label: name + '  —  ' + sub,
                 hasProfile: fs.existsSync(path.join(subdir, name + '.profile.json')), tier: 'stable' });
    }
  }
  out.sort((a, b) => (b.hasProfile - a.hasProfile) || (!!a.version - !!b.version)
                     || String(a.label).localeCompare(String(b.label)));
  return out;
}

const DECKS = listDecksLikeServer();

function buildDom() {
  let html = fs.readFileSync(path.join(PLAY, 'index.html'), 'utf8');
  const lb = fs.readFileSync(path.join(PLAY, 'linebuild.js'), 'utf8');
  html = html.replace('<script src="/linebuild.js"></script>', '<script>\n' + lb + '\n</script>');
  const dom = new JSDOM(html, {
    runScripts: 'dangerously',
    url: 'http://localhost/',
    beforeParse(window) {
      window.fetch = async (u) => {
        if (String(u).includes('/api/decks')) {
          return { json: async () => ({ decks: DECKS, binExists: true }) };
        }
        // checkReference() fires on every selection change; answer it inertly.
        return { json: async () => ({ exists: false }) };
      };
    },
  });
  const acc = dom.window.document.createElement('script');
  // selDeck / selEntryId are top-level lexical bindings, not window properties.
  acc.textContent = 'window.__selDeck = () => selDeck(); window.__selEntryId = () => selEntryId();';
  dom.window.document.body.appendChild(acc);
  return dom.window;
}

let fails = 0;
const ok  = (c, m) => { console.log((c ? '  PASS  ' : '  FAIL  ') + m); if (!c) fails++; };

(async () => {
  const win = buildDom();
  await win.loadDecks();
  const deckSel = win.document.getElementById('deck');
  const verSel  = win.document.getElementById('version');
  const wrap    = win.document.getElementById('versionwrap');

  const names = [...new Set(DECKS.map(d => d.name))];
  console.log(`--- viewer deck select: ${DECKS.length} entries over ${names.length} decks ---`);

  // 1. ONE ROW PER DECK. This is the whole point of the change.
  ok(deckSel.options.length === names.length,
     `deck dropdown has one row per deck (${deckSel.options.length} rows, ${names.length} decks, `
     + `${DECKS.length} total lists)`);
  const emdash = [...deckSel.options].filter(o => o.textContent.includes('—'));
  ok(emdash.length === 0,
     `no archived list leaked into the deck dropdown (found ${emdash.length})`);

  const withVariants = names.filter(n => DECKS.filter(d => d.name === n).length > 1);
  if (!withVariants.length) {
    console.log('  SKIP  no deck currently has an archived list -- nothing further to check');
    process.exit(fails ? 1 : 0);
  }

  // 2. A deck WITHOUT variants hides the Version control entirely.
  const plain = names.find(n => DECKS.filter(d => d.name === n).length === 1);
  if (plain) {
    deckSel.value = plain; deckSel.dispatchEvent(new win.Event('change'));
    ok(wrap.style.display === 'none', `Version control hidden for a single-list deck (${plain})`);
  }

  // 3. A deck WITH variants shows it, with one row per list, and resolves each correctly.
  for (const name of withVariants) {
    const lists = DECKS.filter(d => d.name === name);
    deckSel.value = name; deckSel.dispatchEvent(new win.Event('change'));
    ok(wrap.style.display !== 'none', `Version control shown for ${name}`);
    ok(verSel.options.length === lists.length,
       `${name}: Version has one row per list (${verSel.options.length} of ${lists.length})`);
    ok([...verSel.options].some(o => o.textContent.startsWith('Current')),
       `${name}: the shipping list is offered as "Current"`);

    // Default selection must be the SHIPPING list, not an archived one.
    ok((win.__selDeck().version || null) === null,
       `${name}: defaults to the shipping list (version=${win.__selDeck().version})`);

    // Every archived list must round-trip through selDeck() to its own version folder.
    for (const v of lists.filter(d => d.version)) {
      verSel.value = v.id; verSel.dispatchEvent(new win.Event('change'));
      const sel = win.__selDeck();
      ok(sel.version === v.version && sel.deck === v.deck,
         `${name}: "${v.version}" resolves to {deck:${sel.deck}, version:${sel.version}}`);
    }

    // Re-picking the SAME deck must not silently snap back to the shipping list.
    const last = lists.filter(d => d.version).slice(-1)[0];
    if (last) {
      verSel.value = last.id; verSel.dispatchEvent(new win.Event('change'));
      deckSel.value = name; deckSel.dispatchEvent(new win.Event('change'));
      ok(win.__selDeck().version === last.version,
         `${name}: re-selecting the deck keeps the chosen version (${win.__selDeck().version})`);
    }
  }

  console.log(fails ? `\nFAILED (${fails})` : '\nALL PASS');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
