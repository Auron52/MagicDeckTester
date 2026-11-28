#!/bin/bash
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
bash $S/arm.sh $S/mtg.lf 2 $S/mbs20.json lf_bs
bash $S/arm.sh $S/mtg.lf 2 $S/mother.json lf_other
bash $S/arm.sh $S/mtg.lf 2 $S/mdc7.json lf_dc MTG_MANA_ORACLE_HEAVY=1
bash $S/arm.sh $S/mtg.memo 2 $S/mdc7.json memo_dc MTG_MANA_ORACLE_HEAVY=1
echo done > $S/stage2.DONE
