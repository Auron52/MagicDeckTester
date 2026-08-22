# MagicDeckTester

Simulate *Magic: The Gathering* games to find out whether a deck change actually helps.

Swap a card, change a land count, try 3 copies instead of 2 — then play the deck thousands of
times and compare. The headline metric is **average turn to win** (lower is better); games that
don't win inside the turn limit count as a loss.

There are two ways to use it:

- **Play a game yourself** in a browser, against the same engine — a real board with card art,
  where you build each turn by hand. Good for seeing how a deck actually runs, and for checking
  the AI's judgement against your own.
- **Run the simulator** over thousands of games and read the numbers.

---

## Quickstart

You need a **C++20 compiler**, **CMake**, **Git**, and **Node.js** (for the browser viewer).
Full per-OS instructions with install commands are in **[docs/INSTALL.md](docs/INSTALL.md)**.

### Linux / macOS

```bash
git clone https://github.com/Auron52/MagicDeckTester.git
cd MagicDeckTester
./play.sh
```

### Windows

```bat
git clone https://github.com/Auron52/MagicDeckTester.git
cd MagicDeckTester
play.cmd
```

That's it. `play.sh` / `play.cmd` builds the engine if it isn't built yet, starts a local
server, and opens <http://localhost:8080>. Pick a deck, hit **New game**, and play.

> First run takes a few minutes — CMake downloads a few small libraries and compiles the
> engine. After that it starts instantly.

To build without playing:

```bash
./build.sh          # Linux/macOS  -> build/Release/mtg
build.cmd           # Windows      -> build\Release\mtg.exe
```

## Run the simulator

```bash
./build/Release/mtg decks/burn/burn.txt --games 1000 --seed 1001
```

```
Seed          : 1001
Games played  : 1000
avg (turns)   : 4.3650    [mean turn-to-win, unwon = max_turns+1; lower is better]
```

The deck's tuning profile next to the decklist is picked up automatically. Useful options:
`--games N`, `--seed S`, `--max-turns T`, `--threads N`, `--log-dir path` (writes one JSON log
per game). Run `mtg` with no arguments for the full list.

Decks live one per folder under [`decks/`](decks/), each holding the decklist plus its
generated profile. Around 20 decks ship with the repo.

## Add or change a deck

Drop a `.txt` or Cockatrice `.cod` decklist into `decks/<name>/<name>.cod`, then:

```bash
python3 scripts/analyze_deck.py decks/<name>/<name>.cod --coverage-only   # what's missing?
python3 scripts/analyze_deck.py decks/<name>/<name>.cod                   # build its profile
```

The coverage pass reports any card the engine doesn't implement yet. Implementing one means
adding an entry to [`src/cards/data/cards.json`](src/cards/data/cards.json) — it's data, not
code, for most cards.

---

## For developers

| Topic | Where |
|---|---|
| Install prerequisites, per OS | [docs/INSTALL.md](docs/INSTALL.md) |
| The play viewer — protocol, checks, scope | [tools/play/README.md](tools/play/README.md) |
| Engineering record: designs, measurements, dead ends | [docs/design/README.md](docs/design/README.md) |
| Reproducible Linux dev container | [.devcontainer/README.md](.devcontainer/README.md) |
| Regression suite | `test/regression.sh --smoke` (~1 min), `--regression`, `--overnight` |
| Unit tests | `ctest --test-dir build -C Release` |

Builds are **always optimized** — `build.sh` / `build.cmd` are the supported entry points, and
there is deliberately no `-O0` mode. Both configure through
[`CMakePresets.json`](CMakePresets.json), so Visual Studio and VS Code's CMake Tools also work
by just opening the folder.

The engine is **bit-portable**: the same seed produces the same games on Linux and Windows.
CI ([`.github/workflows/build.yml`](.github/workflows/build.yml)) builds on both and asserts
they agree.

This project is developed largely with AI assistance. Instructions and conventions for agents
working in this repo live in [CLAUDE.md](CLAUDE.md) and `.claude/skills/` — humans can ignore
those; nothing in the workflow above depends on them.

## License

GNU General Public License v3 — see [LICENSE](LICENSE).

*Magic: The Gathering* is a trademark of Wizards of the Coast. This is an unaffiliated fan
project; card art shown in the viewer is fetched live from [Scryfall](https://scryfall.com/).
