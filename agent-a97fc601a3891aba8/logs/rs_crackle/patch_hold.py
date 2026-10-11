#!/usr/bin/env python3
"""Rewrite HinataProvider::XCandidates into a shared hold helper + the HoldCastUnlessLethal hook."""
p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/src/ai/DecisionProviders.cpp'
s = open(p).read()
start = s.index("// Hold a LONE Crackle with Power as a combo piece. See the header note: casting a single non-lethal")
end = s.index("// Magma Opus scaled-cast (face-damage) variants (ScaledCastVariants) -- the DECK-SPECIFIC cost model.")
new = '''// MTG_HINATA_HOLD_LETHAL_BURN (heurarm HINATA_HOLD_LETHAL_BURN). DEFAULT ON; =0 -> XCandidates' own
// Crackle-plus-attack lethal estimate, byte-identical to 2cec6ec4.
static bool HinataHoldLethalBurn()
{
    static const bool on = EnvOn("MTG_HINATA_HOLD_LETHAL_BURN", true);
    return heurarm::Flag(heurarm::HINATA_HOLD_LETHAL_BURN, on);
}

// MTG_HINATA_HOLD_NEEDS_HINATA (heurarm HINATA_HOLD_NEEDS_HINATA). DEFAULT OFF: it narrows the SCOPE of
// a USER-approved heuristic, so the USER decides; =1 is the measured arm.
static bool HinataHoldNeedsHinata()
{
    static const bool on = EnvOn("MTG_HINATA_HOLD_NEEDS_HINATA");
    return heurarm::Flag(heurarm::HINATA_HOLD_NEEDS_HINATA, on);
}

// Is `def` a LONE Crackle the hold applies to on this state -- everything in the hold rule except its
// "wins THIS turn" exemption? Shared by XCandidates (the estimate, lever off) and HoldCastUnlessLethal
// (the subset walks' projection, lever on), so the two arms differ ONLY in how lethality is judged.
static bool HinataLoneCrackleHeld(const GameState& s, const CardDefinition& def)
{
    static const bool enabled = !EnvOn("MTG_NO_HINATA_HOLD_CRACKLE");
    if (!enabled || !IsCrackleCountSpell(def)) { return false; }
    // HUMAN play (the viewer): never hide a castable Crackle -- the hold-as-a-combo-piece prior is
    // an AUTONOMOUS search heuristic, not a restriction on the player. Offer the full affordable X
    // range so the human can choose to fire a chip Crackle if they want.
    if (HumanPlayActive()) { return false; }

    // A SECOND copy in hand -> spending one chip Crackle is free (the other stays for the combo).
    // A Reality Spasm is NOT a copy: it is the combo's OTHER piece, and its mana is already in
    // max_affordable (HinataRitualNetBonus), so a combo-turn kill passes the lethal gate. Until
    // MTG_CRACKLE_COUNT_EXACT the predicate read RS as a Crackle, and an RS in hand turned this hold
    // off exactly when the combo was assembled (docs/design/reality-spasm-crackle-classification.md).
    int copies = 0;
    for (const Card& h : s.players[s.active_player_index].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(h);
        if (d && IsCrackleCountSpell(*d)) { ++copies; }
    }
    if (copies >= 2) { return false; }

    // MTG_HINATA_HOLD_NEEDS_HINATA: the combo the hold protects needs Hinata -- without her a Reality
    // Spasm untaps X for X (nets nothing) and a lone Crackle is plain burn. Hold only while she is in
    // play or castable from hand this turn (the same optimistic gate as the same-subset credit).
    if (HinataHoldNeedsHinata() && !HinataInPlay(s) && !HinataCastableFromHand(s)) { return false; }
    return true;
}

// Hold a LONE Crackle with Power as a combo piece. See the header note: casting a single non-lethal
// Crackle for chip damage throws away the Reality-Spasm -> big-Crackle lethal the shallow search
// cannot see past its horizon. This is the "default to hold Crackle unless you have multiples" prior
// -- the search's own lethal enumeration still fires it the turn it wins (the combo turn's ritual
// mana inflates max_affordable so 5X reaches lethal here and the gate lets it through).
std::vector<int> HinataProvider::XCandidates(const GameState& s, const CardDefinition& def,
                                             int max_affordable) const
{
    std::vector<int> generic = GenericProvider::XCandidates(s, def, max_affordable);
    if (generic.empty() || !HinataLoneCrackleHeld(s, def)) { return generic; }
    // MTG_HINATA_HOLD_LETHAL_BURN: offer the X and let the subset walks hold it -- they drop every
    // subset that casts this Crackle without projecting lethal (HoldCastUnlessLethal below), counting
    // what the estimate here cannot: a Magma Opus / Soulfire co-cast and the subset's real attack
    // (summoning sickness, creatures tapped for mana), on a subset that already passed payability.
    // Hinata d0 gi 157 / 196 held a Crackle that was part of an exact-20 RS + Crackle + Magma kill.
    if (HinataHoldLethalBurn()) { return generic; }

    const int active   = s.active_player_index;
    const int opp_life = s.players[1 - active].life;
    int mult = def.params.x_damage_multiplier; if (mult < 1) { mult = 1; }

    // Would the biggest affordable Crackle win THIS turn? Over-estimate the attack (every own
    // creature's power, ignoring summoning sickness) so we only ever HOLD when even the best case
    // whiffs -- never forfeiting a same-turn Crackle+attack kill.
    int max_x = 0;
    for (int x : generic) { if (x > max_x) { max_x = x; } }
    int attack_ub = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == active) { attack_ub += std::max(0, AttackPowerOf(s, p)); }
    }
    // 2HG: with X >= 2 the second head is a face target too, so the best-case cast deals 5X to
    // BOTH heads of the shared pool (see the CollectActions crackle branch -- lockstep).
    const int face_hits = (gamesetup::OpponentHeads() >= 2 && max_x >= 2) ? 2 : 1;
    if (max_x * mult * face_hits + attack_ub >= opp_life) { return generic; }   // wins now -> cast it

    return {};   // lone, non-lethal -> HOLD the combo piece
}

// The subset-walk half of the hold (MTG_HINATA_HOLD_LETHAL_BURN): a lone Crackle is cast only in a
// subset that wins this turn by the walks' own projection. See DecisionProvider::HoldCastUnlessLethal.
bool HinataProvider::HoldCastUnlessLethal(const GameState& s, const CardDefinition& def) const
{
    return HinataHoldLethalBurn() && HinataLoneCrackleHeld(s, def);
}

'''
s = s[:start] + new + s[end:]
open(p, 'w').write(s)
print('patched')
