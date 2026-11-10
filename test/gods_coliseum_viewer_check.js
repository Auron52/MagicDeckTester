#!/usr/bin/env node
'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const server = require('../tools/play/server.js');

// The saved seed-5 position has threshold, a Coliseum in play, and a
// Grave-Troll in the graveyard. The ordinary activation must ask which player
// draws; choosing ourself must then offer the real dredge replacement.
const ref = JSON.parse(fs.readFileSync(path.join(__dirname, '../references/Gods/claude_s5_gi4.json')));
const p = { deck: 'Gods.cod', seed: 5, gameIndex: 4, maxTurns: 8, depth: 0,
            magusUpkeepPrompt: true,
            choices: ref.decisions.slice(0, 16).map(x => x.chosen) };
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
