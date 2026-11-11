#!/usr/bin/env node
'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const server = require('../tools/play/server.js');

// The saved seed-5 position has threshold, a Coliseum in play, and a
// Grave-Troll in the graveyard. The ordinary activation must ask which player
// draws; choosing ourself must then offer the real dredge replacement.
//
// The game is references/Gods/claude_s5_gi4.json up to its T3 Coliseum line (decision 16). Its
// recorded `chosen` values are NOT replayed raw: the viewer played T1-T3 as line-built
// ("**" whole-sequence) casts, which only reproduce with the reference's --cast-order side
// channel, and the T2 frame's recorded plan index (3) is stale under the current enumeration
// (the pinned Coliseum + Magus line is index 0). The stream below is the keep (1) followed by
// `viewer_protocol_check.py --emit-resolved references/Gods/claude_s5_gi4.json`'s content-resolved
// picks up to that frame's prefix_len (15). If the enumeration moves again, regenerate it the
// same way rather than copying the reference's raw indices.
const ref = JSON.parse(fs.readFileSync(path.join(__dirname, '../references/Gods/claude_s5_gi4.json')));
const castOrder = {};
for (const de of ref.decisions.slice(0, 16)) {
  const d = de.decision || {};
  if (d.type === 'main_phase' && Array.isArray(de.cast_order) && de.cast_order.length
      && Number.isInteger(d.main_ordinal) && d.main_ordinal >= 0) {
    castOrder[d.main_ordinal] = de.cast_order;
  }
}
const p = { deck: 'Gods.cod', seed: 5, gameIndex: 4, maxTurns: 8, depth: 0,
            magusUpkeepPrompt: true, castOrder,
            choices: [1, 0, 5, 0, -1, -1, 0, -1, -1, 0, 5, 4, 2, 2, 4, 3] };
const v = server.runValidate(p, 'act=Cephalid Coliseum');
assert.equal(v.kind, 'validation');
assert.equal(v.verdict, 'choose');
const self = v.variants.find(x => x.subs.some(s => s.choice === 'you draw'));
const opp = v.variants.find(x => x.subs.some(s => s.choice === 'opponent draws'));
assert(self && opp && self.plan_index !== opp.plan_index);

const next = server.runStep({ ...p, choices: [...p.choices, self.plan_index] });
assert.equal(next.kind, 'decision');
assert.equal(next.decision.type, 'dredge');
assert(next.decision.options.some(x => x.name === 'Golgari Grave-Troll'));
console.log('Gods Coliseum target choice and self-draw dredge: OK');
