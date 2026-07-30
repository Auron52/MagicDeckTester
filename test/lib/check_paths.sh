#!/usr/bin/env bash
# Standing audit: which scripts reference repo paths that no longer exist?
#
#   bash test/lib/check_paths.sh          # report
#   bash test/lib/check_paths.sh --strict # report AND exit 1 if anything is dead
#
# WHY. Scripts under test/ and scripts/ hard-code deck and binary paths. When decks moved into
# per-deck folders (decks/<name>/<name>.txt, docs/design/per-deck-folder-layout.md) every script
# still naming the flat decks/<name>.txt was orphaned -- and orphaned SILENTLY: a long A/B whose
# deck file is missing can still print an avg, so it reads as a result rather than an error. Two
# scripts were additionally pointing at build/Release/mtg.exe with no fallback, so they could not
# run on Linux at all.
#
# This audit makes that rot one command away instead of a discovery. It is intentionally dumb:
# it greps literal paths and asks the filesystem. It cannot see paths built from variables, so a
# clean report is NOT proof a script runs -- it is proof of the absence of one specific rot.
#
# Deliberately NOT flagged: a `build/Release/mtg.exe` mention that the same script guards with a
# fallback to `build/Release/mtg` (the documented Windows/Linux multi-config dance). Use
# harness_bin from test/lib/harness.sh in new scripts and the question does not arise.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

STRICT=0
[ "${1:-}" = "--strict" ] && STRICT=1

dead_total=0
files_total=0

for f in test/*.sh scripts/*.sh scripts/*.py test/lib/*.sh; do
    [ -f "$f" ] || continue
    dead=""
    # Literal repo paths worth checking. Kept narrow on purpose: decks/ and references/ are data
    # the scripts consume, build/Release/ is the binary they drive.
    while read -r p; do
        [ -z "$p" ] && continue
        [ -e "$p" ] && continue
        # A .exe mention is fine when the script falls back to the extensionless path.
        case "$p" in
            */mtg.exe|*/mtg-analyze.exe)
                base=${p%.exe}
                grep -qF "$base" "$f" && continue
                ;;
        esac
        # Placeholders in usage text ("decks/x/x.cod", "decks/<name>.txt") are documentation.
        case "$p" in
            *'<'*|*'>'*|decks/x/*|decks/X.*|decks/NewDeck.*|decks/test_deck.*) continue ;;
        esac
        dead+=" $p"
    done < <(grep -ohE '\b(decks|references|build/Release)/[A-Za-z0-9_./-]+' "$f" | sort -u)

    if [ -n "$dead" ]; then
        files_total=$((files_total + 1))
        # shellcheck disable=SC2086
        set -- $dead
        dead_total=$((dead_total + $#))
        printf '%-42s %s dead:\n' "$f" "$#"
        for p in "$@"; do printf '    %s\n' "$p"; done
    fi
done

echo ""
if [ "$dead_total" -eq 0 ]; then
    echo "check_paths: clean -- no dead deck/reference/binary paths in test/ or scripts/"
    exit 0
fi
echo "check_paths: $dead_total dead path(s) across $files_total file(s)."
echo "Fix by resolving through test/lib/harness.sh (harness_bin / h_deck / h_profile) rather than"
echo "hard-coding, then re-run. See docs/design/per-deck-folder-layout.md for the deck layout."
[ "$STRICT" -eq 1 ] && exit 1
exit 0
