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

- B3 search targets (USER 2026-11-20, verbatim): *"Generally speaking, if you have only one Diabolic Revelation
  and only 6 (or 5) mana you don't want to cast it. At 6 mana with an extra it might make sense to play it to
  fetch something useful. Note that a number of cards in the deck are not worth searching at all. Blood Sun
  should be searched early with Grim Tutor or Dimir Machinations if it is missing, but is not necessary to
  search once we have one. The only lands we should be searching for produce 3 (lotuses) or 4 mana (scorched
  ruins) and, with blood sun out, Lotus Field and Vale are the same so they can be collapsed. The only
  acceleration enchantments we want to consider are Exploration and Fastbond and, honestly, we might just want
  to always get fastbond in Goldfishing to simplify things as you don't need any more of these once you have
  it. We shouldn't run out of life before finishing the opponent. Obvious search targets beyond that would be
  Time Warp (earlier), Emrakul (later) and almost always a replacement Diabolic Revelation. (you don't want to
  run out of these, though there will be times where you use a Grim Tutor to find Diabolic Revelation. If you
  have none that is a clear play."*
  Parent's reading -> provider RESTRICTION (USER-authored, ON by default, one `MTG_` lever so it can be A/B'd
  and reported against the unrestricted search; adoption stays the USER's):
  (a) Revelation is not cast at X <= 1 unless another Revelation is in hand (5 mana = X 0 is already folded);
      USER clarification (verbatim): *"Right at 5 mana the Revelation is useless and should never be cast. My
      guidance on duplicates is for when you have 6 mana."* -> X = 0 is NEVER cast (no exception); X = 1 only
      with another Revelation in hand; X >= 2 unrestricted by this rule;
  (b) tutor targets (Revelation packages, Grim Tutor, Machinations' transmute) are limited to: Blood Sun only
      while we have none (battlefield or hand); lands only Lotus Vale / Lotus Field / Scorched Ruins; accel
      enchantments only Exploration and Fastbond, none once a Fastbond is on the battlefield or in hand,
      Fastbond ordered first; Time Warp (ordered early); Emrakul (ordered late); Diabolic Revelation (a
      replacement, nearly always). Everything else (Burgeoning, Ulamog, Grim Tutor, Machinations, Karoos,
      fastlands, City, Cairns, Tomb) is not a search target;
  (c) under Blood Sun Lotus Field = Lotus Vale ALWAYS (F-R2 on even while Ulamog is reachable: answers the
      step-6 Ulamog-exiles-own-Blood-Sun question);
  (d) own death stays live (B1).
  Open readings surfaced to the USER: Blood Sun via Revelation when missing (kept allowed); transmute into
  Grim Tutor (excluded by the whitelist); "always get Fastbond" as Exploration-not-searched-while-Fastbond-
  in-library (built as an A/B arm, not the default).
  USER revision (verbatim): *"I have never run out of Diabolic Revelation because you get to Emrakul and Emrakul
  shuffles when you chain them. You might be able to drop Exploration from the list and just use Fastbond in
  this goldfish. I like to use Exploration in real games when it seems workable, but this isn't so important
  without an opponent. For Machinations there was one mistake there. We must be able to find Grim Tutor in
  that case. Sometimes you need to chain it into Grim Tutor and then into Diabolic Revelation as an example. If
  you have blood sun out or in hand, Grim Tutor should be the only target. I'm not 100 sure this is necessary,
  but if you have a situation where you have 1BB but no Red and blood sun in hand we might want to leave in a
  red producing land like Rakdos Carnarium (if we have extra land drops and nothing to play with it) or City
  of Brass (if we have no land drops or something to play this turn). That is a pretty rare case, but we could
  just add one or both of these to the list depending on the situation."*
  -> AMENDED target list (supersedes (b) where they differ):
  * Exploration is NOT a tutor target (default; an A/B arm keeps it). Accel = Fastbond only, none once a
    Fastbond is on the battlefield or in hand.
  * Machinations' transmute: Grim Tutor is a target (chain Machinations -> Grim -> Revelation); Blood Sun too
    only while we have none; with Blood Sun on the battlefield or in hand, Grim Tutor is the ONLY target.
  * Red-source exception: with Blood Sun in hand (none in play) and no red source on the battlefield or in
    hand, Rakdos Carnarium and City of Brass join the list (the search picks which; USER's guide: Carnarium
    with spare land drops and nothing else to play, City when we need the red this turn).
  * Diabolic Revelation stays a target (a replacement); no change.
  USER (verbatim): *"But you are right, exploration when life is looking dicey could be a consideration. (say
  if you had to play multiple grim tutors to get blood sun and Diabolic Revelation and pay life for lands at
  the start) It doesn't help once fastbond is out, though and generally this wouldn't come up."*
  -> Exploration IS a target again, but only (i) while no Fastbond is on our battlefield and (ii) when our life
  is low (default threshold: life <= 10, a parent-chosen number surfaced to the USER), ordered after Fastbond.
  USER (verbatim): *"Worst case scenario you maybe end up with 3 City of Brass and take 7 from them before Blood
  Sun + 2 Grim Tutor 6, so you end up at 7 life and can only play 6 lands with fastbond. In this extreme case it
  would probably have been better to use Exploration, but this is difficult to hit."* -> the USER's worst case
  (life 7) sits inside the life <= 10 threshold; threshold kept.
  USER (verbatim): *"But even in this worst case you still get your normal land drop before taking damage, so
  it's probably possible to win at one life."* -> Fastbond's first land is free (7 lands at 7 life). Exploration
  stays a low-life target (rarely live; cheap); the A/B arms report whether it ever matters.

- B4 big lands wait for Blood Sun (USER 2026-11-20, verbatim): *"Generally Scorched Ruins and Lotuses wait for
  Blood Sun to drop. The only exception is when you fail to have the 3 mana or Red needed for Blood Sun. In
  that case Lotus Vale is a reasonable drop in order to play it. You lose 2 lands, but can at least start
  progressing."* -> provider RESTRICTION (same lever family as B3, ON by default, A/B'd and reported): while
  no Blood Sun is on our battlefield, Lotus Vale / Scorched Ruins / Lotus Field are not played (nor put by
  Burgeoning); exception: Lotus Vale when a Blood Sun is in hand and our other mana cannot cast it this turn
  (short of 3 mana or of {R}) but can with the Vale. Ruins (colourless) and Field (enters tapped) never
  enable a same-turn Blood Sun, so they have no exception.
  USER refinement (verbatim): *"Note that Lotus Field may be played early like the vale for the same reasons,
  but is strictly worse at it. The field needs to wait a turn to tap. Also, neither of these should ever be
  played when there is no Blood Sun and 1 or 0 lands out as they cannot pay the sacrifice cost. Scorched Ruins
  I don't ever play without Blood Sun."* -> AMENDED rule, no Blood Sun on our battlefield:
  Scorched Ruins: never played (or put). Lotus Vale / Lotus Field: only when Blood Sun is in hand and our
  other mana cannot cast it (short of 3 mana or of {R}), and only with >= 2 other lands out (Vale: 2 untapped
  to sacrifice); Field is allowed for the same reason (its mana arrives next turn) but ORDERED after the Vale
  (USER: "strictly worse at it"). With 0-1 other lands neither is ever played.
  USER (verbatim): *"In fact you can prune trying Field when Vale is in hand. There is no reason to try the
  Field because it is worse at that point and the same later."* -> pre-Blood Sun, Lotus Field is NOT offered
  while a Lotus Vale is in hand (dominated now, identical once Blood Sun lands).
  USER (verbatim, confirming): *"Field is strictly for when you don't have a Vale in this situation."*
  USER (verbatim, 2026-11-21): *"On Burgeoning the Lotus Field and Lotus Vale are equivalent there, so I'm not
  sure why we would have different rules for them. On the Red-source exception we only need one red source."*
  -> Burgeoning puts (opponent's turn) pre-Blood Sun: Vale and Field get the SAME rule (both judged on our next
  turn: Blood Sun in hand, other mana cannot cast it, >= 2 other lands) and are one class there (the Field-only-
  without-Vale prune is for our own land PLAY); red-source exception: at most ONE red source fetched.
  USER (verbatim): *"Realistically the red-source exception is for Grim Tutor for the most part. "Your other
  lands couldn't cast it on your next turn." This is fine, but we should drop a non-sacrificing land to reach
  sufficient mana and red for Blood Sun if one is available. The Lotus plan is just a backup plan, not the
  intended way to do things."* -> the pre-Blood Sun Lotus exception (play AND Burgeoning put) additionally
  requires that NO non-sacrificing land in hand would, played/put instead, make Blood Sun castable (this turn
  for our play; our next turn for a Burgeoning put). Red-source exception stays on every tutor (USER: "for
  the most part" Grim Tutor), capped at one.
  USER rationale (verbatim): *"Because we would rather save Lotus for an active Blood Sun where it is insanely
  powerful."*

- B5 Dimir Machinations (U3) (USER 2026-11-21, verbatim): *"I would prune this as it only matters under
  clairvoyance. Honestly I'm tempted to fully prune the cast option, since the effect is extremely weak compared
  to the transmute."* -> DECIDED: the opponent-target cast is pruned (provider restriction behind
  `MTG_BLOODSUN_USER_PRUNE`). NOT decided (USER "tempted"): pruning the self-target CAST entirely (transmute
  only). Implemented as its own sub-lever, default ON on the USER's lean, measured in the Stage 5 A/B and
  presented; the residual line it removes is a hand with {2}{B} but not {1}{B}{B}, or a top-3 look worth more
  than any mana-value-3 card.
- B6 go-off bridge (U5) (USER 2026-11-21, verbatim): *"If they are handled smartly there should be no way to
  get stuck on this assuming you have Blood Sun out. We'll have to discuss more if you think you have found a
  counterexample. In my experience on the turn when you can Revelation -> Time Warp (needing something like
  9-11 mana) you quickly ramp up the mana by playing a couple of lotuses. This means that the next turn you can
  reach Emrakul by searching 4 Scorched ruins, Fastbond (or multiple exploration), Emrakul and Revelation."*
  -> the design's "stall" case was the unsmart minimal bridge (X = 2, no lands), not a counterexample. Default
  stands (bridge packages that carry Lotus lands and reach the Emrakul stage next turn are ordered first);
  the canonical line above becomes a step-8 test. Any real counterexample found is brought to the USER.
- B7 second Emrakul (U6) (USER 2026-11-21, verbatim): *"Wasn't this always considered? It should be because of
  the extra turn. Extra Turns doesn't have multiple, though. Cyclone does, but doesn't hardcast. Cyclone should
  be deploying multiples at times."* -> it was NOT considered for a HARD cast: the generic duplicate-legend
  prune (`OfferDuplicateLegendCast`, TurnSolver hand-cast enumeration) drops a second legendary copy unless a
  resident watcher makes tokens off the cast; it never read `cast_trigger_extra_turns`. Step 7 fixes it.
  Cyclone's free casts do not pass through that prune, so its multiples were already offered; Extra Turns
  (1 Emrakul) cannot move; Cyclone moves only where it would hard-cast a second copy.

- B8 Revelation packages (U7) (USER 2026-11-21, verbatim): *"Revelation for X=0 should be pruned entirely in
  the heuristic provider. It is a net negative. I don't know what you mean by same-name copies? Searching for
  only Revelation wouldn't be worth it and should be pruned. (i.e. keep the mana)"* -> provider restrictions
  behind `MTG_BLOODSUN_USER_PRUNE`: no Revelation cast with X = 0; no package whose every card is a Diabolic
  Revelation (keep the mana). "Same-name copies" = WHICH of the four library Scorched Ruins a package takes:
  a pure identity (the copies are interchangeable), folded, nothing for the USER to decide.
  USER refinement (verbatim): *"I guess technically fetching multiple Revelations is a net-positive, but that is
  also dominated by fetching Revelation and something else useful for our situation. (since the next chain can
  get the second Revelation)"* -> additionally AT MOST ONE Diabolic Revelation per package (a second copy is
  dominated by one copy plus a useful card; the next Revelation can fetch the next copy). With B8: X = 1
  fetching only a Revelation stays pruned.
