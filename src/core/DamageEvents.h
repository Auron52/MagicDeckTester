#pragma once
// ---- DAMAGE EVENTS: own death, damage-event triggers, lifegain replacement (Prevent Damage) ----
//
// WHY THIS EXISTS. Until 2026-09-27 our own life was bookkeeping: a painland tap was a raw
// `life -= 1` at five separate sites, nothing could trigger on it, and nothing ever checked it --
// our life could go to -40 with no consequence. The Prevent Damage deck makes all three load-bearing
// at once:
//   * Tamanoa ("Whenever a noncreature source you control deals damage, you gain that much life")
//     turns every painland / City of Brass / Ancient Tomb tap and every Manabarbs trigger into a
//     lifegain EVENT, so a tap must be a damage event with a source and an amount.
//   * Rhox Faithmender (x2) and Bilbo (+1) REPLACE lifegain, and Vito ("target opponent loses that
//     much") and Dina ("each opponent loses 1", per EVENT) turn it into the win -- so lifegain must
//     go through one replacement chain, and the EVENT granularity is observable.
//   * The Tamanoa ruling: "if a noncreature source you control deals damage to you that drops your
//     life total to 0 or less, you'll lose the game before Tamanoa's ability can resolve." So own
//     death is a state-based action (CR 704.5a), checked BEFORE the triggers resolve.
//
// THE MODEL.
//   1. A mana tap during a payment never fires anything in place: the payment backtracks and the
//      mana cache replays, so side effects there must be restorable. Instead the tap (a) takes its
//      pain off our life at once -- a mana ability's damage is immediate -- and (b) RECORDS the tap
//      on the permanent (Permanent::mana_tap_mark): "a land was tapped for mana" and "this much
//      pain was dealt (or prevented)". Every snapshot/restore the payment code already has carries
//      the mark (PermPaySnap, whole-battlefield copies), so a rolled-back attempt leaves none.
//   2. Once the payment COMMITS (the outermost TapForCostShared success, the batch prepay commit,
//      the end-of-main sweeps), FlushDamageEvents turns the marks into triggers and resolves them,
//      in this order, which is the order we would put them on the stack (we control every one of
//      these triggers and CR 603.3b lets us choose; gains first is weakly dominant for survival):
//        a. SbaCheckpoint -- a payment whose pain took us to 0 loses the game HERE, before any
//           Tamanoa trigger resolves (the ruling above).
//        b. every lifegain event: one Tamanoa trigger per Tamanoa per pain event, and one Purity
//           gain per prevented pain event. Each goes through GainLife -> replacement chain ->
//           Vito / Dina watchers, with an SBA check after each (the drain can kill the opponent).
//        c. every Manabarbs trigger (one per land tap per Manabarbs), each its OWN damage event
//           (DealDamageEvent), which in turn triggers Tamanoa, resolved before the next barb.
//   3. OWN DEATH is sticky by parking our life at kLostLife -- a value no later gain can lift back
//      above 0. That is deliberately NOT a new sticky flag on GameState: every snapshot/restore in
//      the engine already restores life, so a speculative line that died restores cleanly for free,
//      where a new flag would have needed every one of those ~25 sites to learn about it.
//   4. The WON-LOCK: once the opponent has lost and we have not, the game is over, so later damage
//      to us is a no-op. That is what makes OpponentHasLost's `our life > 0` conjunct exact (see
//      GameState.h): our life can only be <= 0 alongside a dead opponent if we died first or in the
//      same event (CR 104.4a: a DRAW, which is not a win).
//
// GATING. Every entry point returns immediately (or takes its byte-identical legacy branch) unless
// GameState::dmg_events_armed, which StampDeckTraits sets only for a deck carrying one of the card
// params below. So every other deck keeps its raw `life -=` pain and its un-replaced GainLife.
//
// NOT ROUTED HERE YET (bracket-noted on the cards, ledger `docs/design/analysis-Prevent Damage.md`):
// generic noncreature burn (EffectHandler::ResolveDirectDamage + the rollout twins) and the
// Crackle / Soulfire self-hits -- no card in this deck deals damage that way, and each is a
// separate executor/rollout pair to convert when a deck needs it. ROUTED (phase I2): Spellshock's
// cast trigger (FireOnCastTriggers) and each Pyrohemia activation / Rolling Earthquake
// (PerformDamageEachCreatureAndPlayer, SpellEffects.h -- creature pass first, then DealDamageEvent
// with the pre-damage Tamanoa count as `tamanoa_lki`).
#include "EnvFlags.h"
#include "GameSetup.h"
#include "GameState.h"
#include "GameLogger.h"
#include "../cards/CardDatabase.h"
#include "../ai/DecisionProviders.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// INCLUDED ONLY FROM SpellEffects.h, after g_tap_speculating / TapSpeculatingNow() are defined and
// before the first pain site. GainLife is defined further down SpellEffects.h (identical declaration).
inline void GainLife(GameState& state, int player, int amount);

