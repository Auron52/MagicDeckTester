# Jumbo Cactuar cleanup discard proposal

Status: proposed for user review; not active in the provider.

## Evidence

In 400 searched games there were zero real cleanup discards. The rollout
search made 1,913 cleanup discard choices, all while fewer than four lands
were on the battlefield. A paired default-versus-inverse ranking bound at
depth 3 and depth 5 changed no win turn or play digest in 300 games. The
measured opportunity for a cleanup policy at the shipped settings is zero in
that sample. The policy below is therefore a safeguard for unusual hands,
not a measured speed or win-rate improvement.

## Quota-first keep policy

Count battlefield cards before reserving hand cards and recompute after each
shed. Choose the nearest credible kill route first, then fill one **shared mana
bucket** toward its cost. Its normal quota is five mana cards across board and
reserved hand; use six when that extra land, Aura, or untapper actually reaches
the next mana breakpoint. A required next land drop is protected even if the
bucket is already full. The roles below compete for those same spots; they
are not separate quotas that automatically consume the whole hand.

| Priority | Keep | Rule |
| --- | --- | --- |
| 1 | Tooth and Nail | First payoff to protect when nine mana for entwine is plausible within the next two turns. It finds and puts both Cactuar and Surrak in one spell. If mana is far short, give its slot to the mana package rather than strand it in hand. |
| 2 | Next land drop | Keep one Forest for the next turn. At end of turn one, keep two for turns two and three if available. Further lands compete with acceleration only when they reach the chosen cost sooner. |
| 3 | Mana package | Keep a usable land Aura and one land untapper together. Green Sun's Zenith can fill the untapper slot by fetching Arbor Elf when no untapper is available and that play is affordable. Auras generally beat a second untapper, because the first untapper can re-tap the enhanced Forest. A second untapper earns a slot only when it advances the 7–9 mana route faster than another Aura or land. Stack Auras on the highest-yield Forest and untap that Forest. |
| 4 | Other payoff access | Keep Cactuar or Zenith when Tooth is absent or too far away. Protect one haste enabler only once a Cactuar cast or Zenith fetch is reachable and the chosen line needs haste. Prefer Surrak when it can precede Cactuar in the same turn; otherwise use Greaves or Chariot when their equip line is affordable. |
| 5 | Surplus routes | Finale X=10 costs twelve mana and is usually harder than the Tooth or direct Cactuar route. Keep it as a finisher when the mana package and remaining hand space can support twelve; otherwise shed it before a nearer payoff. Nylea and duplicate payoffs fill only genuine spare slots. |

For the mana package, rank candidate keep sets by the mana they produce on the
next turn and the turn after, including a Forest re-tap from one ready untapper
and the actual casting costs of Auras or a Zenith for Arbor Elf. Prefer Utopia Sprawl or Wild Growth while
only one mana is available; promote Overgrowth when its three-mana cost can be
paid and the enhanced Forest can be re-tapped. Count an already deployed Aura
or untapper toward the package before reserving another from hand. Reserve at
most one untapper by default; the second must clear a concrete breakpoint.

Within overflow, shed the card whose removal least delays the selected kill
route, breaking ties by distance from castable. Price entwined Tooth at nine,
Zenith for Cactuar at eight, and Finale X=10 at twelve. Protect staged cards
and required pieces through `CleanupDiscardRankingWithOrder`, and return one
shed index.

The tutor policy keeps Arbor Elf ahead of Voyaging Satyr for this mono-Forest
goldfish. Satyr counts toward the hand's untapper quota when naturally drawn;
it is not a single-creature tutor target.

The remaining judgment call is how often a sixth mana slot reaches the chosen
7–9 mana line sooner than a second payoff. The measured sample has no real
cleanup shed, so it cannot resolve that choice empirically yet.
