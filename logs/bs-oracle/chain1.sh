#!/bin/bash
S=/workspaces/MagicDeckTester/build/scratch/bs-oracle
bash $S/arm.sh $S/mtg.memo 2 $S/mbs20.json memo_bs
bash $S/arm.sh $S/mtg.memo 2 $S/mother.json memo_other
bash $S/arm.sh $S/mtg.base 2 $S/mother.json base_other
bash $S/arm.sh $S/mtg.memo 0 $S/mall.json off_memo
echo done > $S/chain1.DONE