namespace dmgev
{

// Our life once we have LOST (see point 3 above). Far below any reachable total and far above
// INT_MIN, so neither further damage nor any realistic gain can move it across 0.
constexpr int kLostLife = -(1 << 28);

constexpr std::uint8_t kMarkLand      = 1;   // a LAND was tapped for mana (Manabarbs)
constexpr std::uint8_t kMarkPrevented = 2;   // the pain was prevented (Purity)
constexpr int          kMarkPainShift = 2;   // bits 2..7 = pain amount
constexpr int          kMarkPainMax   = 63;

// MTG_DMG_EVENT_VERIFY=1 (diagnostic, default OFF): assert that no mark survives to a decision
// boundary (the backstop flushes in ApplyPlanDirect / AIEngine::TakeTurn / GameEngine::MainPhase
// must find nothing to do -- if one does, a flush site is MISSING and the trigger resolved late).
inline bool VerifyOn() { static const bool v = EnvOn("MTG_DMG_EVENT_VERIFY"); return v; }

inline const CardDefinition* Def(const Permanent& p)
{ return p.def_absent ? nullptr : CardDatabase::Instance().LookupCached(p.card); }

inline int CountTamanoa(const GameState& s, int ctrl)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != ctrl) { continue; }
        const CardDefinition* d = Def(p);
        if (d && d->params.noncreature_damage_lifegain) { ++n; }
    }
    return n;
}

// Purity protects its CONTROLLER ("If noncombat damage would be dealt to you").
inline bool PurityProtects(const GameState& s, int player)
{
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != player) { continue; }
        const CardDefinition* d = Def(p);
        if (d && d->params.prevent_noncombat_to_self_gain) { return true; }
    }
    return false;
}

// Every Manabarbs on the battlefield (any controller -- "Whenever a player taps a land"), as the
// per-instance damage amounts. Each instance is its own trigger and its own damage event.
inline int ManabarbsAmounts(const GameState& s, int* out, int cap)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    {
        const CardDefinition* d = Def(p);
        if (d && d->params.land_tap_damage_each_player > 0 && n < cap)
        { out[n++] = d->params.land_tap_damage_each_player; }
    }
    return n;
}

inline bool OpponentLostRaw(const GameState& s)
{ return s.Opponent().HasLost() || s.opponent_decked; }

// The won-lock (point 4): the opponent has lost and we have not -> the game is over.
inline bool WonLocked(const GameState& s)
{ return OpponentLostRaw(s) && s.ActivePlayer().life > 0; }

inline void Log(const GameState& s, const char* kind, const std::string& text)
{
    if (g_play_event_sink != nullptr && !TapSpeculatingNow()) { EmitPlayEvent(s.turn_number, kind, text); }
}

// CR 704.5a / 704.5b / 104.4a. Returns true when the game is OVER (we lost, the opponent lost, or
// both -- a draw). Our loss is made sticky by parking our life at kLostLife. Called only on armed
// paths; an unarmed deck never reaches it.
inline bool SbaCheckpoint(GameState& s)
{
    Player& me = s.ActivePlayer();
    if (me.life <= 0)
    {
        if (me.life > kLostLife)
        {
            Log(s, "lifeloss", OpponentLostRaw(s)
                ? std::string("☠ both players at 0 or less life -- the game is a DRAW (CR 104.4a)")
                : std::string("☠ you are at ") + std::to_string(me.life)
                      + " life -- you LOSE (state-based action, CR 704.5a)");
            me.life = kLostLife;
        }
        return true;
    }
    return OpponentLostRaw(s) || s.player_lost_on_draw;
}

