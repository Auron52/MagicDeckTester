#!/usr/bin/env node
// Pins THE reveal invariant (user, 2026-08-03): "what we would show in the viewer history should
// show in the log."
//
// This regressed twice, in both possible directions, which is why it is now a gate:
//   1. A reveal reached the saved game log but NOT the viewer -- every site guarded on
//      `g_reveal_logger`, and --claude-play attaches no GameLogger, so a Muxus put/bottom split
//      showed as nothing at all in the one mode a human is watching. Fixed by RevealVisible().
//   2. A reveal reached the viewer but NOT the log -- a `viewer_only` flag and a second
//      `viewer_labels` channel existed to dodge the play-digest rebaseline cost of logging
//      something new. That made the saved log strictly less informative than the screen: a Goblin
//      Lackey's cheat-into-play choices had to be reverse-engineered from per-phase board deltas
//      during an A/B. Fixed by deleting both and paying the rebaseline.
//
// Static (milliseconds, no binary, no node deps). It cannot prove runtime parity, but it forbids
// the two structural escape hatches that produced both bugs, which is what actually failed.
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const read = (p) => fs.readFileSync(path.join(ROOT, p), 'utf8');
const fail = [];

// --- 1. no viewer-only escape hatch may exist anywhere in the engine ------------------------
const SRC = ['src/core/GameLogger.h', 'src/core/GameLogger.cpp', 'src/core/SpellEffects.h',
             'src/core/SpellEffects.cpp', 'src/core/EffectHandler.cpp', 'src/main.cpp'];
for (const f of SRC) {
  const txt = read(f);
  txt.split('\n').forEach((line, i) => {
    if (/^\s*(\/\/|\*)/.test(line)) return;             // prose may discuss the old design
    if (/\bviewer_only\b|\bviewer_labels\b/.test(line)) {
      fail.push(`${f}:${i + 1}: reintroduces a viewer-only reveal channel -- a reveal the viewer `
                + `shows must also reach the game log:\n      ${line.trim()}`);
    }
  });
}

// --- 2. EmitReveal must keep exactly ONE disposition channel and no bool flag ----------------
const gl = read('src/core/GameLogger.h');
const sig = gl.match(/void EmitReveal\(([\s\S]*?)\);/);
if (!sig) {
  fail.push('src/core/GameLogger.h: EmitReveal declaration not found (renamed? update this check)');
} else {
  const params = sig[1];
  const dispositionParams = (params.match(/std::vector<std::string>&\s*\w+/g) || [])
      .filter((p) => !/looked_at_names/.test(p));
  if (dispositionParams.length !== 1) {
    fail.push(`src/core/GameLogger.h: EmitReveal has ${dispositionParams.length} disposition `
              + `channels, expected exactly 1 (both sinks must read the same one): `
              + dispositionParams.join(', '));
  }
  if (/\bbool\b/.test(params)) {
    fail.push('src/core/GameLogger.h: EmitReveal grew a bool parameter -- if that is a sink '
              + 'selector it breaks log/viewer parity');
  }
}

// --- 3. the shared label derivation must exist and be used by the log renderer ---------------
if (!/std::string RevealDisposition\(/.test(gl)) {
  fail.push('src/core/GameLogger.h: RevealDisposition() is gone -- the viewer and the log renderer '
            + 'would each derive "kept"/"to the bottom" separately and could drift');
}
const renderer = read('scripts/render_game_log.py');
if (!/lookedAt/.test(renderer) || !/to the bottom/.test(renderer)) {
  fail.push('scripts/render_game_log.py: no longer renders REVEAL dispositions -- the saved log '
            + 'stops reading like the viewer history');
}

if (fail.length) {
  console.error('reveal-log parity CONTRACT BROKEN:\n  - ' + fail.join('\n  - '));
  process.exit(1);
}
console.log('reveal/log parity: OK (single disposition channel, no viewer-only reveals, renderer intact)');