- B9 depth-0 chain walk (U8) (USER 2026-11-21, verbatim): *"Okay, I'll leave this part up to you."* -> the
  agent's call: enable the walk, decouple the priced limit, measure both in step 8.
- B10 Machinations transmute (USER 2026-11-21, verbatim): *"we only need one Blood Sun, so we already have one
  or the first retrieved one, it should get Grim Tutor."* -> confirms B3: once a Blood Sun is on the battlefield
  or in hand (including one fetched earlier this turn), every Machinations transmute takes Grim Tutor only. The
  disclosed "second Machinations takes the default" was about the CAST mode's top-3 look, not the transmute; it
  is moot while the cast-mode prune (B5) is on.
- B11 USER-owned heuristics (USER 2026-11-21, verbatim): *"Yes, I'll help you work through cast order and
  discard heuristic, but you should give them a shot first."* -> the agent drafts cast order, discard buckets
  (and the mulligan policy) with measurements, and presents them; nothing is adopted until the USER answers.
- B12 Grim Tutor targets (USER 2026-11-21, verbatim): *"Grim Tutor should also be handily pruned if it hasn't
  been. There are few targets worth getting with it except Blood Sun and Diabolic Revelation when those are
  missing."* -> provider RESTRICTION (USER-authored, ON by default in the `MTG_BLOODSUN_USER_PRUNE` family with
  its own sub-lever so it can be A/B'd at Stage 5): Grim Tutor's targets narrow from the B3 list to
  {Blood Sun while none is on the battlefield or in hand} ∪ {Diabolic Revelation while none is in hand}; the
  red-source exception (B3 amended, "for the most part Grim Tutor", at most one) is KEPT as part of the Blood
  Sun route (a red source is how the missing Blood Sun gets cast). When neither is missing (and no red-source
  case), Grim Tutor has no target worth its mana + 3 life and is NOT cast. Machinations' transmute -> Grim
  (B10) is unchanged; the chain then fetches whichever of the two is missing. Parent's readings surfaced to
  the USER: (i) "Revelation missing" = none in HAND (a Revelation in the library does not count); (ii) the
  red-source exception kept. Human play stays unpruned. Implemented by the perf agent (bloodsun-perf).
  USER revision (verbatim, same day): *"I'm not entirely sure how confident I should be in that ruling, though.
  Maybe there is an argument to allow the others in your list. We'll have to test it out if it is a notable
  cost. I would probably not bother with fetching the lands except for the red source or if you don't have
  enough land to get to 7 mana in this case. The rest can be fetched when you get multiple cards with Diabolic
  Revelation."* -> AMENDED Grim list: Blood Sun (none on battlefield/hand), Diabolic Revelation (none in hand),
  the one red source (B3 exception), and the big lands (Lotus Vale / Lotus Field / Scorched Ruins, B3's land
  classes and folds) ONLY when our lands cannot reach 7 mana (= Revelation at X = 2). Time Warp, Emrakul,
  Fastbond come through Revelation packages, not Grim. Parent's reading of "enough land to get to 7 mana":
  the mana of lands on the battlefield plus the best land in hand playable with a remaining (or next-turn)
  drop is < 7. Confidence is LOW (USER): the sub-lever's OFF arm (= the B3 list) is A/B'd if Grim Tutor is a
  notable share of the cost; the default after that is the USER's.
  USER rationale + mana gate (verbatim): *"Essentially, you would prefer to use mana on a Diabolic Revelation
  since it starts to become very mana efficient after you hit 7 or 8 mana. Wasting some of your mana on Grim
  Tutor at that point is usually not worth it. That may not be totally correct if you had 2 grim tutors and only
  1 Diabolic Revelation, (in that case you could use the mana to replace the Grim tutors with 2 cards that are
  useful) so we'll have to play with it. Once you have 8 mana it becomes obvious that the Diabolic Revelation is
  a better use."* -> a second Grim sub-lever (ON by default, USER: "obvious" at 8): Grim Tutor is NOT cast while
  a Diabolic Revelation is in hand and our available mana this turn (untapped sources + floating + a land still
  playable this turn) is >= 8; the Revelation does the fetching. Below 8 the B12 list applies. Arms to "play
  with" (measured, reported; default stays the USER's): gate OFF; threshold 7; and the 2-Grim / 1-Revelation
  case exempted.
  USER clarification (verbatim): *"Yes, the reason I mentioned 8 is that at 8 mana+ multiple grim tutors lose
  out to one Diabolic Revelation even when you consider that the Revelation needs to replace itself. At 8 mana
  the only arguable play that might be better with Grim Tutor we may want to leave on the table is tutoring and
  playing Time Warp. We probably should not prune that."* -> the 2-Grim exemption applies only BELOW 8 (it is
  an arm of the threshold-7 variant, never at >= 8). EXCEPTION kept at every mana: Grim Tutor -> Time Warp is
  offered whenever Grim ({1}{B}{B}) and then Time Warp ({3}{U}{U}) are both castable this turn from the
  available mana (colours included); it is the one Grim line the >= 8 gate does not prune.
  USER (verbatim): *"I'm not sure. We might find some other lines with Grim Tutor that have to do with dropping
  things this turn. For example Grim Tutor -> Fastbond which you play into mutliple lands + Time Warp in hand.
  It's possible pruning Fastbond is also dicey for this reason. We'll just have to play with it. I may also be
  getting ahead of myself. We should probably be measuring branching and performance more first."*
  -> B12 is PROVISIONAL and MEASUREMENT-FIRST: the perf agent measures branching and cost by site (Phase A)
  before any Grim prune is built; a Grim prune is built only if Grim Tutor is a notable share, and then as
  levered arms including a "same-turn deploy" exception (Grim -> a card cast this turn that enables this-turn
  plays: Time Warp; Fastbond with >= 2 lands in hand to play), all measured and presented; no default chosen
  by an agent. The same caution applies to the B3 "no Fastbond target once Fastbond is held" and any other
  tutor-list prune.