// Record one mana tap on an armed board, in place of the legacy `life -= pain`. `pain` is what this
// tap's MODE deals (0 for a painland's separate {C} ability, or a painless land). Pain is dealt now
// (a mana ability's damage is immediate; City of Brass's is technically a trigger, but it too
// resolves before the spell being paid for, which is all the engine can observe) unless Purity
// prevents it; the triggers it causes are only RECORDED, for FlushDamageEvents.
inline void ArmedManaTap(GameState& s, int ctrl, Permanent& p, int pain)
{
    std::uint8_t m = p.mana_tap_mark;
    if (p.card.IsLand()) { m |= kMarkLand; }
    if (pain > 0 && !WonLocked(s))
    {
        if (PurityProtects(s, ctrl)) { m |= kMarkPrevented; }
        else                         { s.players[ctrl].life -= pain; }
        const int tot = std::min(kMarkPainMax, (m >> kMarkPainShift) + pain);
        m = static_cast<std::uint8_t>((m & 3u) | (tot << kMarkPainShift));
    }
    p.mana_tap_mark = m;
}

// A land tapped for mana outside the pain helpers (a filter land's {T}: Add {C}) -- Manabarbs only.
inline void MarkLandTap(GameState& s, Permanent& p)
{
    if (!s.dmg_events_armed || !p.card.IsLand()) { return; }
    p.mana_tap_mark |= kMarkLand;
}

// Would this tap deal pain? The ONE predicate for "which mode hurts" on an armed board:
// tap_self_damage_any_mode (Ancient Tomb) always hurts; otherwise a {C} tap of a land that has a
// real {C} mode is the painland's separate painless ability.
inline int PainForTap(const CardDefinition& def, Color col, bool has_c_mode)
{
    if (def.params.tap_self_damage <= 0) { return 0; }
    if (def.params.tap_self_damage_any_mode) { return def.params.tap_self_damage; }
    return (col == Color::Colorless && has_c_mode) ? 0 : def.params.tap_self_damage;
}

// ApplyLifegainReplacements -- CR 616.1: the affected player orders the replacement effects, and
// taking Bilbo's +1 BEFORE Faithmender's doubling is always at least as good ((a+1)*2 > a*2+1).
// Several Faithmenders multiply (x4 for two). Armed-only; identity otherwise.
inline int ApplyLifegainReplacements(const GameState& s, int player, int amt)
{
    if (!s.dmg_events_armed || amt <= 0) { return amt; }
    long long plus = 0, mult = 1;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != player) { continue; }
        const CardDefinition* d = Def(p);
        if (!d) { continue; }
        plus += d->params.lifegain_plus;
        if (d->params.lifegain_multiplier > 1)
        { mult = std::min<long long>(mult * d->params.lifegain_multiplier, 1 << 20); }
    }
    const long long r = (static_cast<long long>(amt) + plus) * mult;
    return static_cast<int>(std::min<long long>(r, 1 << 24));
}

// Resolve queued lifegain events, then queued Manabarbs damage events, with an SBA check before
// anything and after every event. `ctrl` is the gaining / tapping player.
inline int DealDamageEvent(GameState& s, int src_ctrl, bool src_is_creature, bool combat,
                           int to_self, int to_opp, int to_creatures_total, const char* src,
                           int tamanoa_lki = -1);

inline void ResolveQueued(GameState& s, int ctrl, const std::vector<int>& gains,
                          const std::vector<int>& barbs)
{
    if (SbaCheckpoint(s)) { return; }
    for (int g : gains)
    {
        GainLife(s, ctrl, g);
        if (SbaCheckpoint(s)) { return; }
    }
    for (int b : barbs)
    {
        DealDamageEvent(s, ctrl, /*src_is_creature=*/false, /*combat=*/false,
                        /*to_self=*/b, 0, 0, "Manabarbs");
        if (SbaCheckpoint(s)) { return; }
    }
}

