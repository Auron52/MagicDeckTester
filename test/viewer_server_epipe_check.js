#!/usr/bin/env node
// A child can close after emitting a prompt. The next interactive write must
// fall back to a stateless replay instead of killing the viewer with EPIPE.
const assert = require('node:assert/strict');
const server = require('../tools/play/server.js');

async function main() {
  const prefix = [1, 1, -1, -1, 1, -1, -1, 0, -1, -1, -1, 0];
  const params = {
    deck: 'Merfolk Turns.cod', seed: 12, gameIndex: 0, maxTurns: 8, depth: 0,
    choices: prefix,
  };
  const city = await server.runStepCached(params, null);
  assert.equal(city.kind, 'decision');
  assert.equal(city.decision.type, 'choose_creature_type');
  assert.equal(city.decision.source, 'Three Tree City');
  const merfolkIndex = city.decision.heuristic_default;
  assert.equal(city.decision.options.find((o) => o.index === merfolkIndex)?.name, 'Merfolk');

  const next = await server.runStepCached({ ...params, choices: [...prefix, 0] }, null);
  assert.equal(next.kind, 'decision');
  assert.equal(next.decision.type, 'main_phase');
  const merfolk = await server.runStepCached({ ...params, choices: [...prefix, merfolkIndex] }, null);
  assert.equal(merfolk.kind, 'decision');
  assert.equal(merfolk.decision.type, 'champion');
  await new Promise((resolve) => setTimeout(resolve, 50));
  process.stdout.write('viewer survives the seed 12 City follow-up\n');
  process.exit(0);
}

main().catch((error) => { console.error(error); process.exitCode = 1; });
