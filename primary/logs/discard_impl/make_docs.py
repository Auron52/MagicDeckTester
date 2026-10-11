#!/usr/bin/env python3
"""One-shot: write the USER-settled 2026-10-09 discard designs into docs/design.

Reads the review record (plan file) by line ranges, copies each WIP proposal off
wip/discard-policy-gate-2026-09-23, and prepends the settled block. Run from the repo root.
"""
import re
import subprocess
import sys
from pathlib import Path

PLAN = Path("/home/vscode/.claude/plans/which-decks-need-a-parallel-hinton.md")
REPO = Path("/workspaces/MagicDeckTester")
WIP = "wip/discard-policy-gate-2026-09-23"
lines = PLAN.read_text().splitlines()


def rng(a, b):
    return lines[a - 1:b]


def demote(block):
    out = []
    for ln in block:
        if ln.startswith("### "):
            out.append("#### " + ln[4:])
        elif ln.startswith("## "):
            out.append("### " + ln[3:])
        else:
            out.append(ln)
    return out


HEADER = "## USER-revised design (2026-10-09) -- SUPERSEDES the proposal below"
PREAMBLE = [
    "",
    "The USER reviewed this proposal on 2026-10-09, one deck at a time, and amended it. Everything in",
    "this section is the review record: USER rulings are quoted verbatim and marked USER; choices an",
    "agent made where the USER had not ruled are marked \"agent default\" / \"agent reading\" and are",
    "surfaced for the USER to overturn. Where this section and the proposal below disagree, THIS",
    "section wins. The `#### <deck>: SETTLED` block is the design to implement; the lines above it",
    "record how the review got there (proposal summary, questions, answers).",
    "",
    "Implementation contract (all decks): a per-deck `XProvider::CleanupDiscardCandidates` override",
    "returning the full shed order (most expendable first, every non-staged hand card named), routed",
    "through `CleanupDiscardRankingWithOrder`, behind a default-ON `EnvOn(\"MTG_<DECK>_BUCKET_DISCARD\",",
    "true)`; `=0` restores `GenericProvider::CleanupDiscardCandidates` (max-MV). Quotas count the board",
    "(\"net of board\"); within a bucket, distance to castable orders the cards (EV = P(play) x value).",
    "",
]

DECKS = {
    "angels": [(76, 105)],
    "breaching-dragonstorm": [(106, 121)],
    "burn": [(122, 152)],
    "creature-giving": [(153, 167)],
    "critterlifegain": [(168, 184)],
    "dragonstorm": [(185, 218)],
    "eldrazi-flicker": [(219, 248)],
    "auras": [(249, 285)],
    "fluctuator": [(286, 343)],
    "giants": [(344, 388)],
    "goblins": [(389, 442)],
    "knights": [(443, 489)],
    "melira-pod": [(490, 533)],
    "slivers-vial": [(534, 577)],
}

AGENT_DEFAULTS = rng(56, 66)


def block_for(ranges):
    out = []
    for a, b in ranges:
        out.extend(demote(rng(a, b)))
        out.append("")
    return out


def check_ranges():
    # every range must start on a "## " header line of the plan
    for slug, rs in DECKS.items():
        for a, _ in rs:
            if not lines[a - 1].startswith("## "):
                sys.exit(f"range for {slug} does not start on a header: {lines[a-1]!r}")


check_ranges()
for slug, ranges in DECKS.items():
    path = f"docs/design/{slug}-discard-policy-proposal.md"
    proposal = subprocess.run(["git", "-C", str(REPO), "show", f"{WIP}:{path}"],
                              check=True, capture_output=True, text=True).stdout
    plines = proposal.splitlines()
    # keep the proposal's own title line first, insert the USER section right after it
    title = plines[0] if plines and plines[0].startswith("# ") else f"# {slug} discard policy"
    rest = plines[1:] if plines and plines[0].startswith("# ") else plines
    out = [title, "", HEADER] + PREAMBLE + block_for(ranges)
    out += ["#### Agent defaults across the 2026-10-09 review (all decks; surfaced, USER may overturn)", ""]
    out += [ln for ln in AGENT_DEFAULTS if not ln.startswith("## ")]
    out += ["", "---", "", "## The original proposal (2026-09-23, AI-authored; superseded where the section above differs)", ""]
    out += rest
    (REPO / path).write_text("\n".join(out).rstrip() + "\n")
    print("wrote", path)

# Fungus: append section 6
fpath = REPO / "docs/design/fungus-cleanup-discard.md"
ftxt = fpath.read_text().rstrip() + "\n"
if "Settled gaps (2026-10-09)" not in ftxt:
    sec = ["", "## 6. Settled gaps (2026-10-09)", "",
           "The section-5 buckets were documented 2026-09-24 but never implemented (no Fungus discard code",
           "existed; the generic max-MV ranking still ran). On 2026-10-09 the USER filled the remaining gap --",
           "which bucket gives way when every bucket is within target and the hand is still over seven:", ""]
    sec += demote(rng(578, 591)) + [""] + demote(rng(631, 646))
    fpath.write_text(ftxt + "\n".join(sec).rstrip() + "\n")
    print("appended", fpath)

NEW_PRE = [
    "",
    "No AI proposal existed for this deck; the design below was drafted by an agent during the",
    "2026-10-09 review and settled by the USER the same day. USER rulings are quoted and marked USER;",
    "agent choices are marked \"agent default\" and surfaced for the USER to overturn.",
    "",
]
for slug, title, ranges in [
    ("selesnyalifegain", "# SelesnyaLifegain cleanup-discard policy (USER-settled 2026-10-09)", [(605, 630), (647, 658)]),
    ("whiteknights", "# WhiteKnights cleanup-discard policy (USER-settled 2026-10-09)", [(592, 604)]),
]:
    path = REPO / f"docs/design/{slug}-discard-policy.md"
    out = [title] + NEW_PRE + PREAMBLE[8:] + block_for(ranges)
    path.write_text("\n".join(out).rstrip() + "\n")
    print("wrote", path)