- B13 Lotus sacrifices (USER 2026-11-21, verbatim): *"Machinations is the one case that is dead simple at
  least. I doubt it will be a notable cost unless we haven't applied the prune yet. We should prune the
  sacrifices for sure. Usually the choices are obvious, but we should have an order that we follow that
  attempts to keep a red, a green and 2 black. Naturally, we should also prioritize bouncelands over regular
  lands."* -> DECIDED provider RESTRICTION (USER-authored, ON by default, own `MTG_BLOODSUN_*` sub-lever, unit
  tests on/off; human play unpruned): the Lotus Vale / Scorched Ruins / Lotus Field sacrifice choice (only
  exists without Blood Sun) collapses to ONE pair chosen by an order: (1) the remaining lands keep at least one
  red, one green and two black sources where possible (dual / any-colour lands counted by what they can make);
  (2) then sacrifice regular lands before bouncelands (Parent's reading of "prioritize bouncelands over regular
  lands": KEEP the bouncelands, which make two mana, and sacrifice the one-mana lands first -- surfaced to the
  USER); (3) then F-L1 (a tapped victim before an untapped one of the same class, Lotus Field). Machinations:
  no new work (B5 prune applied; the perf census confirms its share).
  USER (verbatim): *"Yes, we keep bouncelands if possible. The only tricky decision is the keep of a bounceland
  that doesn't meet our colour needs vs a normal land that does. Given that the lotus can kind of fill the gap
  for one of them I suppose we keep the bounceland in that case."* -> ORDER REVISED: (1) keep bouncelands
  (sacrifice regular lands first), even over a regular land that would meet a colour need; (2) then the colour
  rule (1 red, 1 green, 2 black among the kept lands), counting a Lotus Vale / Lotus Field as ONE any-colour
  source (Scorched Ruins makes only colourless and fills nothing); (3) then F-L1.

