#!/bin/bash
# One-command installer for this dev container's VS Code extensions.
#
# Run it from the container's integrated terminal (Terminal -> New Terminal in
# VS Code), e.g. after a rebuild:
#
#     bash .devcontainer/install-extensions.sh
#
# Why a script and not automatic: the extensions download from the Marketplace
# (wildcard CDN hosts the egress firewall can't pin), and they install via the
# VS Code Server's `code` CLI, which only exists once the server is running. So we
# briefly lift the firewall, install, then re-apply it. A trap re-locks egress on
# ANY exit (success, failed install, or Ctrl-C), so the firewall is never left open.
#
# Keep this list in sync with customizations.vscode.extensions in devcontainer.json.
set -euo pipefail

EXTENSIONS=(
  ms-vscode.cpptools-extension-pack
  ms-python.python
  anthropic.claude-code
)

if ! command -v code >/dev/null 2>&1; then
  echo "ERROR: the 'code' CLI is not on PATH." >&2
  echo "       Run this inside the dev container's integrated terminal." >&2
  exit 1
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[ext] lifting egress firewall temporarily..."
sudo iptables -P OUTPUT ACCEPT

# Re-lock on ANY exit so egress is never left open.
relock() {
  echo "[ext] re-applying egress firewall..."
  sudo bash "$HERE/init-firewall.sh"
}
trap relock EXIT

for ext in "${EXTENSIONS[@]}"; do
  echo "[ext] installing $ext ..."
  code --install-extension "$ext" --force
done

echo "[ext] all extensions installed; reload the window if VS Code prompts you."
