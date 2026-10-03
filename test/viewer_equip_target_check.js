#!/usr/bin/env node
// "EQUIP ALL FREE TO X" must target ON THE BOARD, and must be escapable.
// ============================================================================
// USER, 2026-10-03: *"When choosing which creature to equip all to, I don't want a dialog. I want
// targeting on the board so I know what I am equipping."*
//
// WHY THIS IS A GATE AND NOT JUST A CHANGE. The gesture used to open a modal listing host names.
// Two separate things can silently undo the fix:
//   * a future edit re-adds a panel render for S.freeEquipPick, and the dialog is back;
//   * the armed mode loses its exit. That is not hypothetical in this file -- a sub-decision modal
//     once made Undo unclickable and stranded a real game mid-turn (5bde7bf1), and moving this
//     gesture onto the board DELETED its modal's Cancel button, so Escape and the toggle are now
//     the only ways out. An armed mode with no exit is the same trap wearing a different costume.
// So both halves are asserted: the dialog stays gone, and the mode stays escapable.
//
// DOM-ONLY, off a synthetic main-phase frame. It needs no engine run: everything under test is the
// client's own mode handling, and the engine's half (publishing `free_equip_all`) is already
// covered by test/viewer_protocol_check.py and the valve cover check.
//
// NOTE ON WHAT jsdom CANNOT SEE: it does no layout or hit-testing, so this check cannot prove a
// click physically lands -- that is the exact blind spot that let the Undo trap through. The CSS
// stacking invariants are guarded separately by test/viewer_modal_escape_check.js; here we assert
// the DOM wiring (class, badge, handler, state) and say so plainly rather than implying more.
//
// Run:  node test/viewer_equip_target_check.js
'use strict';
const fs = require('fs');
const path = require('path');

let JSDOM;
try { ({ JSDOM } = require('jsdom')); }
catch (e) { console.error('jsdom not installed — add it to the container (npm i -D jsdom in test/).'); process.exit(2); }

const ROOT = path.resolve(__dirname, '..');
// MDT_PLAY_DIR points the check at a PATCHED COPY of tools/play, which is how its own no-power
// risk is settled: a gate that has never been shown to fail is not evidence. Controls run:
//   cp -r tools/play /tmp/playctl
//   # re-add the modal:  else if (S.freeEquipPick) renderFreeEquipPicker();
//   MDT_PLAY_DIR=/tmp/playctl node test/viewer_equip_target_check.js   -> must FAIL
const PLAY = process.env.MDT_PLAY_DIR || path.join(ROOT, 'tools', 'play');

let fails = 0;
function chk(cond, msg) {
  if (cond) { console.log('  ok   ' + msg); }
  else      { console.log('  FAIL ' + msg); fails++; }
}

function buildDom() {
  let html = fs.readFileSync(path.join(PLAY, 'index.html'), 'utf8');
  const lb = fs.readFileSync(path.join(PLAY, 'linebuild.js'), 'utf8');
  html = html.replace('<script src="/linebuild.js"></script>', '<script>\n' + lb + '\n</script>');
  const dom = new JSDOM(html, {
    runScripts: 'dangerously',
    url: 'http://localhost/',
    beforeParse(window) { window.fetch = () => new Promise(() => {}); },
  });
  const win = dom.window;
  const acc = win.document.createElement('script');
  acc.textContent = 'window.__getS = function(){ return S; };';
  win.document.body.appendChild(acc);
  return win;
}

const win = buildDom();
const S = win.__getS();
const doc = win.document;

// Two legal hosts with different counts, plus a creature that is NOT a host and a loose Equipment.
// `free_equip_all` is the engine's own single-action affordance (src/main.cpp), which is the route
// the viewer prefers; host counts are >= FREE_EQUIP_MIN (2) or the button is not offered at all.
function frame(di) {
  S.decision = {
    type: 'main_phase', decision_index: di || 1, turn: 4, phase: 'pre_main',
    me: { hand: [], land_drops_left: 0,
          battlefield: [
            { name: 'Kor Duelist',       num: 10, is_creature: true },
            { name: 'Sram, Senior Edificer', num: 11, is_creature: true },
            { name: 'Puresteel Paladin', num: 12, is_creature: true },   // not a host
            { name: 'Colossus Hammer',   num: 20, is_equip: true },
          ] },
    opponent: { life: 20 },
    plans: [{ index: 0, summary: 'equip', actions: [] }],
    free_equip_all: [ { host: 10, host_name: 'Kor Duelist', pieces: 4 },
                      { host: 11, host_name: 'Sram, Senior Edificer', pieces: 2 } ],
  };
  S.plan = []; S.over = false; S.freeEquipPick = false;
  return S.decision;
}

