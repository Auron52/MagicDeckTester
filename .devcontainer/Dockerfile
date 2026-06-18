# Minimal layer on Microsoft's prebuilt C++ image (gcc-13, gdb, cmake, git). The
# ONLY reason this Dockerfile exists: pre-create directories owned by the non-root
# `vscode` user so that named volumes mounted onto them initialize vscode-owned:
#   ~/.vscode-server/extensions -> persists editor extensions across rebuilds
#   ~/.config/gh                 -> persists the `gh` (dtippett-bot) login
#
# Without this, Docker auto-creates the parent dir root-owned when it mounts the
# volume. For ~/.vscode-server that breaks the VS Code server's startup
# `mkdir ~/.vscode-server/bin` (EACCES at ~7s, before any lifecycle hook can chown
# it); for ~/.config/gh it blocks `gh` from writing its config.
#
# Everything else (apt packages, the Node/GitHub-CLI features, the egress firewall)
# stays in devcontainer.json.
FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

RUN mkdir -p /home/vscode/.vscode-server/extensions /home/vscode/.config/gh \
    && chown -R vscode:vscode /home/vscode/.vscode-server /home/vscode/.config
