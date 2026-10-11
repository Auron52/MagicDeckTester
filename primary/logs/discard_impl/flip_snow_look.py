import re
R = "/workspaces/MagicDeckTester/"
p = R + "src/ai/TurnSolver.cpp"
s = open(p).read()
old_c = ("// MTG_SNOW_LOOK_COLOR -- tighten breakpoint site 8's playability gate from MANA VALUE to a real\n"
         "// COLOUR test. Default OFF until measured; `=1` arms it.\n")
new_c = ("// MTG_SNOW_LOOK_COLOR -- tighten breakpoint site 8's playability gate from MANA VALUE to a real\n"
         "// COLOUR test. DEFAULT ON since 2026-10-10 (a sound necessary-condition prune: 0 worse cells in 1,440\n"
         "// held-out games, byte-identical unbudgeted -- the USER's collapse-wasted-search rule); `=0` disarms it.\n")
assert old_c in s
s = s.replace(old_c, new_c)
old_e = '    static const bool env = EnvOn("MTG_SNOW_LOOK_COLOR");\n'
assert old_e in s
s = s.replace(old_e, '    static const bool env = EnvOn("MTG_SNOW_LOOK_COLOR", true);   // DEFAULT ON\n')
open(p, "w").write(s)
p = R + "src/ai/HeuristicArm.h"
s = open(p).read()
anchor = "freed work is respent ([[matrix-cost-is-abandonment-rate]]). Default OFF\n"
assert s.count(anchor) == 1
s = s.replace(anchor, "freed work is respent ([[matrix-cost-is-abandonment-rate]]). DEFAULT ON since 2026-10-10 (sound prune, USER collapse rule); =0 disarms\n")
open(p, "w").write(s)
p = R + "docs/design/snow-breakpoint-degeneracy.md"
s = open(p).read()
h = "### `MTG_SNOW_LOOK_COLOR` — RECOMMENDED for adoption (not flipped; see why below)\n"
assert h in s
s = s.replace(h, h + "\n**FLIPPED DEFAULT ON 2026-10-10.** The tree-state reason below no longer holds, and the lever is a sound\n"
              "necessary-condition prune, so the USER's collapse-wasted-search rule (CLAUDE.md) puts it on by default.\n"
              "`MTG_SNOW_LOOK_COLOR=0` restores the MV-only gate. Snow's budgeted digests move (freed work is respent),\n"
              "so the flip is measured in the 10-10 pooled A/B and rebaselined with that round's GT.\n")
open(p, "w").write(s)
print("ok")
