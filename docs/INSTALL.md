# Installing prerequisites

MagicDeckTester builds from source. Nothing here needs administrator setup beyond installing
the tools themselves, and no step needs a package manager for the project's own dependencies —
CMake downloads those (pugixml, nlohmann_json, doctest, and zlib if your system lacks it).

| Tool | Needed for | Minimum |
|---|---|---|
| C++ compiler | the engine | C++20 — developed on GCC 13 and MSVC 2022; GCC ≥ 11 / Clang ≥ 14 should work |
| CMake | the build | 3.20 |
| Git | CMake downloads dependencies with it | any |
| Node.js | the browser play viewer | any current LTS |
| Python 3 | deck analysis (`scripts/`) | 3.9 |

Node and Python are each only needed for their own feature — the simulator itself needs
neither.

---

## Windows

Install the **Visual Studio 2022 Build Tools** with the *Desktop development with C++*
workload. That single workload provides the MSVC compiler, the Windows SDK, **and** CMake, so
it covers three rows of the table at once.

- Download: <https://visualstudio.microsoft.com/downloads/> → scroll to *Tools for Visual
  Studio* → *Build Tools for Visual Studio 2022*.
- (The full Visual Studio Community edition works too, if you'd rather have the IDE.)

Then the rest, most easily via `winget` in a terminal:

```bat
winget install Microsoft.VisualStudio.2022.BuildTools
winget install Git.Git
winget install OpenJS.NodeJS.LTS
winget install Python.Python.3.12
```

Close and reopen your terminal afterwards so the new tools are on `PATH`, then:

```bat
cd MagicDeckTester
play.cmd
```

**You do not need a Developer Command Prompt.** `build.cmd` uses the Visual Studio generator,
which locates the compiler itself. (If you *are* in a Developer Command Prompt and have Ninja
installed, the build automatically picks the faster Ninja route instead.)

**Why `.cmd` and not `.ps1`?** Windows defaults to a `Restricted` PowerShell execution policy
that blocks `.ps1` scripts outright. `build.cmd` and `play.cmd` are thin shims that invoke the
real PowerShell scripts with that policy bypassed, so they work from cmd.exe, from PowerShell,
and from a double-click in Explorer.

## Linux

Debian / Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git nodejs python3
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build git nodejs python3
```

Arch:

```bash
sudo pacman -S base-devel cmake ninja git nodejs python
```

Check your compiler is new enough — the engine is C++20:

```bash
g++ --version        # 11 or newer; the project is developed on 13
```

Then:

```bash
cd MagicDeckTester
./play.sh
```

**Optional, for speed:** `libjemalloc-dev` (Debian/Ubuntu). The search is allocation-bound, so
jemalloc is a meaningful speedup. CMake links it only if present, and it changes only *where*
memory lives — results are identical either way.

## macOS

Untested, but expected to work: the engine is plain C++20 with no platform-specific code
outside a couple of guarded spots, and its shuffle is deliberately platform-independent.

```bash
xcode-select --install                  # Apple Clang
brew install cmake ninja node python3
./play.sh
```

If you try it, reports either way are welcome.

## Dev container (any host with Docker)

The repo ships a reproducible Linux container with the whole toolchain preinstalled — useful
on Windows if you'd rather not install a C++ toolchain natively. See
[.devcontainer/README.md](../.devcontainer/README.md). Note that the container applies a
default-deny egress firewall; the play viewer's card art comes from `api.scryfall.com`, which
is on the allowlist.

---

## Verifying the install

```bash
./build.sh                                    # build.cmd on Windows
ctest --test-dir build -C Release             # unit tests
./build/Release/mtg decks/burn/burn.txt --games 200 --seed 1001 --threads 1
```

That last command should print:

```
avg (turns)   : 4.3650
```

**Exactly that number, on every platform.** The engine is bit-portable by design — a given
seed produces identical games on Linux and Windows (see the note in `src/core/Library.h`). If
you get a different value, something is genuinely wrong; please open an issue with your OS and
compiler version.

## Troubleshooting

**`'cmake' is not recognized` / `command not found`** — the tool isn't on `PATH`. On Windows,
reopen the terminal after installing; the installer updates `PATH` only for new sessions.

**`No CMAKE_CXX_COMPILER could be found`** — the compiler is missing, not CMake. On Windows
that means the *Desktop development with C++* workload wasn't selected; re-run the Visual
Studio Installer and add it.

**Build fails downloading dependencies** — CMake's FetchContent clones from GitHub over HTTPS,
so it needs network access on the first configure. Behind a proxy, set `HTTPS_PROXY`.

**`stale single-config CMake tree at build/Release/`** — someone ran a bare
`cmake -S . -B build/Release` at some point, which compiles *unoptimized* (~10× slower) and
collides with the real build tree. Delete that directory as the message says and re-run.

**The viewer says the simulator binary is missing** — the engine isn't built. Run `./build.sh`
(`build.cmd`), or just use `./play.sh` (`play.cmd`), which builds it for you.

**Port 8080 is already in use** — `PORT=9000 ./play.sh`, or on Windows
`$env:PORT=9000; .\play.cmd`.