const hosts = () => Array.from(doc.querySelectorAll('#playfield .thumb.feqhost'))
                          .map(t => +t.dataset.num).sort((a, b) => a - b);
const modalUp = () => {
  const p = doc.getElementById('decpanel');
  return !!(p && /\bshow\b/.test(p.className) && /\bmodal\b/.test(p.className));
};

console.log('"equip all free" targets on the board, and the mode is escapable');

// ---- unarmed: the control is offered, nothing is lit, no modal ------------------------------
frame(1);
win.renderBoard();
chk(!!doc.getElementById('freeeq'), 'the ⚔ Equip all free control does not render with two legal hosts');
chk(hosts().length === 0, `unarmed, ${hosts().length} board thumbs are already lit as hosts (expected 0)`);
chk(!modalUp(), 'a modal is up before the gesture is even armed');

// ---- arming: NO DIALOG, and the legal hosts light up in place -------------------------------
win.toggleFreeEquipMode();
chk(!modalUp(), 'arming the gesture opened a MODAL -- the user asked for board targeting, not a dialog');
chk(JSON.stringify(hosts()) === JSON.stringify([10, 11]),
    `armed, the lit hosts are [${hosts()}] -- expected [10,11] (12 is not a host and must stay dark)`);
const badge10 = doc.querySelector('#playfield .thumb.feqhost[data-num="10"] .feqbadge');
const badge11 = doc.querySelector('#playfield .thumb.feqhost[data-num="11"] .feqbadge');
chk(!!badge10 && /\+4\b/.test(badge10.textContent),
    `the lit host does not carry its piece COUNT (got ${badge10 ? JSON.stringify(badge10.textContent) : 'no badge'}) `
    + '-- the count is how the player knows what they are equipping');
chk(!!badge11 && /\+2\b/.test(badge11.textContent),
    'the second host shows the wrong count, so the badge is not per-host');
chk(/armed/.test((doc.getElementById('freeeq') || {}).className || ''),
    'the armed mode is not reflected on the control, so it reads as a button already pressed');

// ---- the board click lands the gesture and disarms -------------------------------------------
doc.querySelector('#playfield .thumb.feqhost[data-num="10"]').click();
const queued = (S.plan || []).filter(p => p.kind === 'activate' && p.verb === 'equipallfree');
chk(queued.length === 1 && queued[0].hostNum === 10,
    `clicking the lit host queued ${JSON.stringify(queued)} -- expected one equipallfree on host 10`);
chk(S.freeEquipPick === false, 'the mode stayed armed after a successful pick');
chk(hosts().length === 0, 'the board is still lit after the pick, so the mode did not visibly end');

// ---- ESCAPE disarms, and queues nothing (the no-exit trap) -----------------------------------
frame(2);
win.renderBoard();
win.toggleFreeEquipMode();
chk(S.freeEquipPick === true, 'the gesture did not arm on the second frame');
doc.dispatchEvent(new win.KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
chk(S.freeEquipPick === false, 'ESCAPE did not cancel the armed mode -- it has no exit');
chk((S.plan || []).length === 0, 'cancelling the mode queued something anyway');

// ---- pressing the control again cancels too (the modal had a Cancel button; this replaces it) -
win.toggleFreeEquipMode();
chk(S.freeEquipPick === true, 're-arming after a cancel does not work');
win.toggleFreeEquipMode();
chk(S.freeEquipPick === false, 'the control does not toggle the mode off');

// ---- a NEW decision must not inherit an armed mode (an armed dead control) -------------------
win.toggleFreeEquipMode();
chk(S.freeEquipPick === true, 'arming before the new-decision test failed');
frame(3);                                  // frame() mirrors what a new decision resets
chk(S.freeEquipPick === false, 'an armed mode survived into a new decision');

// ---- a click on a non-host is refused rather than mis-attached -------------------------------
frame(4);
win.renderBoard();
win.toggleFreeEquipMode();
win.freeEquipAtHost(12);                   // Puresteel Paladin: on the board, not an offered host
chk((S.plan || []).filter(p => p.verb === 'equipallfree').length === 0,
    'targeting a creature the engine did not offer as a host queued a bundle anyway');
chk(S.freeEquipPick === false, 'a refused pick left the mode armed');

console.log(fails ? `\nFAIL -- ${fails} assertion(s)` : '\nPASS -- board targeting, with an exit');
process.exit(fails ? 1 : 0);
