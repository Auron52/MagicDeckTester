# Dev Container

A reproducible Linux C++20 environment for MagicDeckTester, for VS Code + Docker.

## Open it

1. Install Docker Desktop and the VS Code **Dev Containers** extension
   (`ms-vscode-remote.remote-containers`).
2. Open this folder in VS Code → **Reopen in Container** (or run
   *Dev Containers: Reopen in Container* from the command palette).

On first creation the container will:

- build a one-layer image on `mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04`
  (gcc-13, gdb, cmake, git; the layer only pre-creates `~/.vscode-server` — see the
  Dockerfile header),
- `apt-get install` Ninja and Python (`onCreateCommand`),
- configure CMake into `build/` with the **Ninja Multi-Config** generator
  (`postCreateCommand`). This step **downloads pugixml + nlohmann_json** via
  `FetchContent`, so it needs network access the first time.

## Build & run

```bash
cmake --build build --config Release          # or Debug
./build/Release/mtg <deck>.txt --games 500 --seed 1001
./build/Release/mtg-analyze <deck>.txt --cards-json src/cards/data/cards.json
```

The tooling scripts work unchanged inside the container — they auto-detect the
Linux binary names:

```bash
python scripts/analyze_deck.py decks/treasure_hunt.txt --coverage-only
bash   test/regression.sh --smoke
```

VS Code's **CMake Tools** is preconfigured (same generator + `build/` dir), so the
*Build* / *Run* / *Debug* buttons also work, with IntelliSense driven by
`build/compile_commands.json`.

## Card workflow (implement / edit / audit)

Adding or changing cards works fully in the container — the three stages from
`.claude/skills/analyze-deck.md`:

1. **Coverage check** — list missing or partially-implemented cards:
   ```bash
   python scripts/analyze_deck.py <deck>.txt --coverage-only
   ```
2. **Implement / edit** — add or fix entries in `src/cards/data/cards.json` (Claude
   uses `.claude/skills/mtg-rules.md` for correct ability type, timing, targeting).
3. **Audit costs against Scryfall** — catch a transcribed miscost mechanically:
   ```bash
   python scripts/audit_card_costs.py        # defaults to src/cards/data/cards.json
   ```
   This is the only step that uses the network (`api.scryfall.com`), which the
   firewall allows. If it ever times out (Scryfall's Cloudflare IPs can rotate when
   the container has been up a long time), refresh the allowlist:
   ```bash
   sudo bash .devcontainer/init-firewall.sh
   ```
4. **Analyze** — rebuild and regenerate the deck profile:
   ```bash
   python scripts/analyze_deck.py <deck>.txt
   ```

## Claude Code in the container

The Claude Code CLI is installed (Node feature + `npm i -g @anthropic-ai/claude-code`
in `postCreateCommand`) and `api.anthropic.com` is allowlisted in the firewall. To
use it:

```bash
claude        # first run: type /login and open the printed URL in your host browser
```

The browser login happens on your **host**; the container only talks to
`api.anthropic.com`. Your credentials are stored under `~/.claude`, which is a
persisted Docker volume (`mdt-claude`), so you authenticate **once** and it survives
rebuilds. To sign out / reset, remove the volume: `docker volume rm mdt-claude`.

Running Claude *inside* the container lets it drive the Linux/GCC toolchain directly
(build, run `./build/Release/mtg`, run the regression suite). You can still run Claude
on the Windows host against the same files — the two don't conflict.

Non-essential traffic (telemetry, error reporting, auto-updates) is disabled via
`CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1` so Claude doesn't repeatedly hit hosts
the firewall blocks. (That also pins the version — to upgrade, rebuild the container,
which reinstalls the latest via npm.)

See "Editor extensions behind the firewall" below for the Claude **VS Code
extension** (vs. the CLI used here).

## Git: committing & pushing as dtippett-bot

