# Minimal layer on Microsoft's prebuilt C++ image (gcc-13, gdb, cmake, git). The
# ONLY reason this Dockerfile exists: pre-create ~/.vscode-server owned by the
# non-root `vscode` user, so the named volume mounted at ~/.vscode-server/extensions
# (which persists editor extensions across rebuilds) initializes vscode-owned.
#
# Without this, Docker auto-creates ~/.vscode-server root-owned when it mounts that
# volume, and the VS Code server's startup `mkdir ~/.vscode-server/bin` fails with
# EACCES at ~7s -- before any devcontainer lifecycle hook could chown it.
#
# Everything else (apt packages, the Node feature, the egress firewall) stays in
# devcontainer.json.
FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

RUN mkdir -p /home/vscode/.vscode-server/extensions \
    && chown -R vscode:vscode /home/vscode/.vscode-server