// ONE damage EVENT from one source (CR 120): `to_self` to the source's controller, `to_opp` to the
// opponent, `to_creatures_total` summed over every creature it hit (the caller applied that part
// and ran its own creature SBAs). Purity prevents the controller's own NONCOMBAT share and gains
// that much instead (a replacement: it happens now, before the SBA check); the prevented share is
// NOT dealt, so it does not count toward Tamanoa. A NONCREATURE source then triggers every Tamanoa
// its controller controls ONCE for the event's TOTAL -- `tamanoa_lki` lets a sweeper pass the count
// from before its own creature damage (a Tamanoa killed by the event still triggers: leaves-the-
// battlefield look-back, CR 603.10a). Returns the damage actually dealt.
inline int DealDamageEvent(GameState& s, int src_ctrl, bool src_is_creature, bool combat,
                           int to_self, int to_opp, int to_creatures_total, const char* src,
                           int tamanoa_lki)
{
    if (!s.dmg_events_armed) { return 0; }
    if (WonLocked(s) || s.ActivePlayer().life <= 0) { return 0; }   // the game is already over
    const int opp = 1 - src_ctrl;
    int dealt = to_creatures_total;
    std::vector<int> gains;
    if (to_self > 0)
    {
        if (!combat && PurityProtects(s, src_ctrl))
        {
            GainLife(s, src_ctrl, to_self);   // replacement: immediate, not a trigger
            Log(s, "lifegain", std::string("✚ Purity prevents ") + std::to_string(to_self)
                               + " damage from " + src + " and you gain that much");
        }
        else
        {
            s.players[src_ctrl].life -= to_self;
            dealt += to_self;
            Log(s, "lifeloss", std::string("🩸 ") + src + " deals " + std::to_string(to_self)
                               + " damage to you");
        }
    }
    if (to_opp > 0)
    {
        s.players[opp].life -= to_opp;
        s.opponent_lost_life_this_turn = true;
        dealt += to_opp;
    }
    if (!src_is_creature && dealt > 0)
    {
        const int t = tamanoa_lki >= 0 ? tamanoa_lki : CountTamanoa(s, src_ctrl);
        for (int k = 0; k < t; ++k) { gains.push_back(dealt); }
        if (t > 0)
        {
            Log(s, "lifegain", std::string("✚ Tamanoa: ") + src + " dealt " + std::to_string(dealt)
                               + (t > 1 ? " -- " + std::to_string(t) + " triggers" : std::string()));
        }
    }
    ResolveQueued(s, src_ctrl, gains, {});
    return dealt;
}

// Turn every mana_tap_mark on the board into its triggers and resolve them (see THE MODEL, step 2).
// Idempotent: a board with no marks is a no-op, so a redundant flush (the backstops) costs one scan.
inline void FlushDamageEvents(GameState& s)
{
    if (!s.dmg_events_armed) { return; }
    const int ctrl = s.active_player_index;
    int barb_amt[16];
    const int nbarb = ManabarbsAmounts(s, barb_amt, 16);
    const int tam   = CountTamanoa(s, ctrl);
    std::vector<int> gains, barbs;
    bool any = false;
    int prevented = 0, pain_events = 0, land_taps = 0;
    for (Permanent& p : s.battlefield)
    {
        if (p.mana_tap_mark == 0) { continue; }
        any = true;
        const std::uint8_t m = p.mana_tap_mark;
        p.mana_tap_mark = 0;
        const int pain = m >> kMarkPainShift;
        if (pain > 0)
        {
            if (m & kMarkPrevented) { gains.push_back(pain); ++prevented; }
            else { for (int k = 0; k < tam; ++k) { gains.push_back(pain); } ++pain_events; }
        }
        if ((m & kMarkLand) && p.controller_index == ctrl)
        {
            ++land_taps;
            for (int k = 0; k < nbarb; ++k) { barbs.push_back(barb_amt[k]); }
        }
    }
    if (!any) { return; }
    if (g_play_event_sink != nullptr && !TapSpeculatingNow() && (!gains.empty() || !barbs.empty()))
    {
        std::string t;
        if (pain_events > 0 && tam > 0)
        { t += "Tamanoa x" + std::to_string(tam) + " on " + std::to_string(pain_events) + " pain tap(s)"; }
        if (prevented > 0) { t += (t.empty() ? "" : "; ") + std::string("Purity prevented ") + std::to_string(prevented) + " pain tap(s)"; }
        if (!barbs.empty()) { t += (t.empty() ? "" : "; ") + std::string("Manabarbs x") + std::to_string(nbarb) + " on " + std::to_string(land_taps) + " land tap(s)"; }
        EmitPlayEvent(s.turn_number, "trigger", "⚡ " + t);
    }
    ResolveQueued(s, ctrl, gains, barbs);
}

// A decision-boundary BACKSTOP: flush anything a flush site missed. Under MTG_DMG_EVENT_VERIFY a
// backstop that finds work reports it -- that is a missing flush site (the triggers resolved late).
inline void BackstopFlush(GameState& s, const char* site)
{
    if (!s.dmg_events_armed) { return; }
    if (VerifyOn())
    {
        for (const Permanent& p : s.battlefield)
        {
            if (p.mana_tap_mark != 0)
            {
                std::fprintf(stderr, "[dmg-event-verify] UNFLUSHED mark on %s at %s (turn %d)\n",
                             p.card.m_name.str().c_str(), site, s.turn_number);
                std::abort();
            }
        }
    }
    FlushDamageEvents(s);
}

