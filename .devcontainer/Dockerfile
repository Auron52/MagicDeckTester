# Thin layer on Microsoft's prebuilt C++ image (gcc-13, gdb, cmake, git). Two jobs,
# both of which must happen at image-build time:
#
# 1. Pre-create vscode-owned dirs so their persistence volumes mount cleanly:
#      ~/.vscode-server/extensions -> persists editor extensions across rebuilds
#      ~/.config/gh                 -> persists the `gh` (dtippett-bot) login
#    Without this, Docker auto-creates the parent dir root-owned when it mounts the
#    volume. For ~/.vscode-server that breaks the VS Code server's startup
#    `mkdir ~/.vscode-server/bin` (EACCES at ~7s, before any lifecycle hook can chown
#    it); for ~/.config/gh it blocks `gh` from writing its config.
#
# 2. Install `perf`. The container runs on Docker Desktop's WSL2 kernel (e.g.
#    6.x-microsoft-standard-WSL2), for which NO apt linux-tools package version-
#    matches -- so we install the GENERIC perf and symlink it onto PATH ahead of the
#    version-checking /usr/bin/perf shim. Fine for user-space sampling of mtg; needs
#    --cap-add=PERFMON (set in devcontainer.json) to actually sample.
#
# Everything else (firewall tools, the Node/GitHub-CLI features, the egress firewall)
# stays in devcontainer.json.
FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

RUN mkdir -p /home/vscode/.vscode-server/extensions /home/vscode/.config/gh \
    && chown -R vscode:vscode /home/vscode/.vscode-server /home/vscode/.config

RUN apt-get update \
    && apt-get install -y --no-install-recommends linux-tools-generic \
    && ln -sf "$(ls -d /usr/lib/linux-tools-*/perf | sort -V | tail -1)" /usr/local/bin/perf \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