## Open USER decisions
- Go-off (2) "always bridges to (1)": design to report any residual gap.
- Two-deviation completeness gap (step 6e diagnosis, design §14): a line needing two simultaneous non-default
  breakpoint picks (Machinations -> Grim -> Revelation X=2 -> kill) is unfindable at default settings at any budget
  (a documented single-deviation bound, `docs/design/bp-empty-arm-deferred-site.md` §6.1). 6e repaired the dead
  `MTG_BP_DEV_SEQ_ALL` lever (default off; T5 -> T4 on that chain, +12-25% search work). SCHEDULED as step 7b:
  re-measure `_ALL`, then a scoped default-on version (sequences only at acquisition / cantrip resolution sites)
  through the prune/adoption bar. Whether a scoped `_ALL` becomes default is presented to the USER with numbers.
- Step 7 parent readings (2026-11-21, of existing USER rulings, not new decisions): (1) B3's "X = 1 only with another
  Revelation in hand" applies only when X = 1 is the largest X our mana affords (USER: "My guidance on duplicates is for
  when you have 6 mana"), so the 21-mana Revelation X = 1 -> Emrakul kill stays findable; (2) an Overgrown Tomb put on
  the opponent's turn enters tapped without paying -- a sound fold (no instant-speed use exists in this deck; it untaps
  before our first sorcery-speed use).
