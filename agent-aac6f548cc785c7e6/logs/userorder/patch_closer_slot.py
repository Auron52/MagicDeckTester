h='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/src/ai/HeuristicArm.h'
lines=open(h).read().split('\n')
for i,l in enumerate(lines):
    if l.startswith('    DRAGONSTORM_IRENCRAG_CLOSER,'):
        lines[i]=('    DRAGONSTORM_IRENCRAG_CLOSER, // MTG_DRAGONSTORM_IRENCRAG_CLOSER (USER order, rule 7) Irencrag Feat needs a CLOSER (Dragonstorm or a Dragon) after it: a Dragonstorm subset holding the restrictor, no closer and no Apex is refused, and the post-Apex merge does not cast a LEFTOVER Irencrag that no closer follows and no Apex precedes (it stays in hand) -- dragonstorm d0 s7000035 T5 cast it alone after an empty continuation. A subset WITH an Apex stays admitted (its exile may bring the closer: s8500074). Shared reader DragonstormIrencragCloserOn (EngineFlags.h). Default ON; =0 -> cast as before')
        break
else:
    raise SystemExit('slot not found')
open(h,'w').write('\n'.join(lines))
print('ok')
