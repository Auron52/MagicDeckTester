#!/usr/bin/env node
// VIEWER MODAL ESCAPE CHECK -- can the user always get OUT of a sub-decision?
//
//   node test/viewer_modal_escape_check.js          # the gate (milliseconds, non-zero on fail)
//   node test/viewer_modal_escape_check.js -v
//
// WHAT IT GUARDS. A sub-decision panel (#decpanel) cannot be dismissed -- it can only be ANSWERED.
// So Undo is the only exit, and if Undo becomes unreachable while a panel is open the game is
// stranded: no click anywhere can advance or retreat it.
//
// USER 2026-10-02: *"A major viewer headache is that I cannot undo through the discard menu. I
// accidentally pressed the wrong thing and I'm stuck once I hit that menu. The button is
// unclickable with the discard menu up."*
//
// THE BUG IT ENCODES. `#decback` is a full-viewport `position:fixed` backdrop at z-index 24. It used
// to be inset `51px 0 0 0` -- the header's height on ONE row -- so that the header stayed clickable.
// But the header is `flex-wrap:wrap` with eleven children, so at ordinary window widths it wraps to
// a second row, Undo sits below 51px, and the backdrop covers it. The header was statically
// positioned, so it had no stacking context and the fixed backdrop painted over it and ate the
// event: Undo rendered ENABLED and was unreachable.
//
// WHY THIS CHECK IS STATIC, AND WHAT THAT COSTS. The bug is a HIT-TESTING failure. jsdom performs no
// layout and no hit-testing, so `test/viewer_client_check.js` -- which already drives `discard`
// decisions and renders their panel -- structurally cannot see it, and indeed passed throughout.
// Catching it for real needs a headless browser with hit-testing, which this repo does not have. So
// this check asserts the STACKING INVARIANT that makes the trap impossible instead of observing the
// trap: every full-viewport fixed overlay must sit BELOW the header, and the backdrop must not
// encode the header's height as a magic number (which is what silently rotted when it wrapped).
// A static invariant cannot prove the button is clickable; it does make this specific regression,
// and the whole class of "a modal covers the only exit", fail loudly at zero cost.

const fs = require('fs');
const path = require('path');

const ROOT = path.dirname(__dirname);
const FILE = path.join(ROOT, 'tools', 'play', 'index.html');
const VERBOSE = process.argv.includes('-v');

// Overlays that are ALLOWED to out-stack the header, with the reason. #optmenu is the header's own
// gear menu: it is anchored directly under the header, must paint over it, and is dismissible by
// clicking the gear again -- so it cannot strand anything.
const MAY_COVER_HEADER = new Set(['#optmenu']);

function cssOf(html) {
  const out = [];
  const re = /<style[^>]*>([\s\S]*?)<\/style>/gi;
  let m;
  while ((m = re.exec(html))) out.push(m[1]);
  return out.join('\n');
}

// Flatten a declaration block into a { prop: value } map. Good enough for the simple, hand-written
// rules in this file; it is not a CSS parser and does not need to be.
function decls(body) {
  const d = {};
  body.split(';').forEach(part => {
    const i = part.indexOf(':');
    if (i < 0) return;
    d[part.slice(0, i).trim().toLowerCase()] = part.slice(i + 1).trim();
  });
  return d;
}

function rules(css) {
  const out = [];
  // Strip comments first so a commented-out rule cannot be read as live CSS.
  const clean = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const re = /([^{}]+)\{([^{}]*)\}/g;
  let m;
  while ((m = re.exec(clean))) {
    const sel = m[1].trim();
    if (!sel || sel.startsWith('@')) continue;
    out.push({ sel, d: decls(m[2]) });
  }
  return out;
}

// Merge every rule whose selector list mentions `want` as a whole selector.
function styleFor(rs, want) {
  const merged = {};
  for (const r of rs) {
    const hit = r.sel.split(',').map(s => s.trim()).some(s => s === want);
    if (hit) Object.assign(merged, r.d);
  }
  return merged;
}

// Does this rule cover the viewport top-left corner, where the header lives?
function coversHeader(d) {
  if ((d.position || '') !== 'fixed') return false;
  const inset = d.inset;
  if (inset != null) {
    // inset: T R B L (or fewer). A non-zero TOP means it deliberately clears something -- which is
    // exactly the magic-number shape this check exists to reject, so treat it as covering.
    return true;
  }
  return (d.top === '0' || d.top === '0px') && (d.left === '0' || d.left === '0px');
}

function main() {
  const html = fs.readFileSync(FILE, 'utf8');
  const rs = rules(cssOf(html));
  const fails = [];

  const hdr = styleFor(rs, 'header');
  const hz = parseInt(hdr['z-index'], 10);
  if (!hdr.position || hdr.position === 'static') {
    fails.push('`header` has no non-static `position`, so its `z-index` cannot apply and a fixed '
             + 'overlay will paint over Undo (the only exit from a sub-decision)');
  }
  if (!Number.isFinite(hz)) {
    fails.push('`header` declares no `z-index`, so nothing stops a modal backdrop covering Undo');
  }

  const back = styleFor(rs, '#decback');
  if (!back.position) {
    fails.push('#decback not found -- this check is keyed on the sub-decision backdrop; re-point it');
  } else {
    const bz = parseInt(back['z-index'], 10);
    if (Number.isFinite(hz) && Number.isFinite(bz) && !(hz > bz)) {
      fails.push(`#decback z-index ${bz} is not below header z-index ${hz} -- the backdrop can `
               + 'swallow the Undo click and strand an un-dismissable sub-decision');
    }
    const ins = (back.inset || '').trim();
    if (ins && !/^0(px)?$/.test(ins)) {
      fails.push(`#decback uses \`inset: ${ins}\` -- a hardcoded offset encodes the header's height `
               + 'as a magic number, which rots the moment the header wraps to a second row (this is '
               + 'the exact 2026-10-02 regression). Use inset:0 and let the header out-stack it.');
    }
    if (!ins && back.top && !/^0(px)?$/.test(back.top.trim())) {
      fails.push(`#decback uses \`top: ${back.top}\` -- same magic-number problem as inset`);
    }
  }

  // The general class, not just the one element.
  for (const r of rs) {
    const sel = r.sel.split(',')[0].trim();
    if (sel === 'header' || !coversHeader(r.d)) continue;
    const z = parseInt(r.d['z-index'], 10);
    if (!Number.isFinite(z) || !Number.isFinite(hz)) continue;
    if (z > hz && !MAY_COVER_HEADER.has(sel)) {
      fails.push(`full-viewport fixed overlay \`${sel}\` (z-index ${z}) out-stacks the header `
               + `(z-index ${hz}); if it is ever shown while a sub-decision is open, Undo is `
               + 'unreachable. Lower it, or add it to MAY_COVER_HEADER with a reason it cannot strand.');
    }
  }

  if (VERBOSE) {
    console.log('header :', JSON.stringify(hdr));
    console.log('#decback:', JSON.stringify(back));
  }
  console.log('header z-index=%s position=%s ; #decback z-index=%s inset=%s',
              hdr['z-index'], hdr.position, back['z-index'], back.inset || back.top || '(none)');

  if (fails.length) {
    console.log('\nFAIL (%d):', fails.length);
    fails.forEach(f => console.log('  * ' + f));
    return 1;
  }
  console.log('\nPASS -- Undo stays above every full-viewport overlay, so a sub-decision is escapable');
  return 0;
}

process.exit(main());