// Does this deck have a GAIN ENGINE for our own damage right now: Purity (the damage becomes
// lifegain outright) or a Tamanoa (it is paid back, and more with a Faithmender)? The default the
// PreventDamageProvider answers DecisionProvider::SelfDamageUseful with.
inline bool SelfDamageGainEngine(const GameState& s, int ctrl)
{
    if (!s.dmg_events_armed) { return false; }
    return PurityProtects(s, ctrl) || CountTamanoa(s, ctrl) > 0;
}

// The subset enumerators' "a plan that kills us via its own cast triggers cannot win" bill
// (Eidolon / Spellshock). Unarmed: the plain SUM (byte-identical to the historical test). Armed, it
// is exact per EVENT: each trigger is its own damage event with an SBA check before its gain, so
// with Purity out nothing lands at all, and with a Tamanoa out every hit is paid back before the
// next one -- only the largest SINGLE hit has to fit under our life. (Optimistic only if the plan
// itself kills the Tamanoa mid-line; the own-death machinery still scores that line correctly.)
inline int CastTriggerBill(const GameState& s, int sum, int max_single)
{
    if (!s.dmg_events_armed) { return sum; }
    const int ctrl = s.active_player_index;
    if (PurityProtects(s, ctrl)) { return 0; }
    return CountTamanoa(s, ctrl) > 0 ? max_single : sum;
}

// Does the FIRST land we tap for mana this payment kill us? Manabarbs ("whenever a player taps a
// land for mana, deals 1 damage to that player", ANY controller's copy) resolves each instance as its
// own damage event with an SBA check before its Tamanoa gain -- so with a Tamanoa out the first 1-point
// hit must fit under our life, and without one every instance of that one tap lands first. The land's
// own pain is ignored (a painless {C} mode may exist), so this is a LOWER bound on the bill: true
// means certain death on the first tap, never a guess. Purity prevents it all. A non-land mana source
// we control (a dork, a rock) could pay without tapping a land, so any untapped one voids the claim.
// Floating mana is the caller's to net out. Armed boards only.
inline bool FirstLandTapKills(const GameState& s, int ctrl)
{
    if (!s.dmg_events_armed || PurityProtects(s, ctrl)) { return false; }
    int amts[16];
    const int n = ManabarbsAmounts(s, amts, 16);
    if (n == 0) { return false; }
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != ctrl || p.tapped || p.card.IsLand()) { continue; }
        const CardDefinition* d = Def(p);
        if (d && !d->params.produces.empty()) { return false; }
    }
    int bill = 0, first = 0;
    for (int i = 0; i < n; ++i) { bill += amts[i]; first = std::max(first, amts[i]); }
    return (CountTamanoa(s, ctrl) > 0 ? first : bill) >= s.players[ctrl].life;
}

// RULES SAFETY of paying with pain, for a whole payment: even if EVERY pain land we can still tap in
// this payment were tapped in its damaging mode, the pain alone must not take us to 0 before the
// Tamanoa triggers resolve (the SBA runs first). Purity makes it always safe. Evaluated on the
// START-OF-PAYMENT state reconstructed from the marks (life + pain already recorded; lands untapped
// or tapped by this payment), so the answer cannot flip mid-payment -- which is what lets the mana
// cache key it (SpellEffects.cpp) and keeps the greedy and the backtracker agreeing tap by tap.
inline bool PaymentPainSafe(const GameState& s, int ctrl)
{
    if (PurityProtects(s, ctrl)) { return true; }
    int life0 = s.players[ctrl].life;
    int potential = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != ctrl) { continue; }
        if (p.mana_tap_mark != 0 && !(p.mana_tap_mark & kMarkPrevented))
        { life0 += p.mana_tap_mark >> kMarkPainShift; }
        if (p.tapped && p.mana_tap_mark == 0) { continue; }
        const CardDefinition* d = Def(p);
        if (d && d->params.tap_self_damage > 0) { potential += d->params.tap_self_damage; }
    }
    return life0 - potential > 0;
}

// THE payment-mode question: should a generic pip on a painland take the DAMAGING coloured mode?
// The provider decides whether our damage is useful (a judgement, provider-owned); the engine adds
// the rules-safety bound. Armed-only; false for every other deck without asking anything.
inline bool PayWithPain(const GameState& s, int ctrl)
{
    if (!s.dmg_events_armed) { return false; }
    return ResolveProvider(s).SelfDamageUseful(s, ctrl) && PaymentPainSafe(s, ctrl);
}
}   // namespace dmgev

// The own-death predicate (CR 704.5a + a draw from an empty library).
inline bool SelfHasLost(const GameState& s)
{ return s.ActivePlayer().life <= 0 || s.player_lost_on_draw; }
