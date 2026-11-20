# Analysis ledger — Blood Sun

Deck: `decks/Blood Sun/Blood Sun.cod` (B/R/G/U big-mana land ramp into extra turns), 60 cards:
4 Blood Sun, 4 Exploration, 2 Burgeoning, 1 Fastbond, 4 Diabolic Revelation, 4 Grim Tutor,
2 Time Warp, 2 Emrakul the Aeons Torn, 1 Ulamog the Ceaseless Hunger;
lands 4 Lotus Vale, 4 Scorched Ruins, 3 Lotus Field, 4 Golgari Rot Farm, 4 Rakdos Carnarium,
2 Dimir Machinations, 4 Blooming Marsh, 4 Copperline Gorge, 2 Graven Cairns, 4 City of Brass, 1 Overgrown Tomb.
Sideboard removed 2026-11-20 (USER permitted; it was unreachable, no wish in the main).

The USER asked for the analysis on 2026-11-20 ("Let's analyze the Blood Sun deck.").

Branch/worktree: `bloodsun-analysis` @ `.claude/worktrees/bloodsun` (cut from origin
`phase-1-2-deck-analyzer` c53a7ae26). The deck folder was COPIED from the primary checkout (untracked
there; original left in place). The primary's other uncommitted files belong to another session.

**The plan of the deck (USER, 2026-11-20, verbatim):** *"Just so it is clear, the typical way this deck
wins is with extra land drops -> extra turns -> looping emrakul. Diabolic Revelation can be used to
consistently fetch a new emrakul. Prior to that you can use it to get land, Exploration or Fastbond
another copy of itself, Time Warp etc. and then follow up the next turn with much more mana."*

So: Blood Sun strips every land's non-mana abilities, which turns the drawback lands into free big
mana (Lotus Vale / Scorched Ruins without their sacrifice-two-lands replacement, Lotus Field without
its enters-tapped + sacrifice trigger, Karoos without the bounce, City of Brass without the ping).
Exploration / Burgeoning / Fastbond turn that into several big lands a turn; Diabolic Revelation
(X tutor) assembles lands + engines + Time Warp, then an Emrakul (15 mana: cast trigger = extra
turn; to the graveyard = shuffle the graveyard back, so it can be fetched again). Extra turns do NOT
count toward the turn count (USER ruling 2026-10-26, Unpredictable Cyclone): win turn = round.

USER, 2026-11-20 (verbatim): *"I can help with some examples in the future or in references as
needed. But overall, it tends to go off pretty quickly after Blood Sun has landed. (though usually not
on the turn of)"* -- the plausibility yardstick for Stage 5b: the turn AFTER Blood Sun (or the one
after) should usually be the explosive one; a game that stalls with Blood Sun down is a lead to read.
Ask for USER reference games once the deck is viewer-ready.

USER, 2026-11-20 (verbatim), on Diabolic Revelation: *"With Diabolic Revelation I think we will need a
go-off heuristic for larger X values. The idea is that we can come up with sets of cards that allow
turns to be chained into the win. Naturally we'll also want heuristics as well, but we should be able
to avoid large x values messing us up, since once X is high enough the game is over. Once you have 22
mana and Diabolic Revelation it is easy to search and play Emrakul + get another tutor for next
turn."* -> design: a provider go-off RECOGNIZER for chainable sets (a proof of a win this round where
it is one; an ORDER where it is only likely), large-X multisets dominated once the recognizer
certifies the round; ordinary order-only heuristics below the threshold. Passed to the design agent.
USER addendum (verbatim): *"But prior to that we can often chain into Time Warp."* -> below the
Emrakul-loop threshold, Revelation for Time Warp + Revelation bridges extra turns (bounded: 2 Time Warps,
returned only by an Emrakul graveyard shuffle) while land drops grow the mana. Passed to the design agent.
USER addendum 2 (verbatim): *"(and get additional lands or extra land playing cards to ramp up our
mana)"* -> the bridge Revelation's spare picks are big lands and Exploration/Burgeoning/Fastbond.
USER worked example (verbatim): *"An example of a easier chain would be Revelation for Time Warp,
Revelation, Exploration, and two lotus lands. As long as you have a remaining land-drop you can play the
first lotus for Exploration and then the second for Time wark. Technically it is even easier if you get
Fastbond, though you will need the green held if you have no current land drop."* -> canonical scenario
test for the bridge recognizer (Revelation X=5; colour matters: Scorched Ruins is colourless).
USER (verbatim): *"And having these in hand makes it even easier, since you don't need to search pieces
you already have."* -> the required set is net of HAND (cards); X covers only the missing pieces.
USER (verbatim): *"To be clear, though, often you want multiple exploration or fastbond to go off in
turns like these. Having an exploration out if often not enough since you need to spend the land drops
earlier. You want to get to an extra turn with what you have remaining."* -> the recognizer's resource
is the REMAINING land drops / mana / cards this turn; an Exploration in play does not satisfy the set
by itself; extra Explorations (stacking) or Fastbond are often required picks.
USER (verbatim): *"Once you can play 4 lands in a turn you can search for 4 Scorched Ruins (or 3 and
one lotus), emrakul and Diabolic Revelation, play Emrakul and begin looping. It typically goes something
like that after you have the extra land drops from the Time Warp extra turn."* -> canonical Emrakul-loop
entry: >= 4 remaining drops + 11 mana -> Revelation X=6 {Ruins x4 | Ruins x3 + Lotus, Emrakul,
Revelation} -> 15-16 mana -> Emrakul -> loop. Scenario test.
USER (verbatim): *"(naturally you can also get exploration or fastbond and play it if you have the mana
to do so)"* -> drops can be bought inside the go-off turn ({G} per Exploration/Fastbond).
USER (verbatim): *"(to make it possible to reach those land drops in one turn)"*; *"But if you cannot
reach that, there is the extra time warp you can get in the same way to build up your lands."*
Parent summarised the ladder: (1) reach Emrakul this turn -> go off; (2) else reach a Time Warp extra
turn keeping a Revelation; (3) else ordinary search, ramp-first order. USER (verbatim): *"We will
probably still heuristically prune the regular search case, but having this go-off should help. Note
that 2 should also be included in the go-off. There is always a bridge to 1 if you can do 2."* ->
go-off = (1) + (2); (3) gets a provider prune (prune-adoption bar). Design to check the "always a
bridge" claim against the 2-Time-Warp limit and report any residual gap to the USER.
USER (verbatim): *"Well, if you are casting Revelation and putting out extra lands anyway. Maybe not if
you just have Time Warp in hand."* -> go-off (2) = the REVELATION-DRIVEN bridge (Revelation cast this
turn, Time Warp + a Revelation retained + extra lands put out); a bare Time Warp from hand is case (3).


## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, attacks or blocks; opponent-facing clauses are inert,
  but self-binding clauses bind us. Model the full card; goldfish-inert deferrals are PROVISIONAL
  until the user signs off.
- **Our own life matters in this deck** (Fastbond pings per extra land, City of Brass, Grim Tutor's
  3 life, Worldfire setting both totals to 1). Own death is currently gated (`own_death_live`, armed
  only for damage-event decks) -- see Stage 2 decision B1.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces (Revelation's X and its up-to-X targets, Grim Tutor target, which lands Lotus Vale /
  Scorched Ruins / Lotus Field sacrifice, Karoo return, extra land drop sequencing) are SEARCHED,
  provider-narrowed (restrict/order only), or collapsed by a stated SOUND dominance fold, and
  surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic); fix shared cards faithfully for every deck.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= `nproc`.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf`, no `timeout`; build with `./build.sh`
  only; subagents do NOT push the shared branch; hand off at ~250K context (CLAUDE.md rule).

## Stage 1 — coverage (2026-11-20)
Missing (12): Blood Sun, Lotus Vale, Scorched Ruins, Diabolic Revelation, Grim Tutor, Golgari Rot
Farm, Dimir Machinations, Copperline Gorge, Graven Cairns, Lotus Field, Fastbond, Worldfire.
Present (8): Exploration (`static_extra_land_drops`), Burgeoning, Rakdos Carnarium (Karoo),
City of Brass (`tap_self_damage`), Blooming Marsh (fastland), Time Warp, Emrakul the Aeons Torn,
Ulamog the Ceaseless Hunger (all from the Unpredictable Cyclone / earlier analyses).
Present but must be re-reviewed for THIS deck: every land, because Blood Sun removes their non-mana
abilities (Karoo enters-tapped + bounce, City of Brass ping, fastland enters-tapped condition).
Scryfall JSONs + rulings: `build/scratch/bloodsun/scry/collection.json` (primary build volume).

## Stage 2 — cards
2026-11-20 ~11:45 UTC: Opus design agent launched (brief `logs/bloodsun/design-brief.md`) ->
`docs/design/blood-sun-pipeline.md` + step plan; USER guidance above relayed to it verbatim. Then one
fresh implementer agent per step, serial. USER asked (~12:33 UTC) to continue autonomously overnight.

## Decided by the USER
- B1 own death (USER 2026-11-20, verbatim): *"Yes, running out of life is a loss, so the deck should not
  fastbond so much that it loses."* -> `own_death_live` armed for this deck by a deck-level stamp; search,
  playout and go-off recognizer all respect it.

- B2 Worldfire (USER 2026-11-20, verbatim): *"Oh crud. Worldfire should not be in the list. that was a
  placeholder and we'll need to correct it."* -> the 1 Worldfire slot is a PLACEHOLDER; the real card is
  pending from the USER. U2 (how Worldfire is used) is moot. Worldfire stays implemented in cards.json (step 5,
  68cf02b88: generic, tested, harmless to other decks); the decklist is NOT edited until the USER names the
  replacement. Nothing downstream of the list (profile, suite entry, generations) runs on the placeholder list.
  USER (verbatim): *"I went through the list and it was a placeholder filled by an Overgrown Tomb. Please feel
  free to update the decklist and even delete the "Sideboard" if you want or if you would really prefer I can
  save over it."* -> DONE: Worldfire -> Overgrown Tomb (present: `etb_pay_life_to_untap` 2, gated under Blood
  Sun by step 3), sideboard zone removed; worktree and primary copies identical, CRLF kept.

## Open USER decisions
- Go-off (2) "always bridges to (1)": design to report any residual gap.
