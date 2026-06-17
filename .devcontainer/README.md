# Dev Container

A reproducible Linux C++20 environment for MagicDeckTester, for VS Code + Docker.

## Open it

1. Install Docker Desktop and the VS Code **Dev Containers** extension
   (`ms-vscode-remote.remote-containers`).
2. Open this folder in VS Code → **Reopen in Container** (or run
   *Dev Containers: Reopen in Container* from the command palette).

On first creation the container will:

- pull `mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04` (gcc-13, gdb, cmake, git),
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
python scripts/analyze_deck.py treasure_hunt.txt --coverage-only
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
