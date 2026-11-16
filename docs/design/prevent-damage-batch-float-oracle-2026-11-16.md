# Prevent Damage: joint mana float in the oracle

The fresh 5,000-game PGO gate on source `aee55374648c` (games 77,000–81,999,
`codex-pd-agekey-20261116`) had byte-identical oracle-on/off play and zero
`APPLY-DROP`, `APPLY-DROP-ORDER`, or `ENUM-GAP` flags. It still had 64
`ORACLE-GAP`, six `UNMODELED`, and five `CAPPED` flags, so the deck remains beta.
The next unused block is 82,000–86,999. The six unmodeled flags are payment
table probe caps; the five capped flags reached the escalated 200,000-apply
ceiling. Full signatures are in the gate report.

Most oracle gaps are a timing difference. The engine solves a multi-cast mana
bill and taps lands before the first cast; the oracle previously replayed only
one payment per cast. When the first cast is Manabarbs, the two lines deal
different damage on taps for the second cast. The Beseech → Vito self-lethal
case is the converse: a joint payment can kill its controller before either
spell resolves, while the per-cast oracle survives long enough to put Vito on
the battlefield.

`MTG_ORACLE_BATCH_FLOAT` is a default-on audit-only rule (`=0` disables it). On a board armed
for damage events, it offers a batch frontier after the engine has computed
the aggregate fixed bill. The oracle can skip the batch or choose a tap set
from its existing payment table. Chosen taps float before the first cast, and
the usual damage-event flush runs at that timing point; the later casts pay
from float. This models a legal payment timing without changing the engine's
own play. Batch options are deduplicated by the state after their taps,
before a verification payment spends the aggregate bill. Different colours
that spend to the same post-payment board can matter to the ordered casts.

Focused Release replays clear the recorded Manabarbs/Beseech, double-Manabarbs,
Manabarbs/Pyrohemia, Beseech/Vito self-lethal, and earlier self-lethal
signatures (seeds 30077025, 30077339, 30080855, 30078289, 30076504): each
has zero oracle gaps, unmodeled flags, and caps with the rule on. The first
two off-control replays still report ten and 12 oracle gaps respectively.
On 100 paired games from seed 39082000, oracle-on/off play output was
byte-identical. Oracle applies rose from 2,188,027 to 2,204,745 (+0.76%),
and recorded oracle time from 103,426 to 104,253 ms (+0.80%). Both arms
reported zero issues on that small block. Logs are in
`build/scratch/pd_quakeorder_ab/batch_float_*`.

A fresh 5,000-game PGO mana-clean gate is still owed after this adoption.