This repo is configured (repo-local) so commits are authored/committed as
**dtippett-bot `<dtippett@agentmail.to>`**, and its github.com credential helper is
`gh` — so pushes authenticate as whoever `gh` is logged in as. To make the container
push as the bot (instead of VS Code forwarding the host's account), authenticate the
container's `gh` once:

```bash
gh auth login        # choose GitHub.com -> HTTPS -> log in as dtippett-bot
```

The browser step runs on your **host**; the container only reaches `github.com`
(allowlisted). The login persists in the `mdt-gh` volume, so it survives rebuilds
(`docker volume rm mdt-gh` to reset). After that, `git commit`/`git push` — and Claude
running in the container — act entirely as dtippett-bot; the host's Auron52 account is
never involved here. Verify with:

```bash
gh api user --jq .login                 # -> dtippett-bot
git ls-remote origin >/dev/null && echo OK
```

## Editor extensions behind the firewall

The extensions listed in `devcontainer.json` (`anthropic.claude-code`,
`ms-vscode.cpptools-extension-pack`, `ms-python.python`) install into the container's
VS Code Server, which downloads them from the **Marketplace**. The Marketplace uses
wildcard CDN hosts (`*.gallerycdn.vsassets.io`, `*.vscode-cdn.net`, …) on shared,
rotating IPs that the IP-allowlist firewall can't cleanly pin — so they will **not**
auto-install while the firewall is locked down.

The approach: **install them with egress briefly lifted**, then re-lock. There's a
script that does the whole thing — run it from a container terminal:

```bash
bash .devcontainer/install-extensions.sh
```

It lifts the firewall, runs `code --install-extension` for each, and re-applies the
firewall on exit (via a `trap`, so egress is restored even if an install fails or you
Ctrl-C). After it finishes, the Claude extension uses the same CLI + login above.

<details><summary>Manual equivalent</summary>

```bash
sudo iptables -P OUTPUT ACCEPT            # 1. open egress temporarily
```
Then install (Extensions view → "Install in Dev Container", or run `claude` once to
auto-install its own extension), then:
```bash
sudo bash .devcontainer/init-firewall.sh  # 2. re-apply the default-deny firewall
```
</details>

**Persistence:** this is a one-time step. Installed extensions are kept in the
`mdt-vscode-extensions` volume, so they survive both restarts and full rebuilds. (On
rebuild they load from the volume without touching the Marketplace — they just won't
auto-update; to refresh, briefly lift egress again and let VS Code update them.)

This persistence is why the project carries a one-line `Dockerfile`: it pre-creates
`~/.vscode-server` owned by `vscode` so the extensions volume mounts cleanly. Without
it, the volume makes that dir root-owned and the VS Code server's startup
(`mkdir ~/.vscode-server/bin`) fails before any lifecycle hook can fix ownership.

> Prefer not to lift the firewall at all? The `claude` **CLI** in the integrated
> terminal is the full experience and needs none of this. To allowlist the
> Marketplace permanently instead (looser egress), ask and I'll add the domains.

## How the build directory is isolated

The host's `build/` (a Windows Visual Studio cache) is bind-mounted into the
container along with the rest of the repo. To avoid a CMake "generator mismatch"
and to keep build artifacts fast, `devcontainer.json` mounts a Docker **volume**
(`mdt-build`) over `build/`. Inside the container `build/` is therefore a fresh,
Linux-only tree; your Windows build is shadowed (untouched) while the container is
open. Deleting the container does not affect the Windows `build/`. To wipe the
Linux build, remove the volume: `docker volume rm mdt-build`.

> The same collision applies to the optional `build-prof/` profiling tree. If you
> want `-DMTG_PROFILE=ON` builds in the container, add a second volume mount over
> `build-prof/` the same way.

## Profiling (perf / callgrind)

**WSL2 has no hardware PMU.** Under Docker Desktop's WSL2 kernel the CPU's hardware
performance counters are **not exposed to the guest** (`perf list` shows no
`cpu`/`cpu_core` PMU). So perf here has **no hardware events** — no `cycles`,
`instructions`, `cache-misses`, or `branch-misses` — and the *default*
`perf record`/`perf stat` (which sample `cycles`) capture nothing or error out on the
missing `cpu_core` topdown group. This is a virtualization limit, **not** a perf
version or `perf_event_paranoid` problem — building a kernel-matched perf would not
change it. (`perf` is the Ubuntu generic build, symlinked onto `PATH`; the container
has `--cap-add=PERFMON`. Both are fine; the PMU is simply absent.)

What works is **software-timer sampling** (`task-clock`) — PMU-free, and still a real
hotspot profile with call graphs. Build with debug info so symbols resolve, and name
the event explicitly:

```bash
cmake --build build --config RelWithDebInfo
perf record -e task-clock -F 999 -g --call-graph dwarf \
  ./build/RelWithDebInfo/mtg decks/<deck>.txt --games 200 --seed 1001
perf report                      # where CPU time goes, with call stacks
perf stat -e task-clock,context-switches,cpu-migrations,page-faults \
  ./build/RelWithDebInfo/mtg decks/<deck>.txt --games 200 --seed 1001
```

For everything else:
- **Exact instruction counts + call graph (deterministic, PMU-free)** —
  `valgrind --tool=callgrind` (already in the image; ~10–50× slower but byte-exact —
  the right tool for A/B comparisons, and what this repo's profiling history uses).
  The in-code `-DMTG_PROFILE=ON` build is the other deterministic option.
- **Real microarchitectural counters** (cycles, IPC, cache, branch-miss) — need a
  **native Linux host** (or a vPMU-enabled hypervisor, which Docker Desktop's WSL2
  does not provide).

## Egress firewall (default-deny)

The container restricts outbound network access. `init-firewall.sh` runs on every
start (`postStartCommand`) and sets a **default-DENY** egress policy, allowing only:

- loopback, DNS, and established/related replies;
- **GitHub** (its published IP ranges) — required, because CMake `FetchContent`
  clones pugixml + nlohmann_json on a fresh build, and for `git` push/pull;
- any host listed in `ALLOWED_HOSTS` at the top of `init-firewall.sh`.

This needs the `NET_ADMIN`/`NET_RAW` capabilities (granted via `runArgs`); they only
let `iptables` program the container's own namespace — they don't touch the host
network. The script verifies itself at the end (a blocked host must fail, GitHub
must succeed) and fails the start if egress was left open.

The firewall applies *after* `onCreateCommand` (apt) and `postCreateCommand` (the
initial CMake configure + dependency download), so first-time setup is unaffected;
the lockdown governs your interactive session and subsequent restarts.

**Scryfall is optional.** `ALLOWED_HOSTS` ships with `api.scryfall.com`, which is
used **only** by `scripts/audit_card_costs.py` (the mana-cost audit). The build, the
simulator (`mtg`), the analyzer (`mtg-analyze`), and the regression suite make no
network calls. If you won't run the cost audit in the container, delete that line
to deny it too. To allow another host, add its name to the list and restart (or run
`sudo bash .devcontainer/init-firewall.sh`). To temporarily lift the firewall:
`sudo iptables -P OUTPUT ACCEPT`.

## Caveat: results are not byte-identical to the Windows ground truth

`test/regression_gt.txt` was generated on **Windows / MSVC**. A **Linux / GCC**
build can legitimately differ (floating-point, hash ordering, threading), so
`test/regression.sh` may report mismatches *that are not regressions* when run in
the container. The suite still works as a self-consistent A/B harness here; just
don't expect the committed Windows fingerprints to validate. If you want a
Linux baseline, inspect a run and `regression.sh <mode> --accept` into a
**separate** ground-truth file rather than overwriting the committed one.
