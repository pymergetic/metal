#!/bin/sh
# Card discovery for the Metal build. There is no card list to maintain:
# a directory holding __pmm__.toml is a card, exactly as pymergetic.util.gen's
# find_cards() sees it. Paths are printed relative to the card root so each
# seat can prefix them its own way.
#
#   cards.sh impl  [ROOT]   C translation units of every impl = "c" card
#   cards.sh tests [ROOT]   __tests__.c of every card, whatever its impl
#   cards.sh check [ROOT]   drift check only (no output)
#
# tests does not filter on impl: net/http/asgi is impl = "rs" and still owns a
# C test that drives it through the registry.
#
# A card contributes every .c beside its manifest except __tests__.c, so
# net/wg's __crypto__.c comes along without being named anywhere.
#
# Drift is a build failure, not a warning: C source with no manifest is
# invisible to gen and to the registry, and a manifest claiming impl = "c"
# with no __impl__.c cannot register the faces it promises.

set -eu

mode="${1:?usage: cards.sh impl|tests|check [ROOT]}"
root="${2:-$(dirname "$0")/../src/pymergetic/metal}"

[ -d "$root" ] || {
    echo "cards.sh: no card root at $root" >&2
    exit 1
}

cd "$root"

drift=0

# C source that no manifest owns.
for c in $(find . -name '*.c' ! -name '__tests__.c' | sed 's|^\./||' | sort); do
    dir=$(dirname "$c")
    [ -f "$dir/__pmm__.toml" ] || {
        echo "cards.sh: $root/$c has no __pmm__.toml — card tree drift" >&2
        drift=1
    }
done

# Manifests that promise C they do not have.
for toml in $(find . -name '__pmm__.toml' | sed 's|^\./||' | sort); do
    dir=$(dirname "$toml")
    grep -q '^impl[[:space:]]*=[[:space:]]*"c"' "$toml" || continue
    [ -f "$dir/__impl__.c" ] || {
        echo "cards.sh: $root/$dir claims impl = \"c\" but has no __impl__.c" >&2
        drift=1
    }
done

[ "$drift" -eq 0 ] || exit 1
[ "$mode" = check ] && exit 0

for toml in $(find . -name '__pmm__.toml' | sed 's|^\./||' | sort); do
    dir=$(dirname "$toml")
    case "$mode" in
    impl)
        grep -q '^impl[[:space:]]*=[[:space:]]*"c"' "$toml" || continue
        for c in "$dir"/*.c; do
            case "$c" in
            *__tests__.c) ;;
            *'*.c') ;;
            *) echo "$c" ;;
            esac
        done
        ;;
    tests)
        [ -f "$dir/__tests__.c" ] && echo "$dir/__tests__.c"
        ;;
    *)
        echo "cards.sh: unknown mode $mode" >&2
        exit 1
        ;;
    esac
done

exit 0
