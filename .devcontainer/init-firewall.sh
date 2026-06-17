#!/bin/bash
# Egress firewall for the MagicDeckTester dev container.
#
# Policy: default-DENY all outbound traffic, then allow only:
#   - loopback and DNS (so the allowlist can be resolved),
#   - established/related connections (so allowed replies come back),
#   - GitHub's published IP ranges  -> required: CMake FetchContent clones
#     pugixml + nlohmann_json on a fresh build; also covers git push/pull,
#   - each host in ALLOWED_HOSTS below (resolved to its current IPs).
#
# Reapplied on every container start via postStartCommand, because a restart
# recreates the container's network namespace and clears its iptables rules.
#
# ---------------------------------------------------------------------------
# Allowlist knob: extra hostnames to permit. Comment a line out to deny it.
#   api.anthropic.com -> the Claude Code CLI. Covers both `/login` (the browser
#   step runs on the HOST, the container only talks to the API) and normal use.
#   api.scryfall.com  -> ONLY used by scripts/audit_card_costs.py (the card mana-
#   cost audit). The build, the simulator (mtg), the analyzer (mtg-analyze), and
#   the regression suite make NO network calls, so if you won't run the cost
#   audit inside the container you can safely remove that line.
# ---------------------------------------------------------------------------
ALLOWED_HOSTS=(
  api.anthropic.com
  api.scryfall.com
)

set -euo pipefail
IFS=$'\n\t'

echo "[firewall] resetting rules..."
# Start permissive so the setup below (curl/dig) can reach the network; we flip
# to default-DROP at the very end. Resetting the policy BEFORE flushing matters:
# a flush alone leaves a stale DROP policy from a previous run in place.
iptables -P INPUT ACCEPT
iptables -P FORWARD ACCEPT
iptables -P OUTPUT ACCEPT
iptables -F
iptables -X
iptables -t nat -F 2>/dev/null || true
iptables -t mangle -F 2>/dev/null || true
ipset destroy allowed-domains 2>/dev/null || true

# Loopback (covers Docker's embedded DNS at 127.0.0.11) and DNS resolution.
iptables -A INPUT  -i lo -j ACCEPT
iptables -A OUTPUT -o lo -j ACCEPT
iptables -A OUTPUT -p udp --dport 53 -j ACCEPT
iptables -A OUTPUT -p tcp --dport 53 -j ACCEPT
# Let replies to connections we initiated come back in.
iptables -A INPUT  -m state --state ESTABLISHED,RELATED -j ACCEPT
iptables -A OUTPUT -m state --state ESTABLISHED,RELATED -j ACCEPT

# From here on, any unexpected error fails CLOSED: deny general egress (DNS and
# loopback stay allowed via the rules above) rather than leave it wide open.
trap 'echo "[firewall] setup error -- failing closed (egress denied)" >&2; iptables -P OUTPUT DROP 2>/dev/null || true' ERR

ipset create allowed-domains hash:net family inet

add_cidr() {
  # Add an IPv4 address or CIDR to the allowlist (dedups silently).
  local cidr="$1"
  if [[ "$cidr" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+(/[0-9]+)?$ ]]; then
    ipset add allowed-domains "$cidr" 2>/dev/null || true
  fi
}

# --- GitHub: use the authoritative published ranges (covers github.com,
#     codeload.github.com, api.github.com, ...). Retry a few times so a transient
#     hiccup at start doesn't leave the build host unreachable. ---
echo "[firewall] fetching GitHub IP ranges..."
gh_ranges=""
for attempt in 1 2 3; do
  gh_ranges="$(curl -sSL --max-time 20 https://api.github.com/meta || true)"
  [ -n "$gh_ranges" ] && echo "$gh_ranges" | jq -e . >/dev/null 2>&1 && break
  echo "[firewall]   attempt $attempt failed; retrying..."
  sleep 2
done
if [ -z "$gh_ranges" ] || ! echo "$gh_ranges" | jq -e . >/dev/null 2>&1; then
  echo "[firewall] ERROR: could not fetch GitHub ranges; failing closed (egress denied)." >&2
  iptables -P OUTPUT DROP 2>/dev/null || true
  exit 1
fi
while read -r cidr; do
  add_cidr "$cidr"
done < <(echo "$gh_ranges" | jq -r '(.web + .api + .git)[]')

# --- Extra allowlisted hosts (resolved to current IPs). ---
for host in "${ALLOWED_HOSTS[@]:-}"; do
  [ -z "$host" ] && continue
  echo "[firewall] resolving $host..."
  ips="$(dig +short A "$host" | grep -E '^[0-9.]+$' || true)"
  if [ -z "$ips" ]; then
    echo "[firewall]   WARN: could not resolve $host (skipping)"
    continue
  fi
  for ip in $ips; do
    add_cidr "$ip"
  done
done

# Permit traffic to everything in the allowlist, then deny the rest.
iptables -A OUTPUT -m set --match-set allowed-domains dst -j ACCEPT
iptables -P INPUT  DROP
iptables -P FORWARD DROP
iptables -P OUTPUT DROP

# Block all IPv6 egress (the allowlist is IPv4-only) so nothing leaks over v6.
# Use REJECT, not just a DROP policy: if Docker has IPv6 enabled, a client that
# tries an AAAA address first (e.g. urllib in audit_card_costs.py) then fails
# *instantly* and falls back to IPv4, instead of stalling on a dropped SYN.
if command -v ip6tables >/dev/null 2>&1; then
  ip6tables -F 2>/dev/null || true
  ip6tables -A INPUT  -i lo -j ACCEPT 2>/dev/null || true
  ip6tables -A OUTPUT -o lo -j ACCEPT 2>/dev/null || true
  ip6tables -A OUTPUT -j REJECT 2>/dev/null || true
  ip6tables -P INPUT   DROP 2>/dev/null || true
  ip6tables -P FORWARD DROP 2>/dev/null || true
  ip6tables -P OUTPUT  DROP 2>/dev/null || true
fi

# --- Verify: a non-allowlisted host must fail, GitHub must succeed. ---
echo "[firewall] verifying..."
if curl -sS --max-time 5 https://example.com >/dev/null 2>&1; then
  echo "[firewall] ERROR: reached example.com, which should be blocked." >&2
  exit 1
fi
if ! curl -sS --max-time 10 https://api.github.com/zen >/dev/null 2>&1; then
  echo "[firewall] ERROR: cannot reach api.github.com, which should be allowed." >&2
  exit 1
fi

echo "[firewall] active: default-deny egress; allowed = GitHub + ${ALLOWED_HOSTS[*]:-none}."
