# Coding Conventions

Repo-specific C++ conventions. Read this before adding an env flag, a debug toggle, or an
A/B lever. (For MTG rules correctness see `mtg-rules.md`; for AI/search patterns see `mtg-ai.md`.)

## Environment flags (`MTG_*`)

### The truthiness rule — one convention, everywhere

Every **boolean** flag is read through the helpers in `src/core/EnvFlags.h`:

| Variable state | `EnvOn("MTG_X")` | `EnvOn("MTG_X", true)` (default-ON flag) |
|---|---|---|
| unset | off | on |
| `MTG_X=` (empty) | off | on |
| `MTG_X=0` | **off** | **off** |
| `MTG_X=1` (or any other value) | on | on |

So `=0` always means off and `=1` always means on, for every flag. This was unified in the
A3 sweep (2026-07-30); before it, 88 flags were presence-only (`getenv(...) != nullptr`),
under which `MTG_X=0` meant **ON** — that is how `MTG_MAGMA_FAITHFUL=0` once silently enabled
the mode an A/B arm believed it was disabling.

### Rules when adding or touching a flag

1. **Never write `std::getenv("MTG_X") != nullptr` (or `== nullptr`) as a truthiness read.**
   Use `EnvOn("MTG_X")`; for a default-ON flag with a `=0` hatch, `EnvOn("MTG_X", true)`.
   Cache in a function-local `static const bool` if the site is hot (the existing style).
2. **`EnvSet("MTG_X")` is only for value-pinning detection** — the pattern where the same
   variable carries a numeric value and `0` is a *legal value*, so "did the user set it at
   all?" is a different question from its value (e.g. `MTG_SHUFFLE_SALT_SEARCH=0` pins salt
   0, distinct from unset). Never use `EnvSet` to gate behaviour on/off.
3. **Value-carrying flags keep their raw `getenv` + parse** (`EnvInt(key, dflt)` for the
   simple integer case). If the value doubles as the on/off switch (e.g. `MTG_LOG_HAND`,
   whose value is the hand composition and `"0"` means off), keep the explicit
   `e && *e && std::string(e) != "0"` form and say so in a comment — `EnvOn` would discard
   the value.
4. **A flag read by both the executor (`AIEngine`) and the rollout (`TurnSolver`) gets ONE
   shared reader in `src/ai/EngineFlags.h`** — never a per-TU static copy in each file.
   Two copies is two chances to update one and not the other, which is the
   executor/rollout lockstep failure mode in miniature.
5. **Document the default at the read site**: `// DEFAULT ON; =0 disables` or the flag's
   purpose in one line. An adopted change keeps a `MTG_LEGACY_*` escape hatch; an
   experiment keeps its lever until the experiment's outcome is recorded in
   `docs/design/` — then delete the losing branch (with user sign-off, per backlog D2).
6. `heuristic_defaults.env` lines are applied with these same semantics, so "set the var
   to the baseline to disable" is now literally correct: `MTG_X=0` disables a default-off
   flag's adoption.

### The rule crosses the language boundary

A Python/JS tool that MIRRORS an engine flag must mirror its **default** too, not just its name.
`os.environ.get("MTG_X")` returning `None` means "unset", which for a default-ON lever means the
engine has it **ON** — so a presence-read (`bool(v)`, `v is not None`) leaves the tool off exactly
when the binary is on, and the two then disagree about the same run.

Measured cost (2026-08-26): `MTG_TREASURE_PAY_SOURCE` is `EnvOn(..., true)` in the engine, while
`test/viewer_protocol_check.py` gated its compat tier on `bool(_tps) and _tps != "0"`. On an
ordinary run the engine treated a Treasure as a payment source (the crack is implicit, not an
enumerated action) and the checker still expected the recorded plan to name it — so a user-owned
reference reported a false **ENUM-GAP** ("the engine no longer offers this plan for the same
state"), which is the checker's loudest category and points investigation at the enumerator rather
than at the tool. It is the same presence-only defect as rule 1, one language over.

The mirror forms, matching the table above:

| engine read | Python mirror |
|---|---|
| `EnvOn("MTG_X")` | `os.environ.get("MTG_X", "") not in ("", "0")` |
| `EnvOn("MTG_X", true)` | `os.environ.get("MTG_X") != "0"` |

Grep for `environ.get("MTG_` when converting a flag's default: the tools do not fail the build when
they fall out of step, they just quietly disagree.

### Testing a flag change

Flags are unset in the harness environment, so the regression suite alone cannot prove a
truthiness change is safe. The A3 verification pattern:

- clean-env smoke → must be byte-identical (digests unchanged);
- `MTG_<some_converted_flag>=0` smoke → must ALSO be byte-identical (the `=0`-means-off
  semantics working);
- `MTG_<same_flag>=1` smoke → must diverge (the lever still functions).

## General

- Build only via `./build.sh` (see CLAUDE.md — the `-O0` footgun).
- Comment density/style: match the surrounding file; this codebase explains *why* at the
  site (measurement results, user decisions, lockstep warnings), not *what*.
- Behaviour-affecting change → its own commit with its own digest check; never batch two
  "obviously safe" refactors into one commit (bisecting a digest move across a batch is
  expensive).
