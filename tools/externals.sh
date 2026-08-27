#!/bin/sh
# External-tree manifests for the Metal build. Every tree under externals/ has
# one __pmm__.toml whose sources array is the single definition of what a seat
# compiles — same contract as tools/cards.sh for cards, but for vendored
# upstreams. There is no external list to maintain: a directory under
# externals/ holding __pmm__.toml is an external.
#
#   externals.sh list <name>   print the manifest's sources, one per line
#   externals.sh check         validate every manifest; every file must exist
#   externals.sh gen <name>    regenerate the sources array from the tree
#
# gen encodes each external's own discovery rule, so the array is regenerable
# and never hand-maintained:
#   tcc         fixed file set (the ONE_SOURCE translation set; the seats
#               compile libtcc.c alone and it #includes the rest)
#   zenoh-pico  every .c under the muscle dirs, src/system excluded
#   mrustc      every src/**/*.cpp plus the tools/common helpers
#
# The manifest schema is exactly: fqn, impl, version, sources, include_dirs,
# defines, depends, archive (opt), upstream (opt), notes (opt).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root="$here/../externals"

mode="${1:?usage: externals.sh list|check|gen NAME}"

manifest_of() {
    name="$1"
    m="$root/$name/__pmm__.toml"
    [ -f "$m" ] || {
        echo "externals.sh: no manifest at $m" >&2
        exit 1
    }
    echo "$m"
}

# Print the sources array entries of a manifest, one per line, in order.
sources_of() {
    m=$(manifest_of "$1")
    awk '
        /^sources[[:space:]]*=[[:space:]]*\[/ { inarr = 1; next }
        inarr && /\]/ { inarr = 0 }
        inarr {
            line = $0
            sub(/^[[:space:]]*/, "", line)
            sub(/[[:space:]]*,?[[:space:]]*$/, "", line)
            gsub(/^"/, "", line)
            gsub(/"$/, "", line)
            if (line != "") print line
        }
    ' "$m"
}

# Discovery rule per external: print the regenerated sources, one per line.
discover() {
    name="$1"
    dir="$root/$name"
    case "$name" in
    tcc)
        # The translation set ONE_SOURCE compiles; order follows libtcc.c's
        # #include chain so the manifest reads like the source.
        for f in libtcc.c tccpp.c tccgen.c tccelf.c tccasm.c tccdbg.c \
            tccrun.c tcctools.c i386-asm.c x86_64-gen.c x86_64-link.c \
            wasm32-gen.c wasm32-link.c; do
            [ -f "$dir/$f" ] || {
                echo "externals.sh: tcc discovery: missing $f" >&2
                exit 1
            }
            echo "$f"
        done
        ;;
    zenoh-pico)
        # Same find|grep|grep as tools/zenoh.mk's ZP_REL, but run inside the
        # tree so paths are relative — hence the leading-less src/system
        # exclusion (zenoh.mk greps the prefixed absolute path).
        (cd "$dir" && find src -name '*.c' \
            | grep -E '/(api|collections|link|net|protocol|runtime|session|transport|utils)/' \
            | grep -v '^src/system/' \
            | sort)
        ;;
    mrustc)
        (cd "$dir" && { find src -name '*.cpp'; \
            echo tools/common/debug.cpp; echo tools/common/jobserver.cpp; \
            echo tools/common/path.cpp; echo tools/common/toml.cpp; } | sort)
        ;;
    *)
        echo "externals.sh: no discovery rule for $name" >&2
        exit 1
        ;;
    esac
}

# Rewrite a manifest's sources array from the discovery rule, preserving
# every other key. Array entries are written sorted as discovery prints them.
rewrite_sources() {
    m=$(manifest_of "$1")
    tmp="$m.tmp"
    discover "$1" >"$m.new" || exit 1
    awk -v newlist="$m.new" '
        BEGIN { while ((getline line < newlist) > 0) new[++n] = line; close(newlist) }
        /^sources[[:space:]]*=[[:space:]]*\[/ {
            print "sources = ["
            for (i = 1; i <= n; i++) printf "    \"%s\",\n", new[i]
            print "]"
            skipping = 1
            next
        }
        skipping && /\]/ { skipping = 0; next }
        skipping { next }
        { print }
    ' "$m" >"$tmp"
    mv "$tmp" "$m"
    rm -f "$m.new"
}

check_manifest() {
    name="$1"
    m=$(manifest_of "$name")
    dir=$(dirname "$m")
    bad=0
    for key in fqn impl sources; do
        grep -q "^$key[[:space:]]*=" "$m" || {
            echo "externals.sh: $name manifest missing key $key" >&2
            bad=1
        }
    done
    impl=$(sed -n 's/^impl[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$m")
    case "$impl" in
    c | cpp | rs) ;;
    *)
        echo "externals.sh: $name impl is \"$impl\" — want c, cpp or rs" >&2
        bad=1
        ;;
    esac
    n=0
    for src in $(sources_of "$name"); do
        [ -f "$dir/$src" ] || {
            echo "externals.sh: $name lists $src but it does not exist" >&2
            bad=1
        }
        n=$((n + 1))
    done
    [ "$n" -gt 0 ] || {
        echo "externals.sh: $name has an empty sources array" >&2
        bad=1
    }
    return "$bad"
}

case "$mode" in
list)
    [ $# -ge 2 ] || { echo "externals.sh: list needs NAME" >&2; exit 1; }
    sources_of "$2"
    ;;
check)
    drift=0
    for m in "$root"/*/__pmm__.toml; do
        [ -f "$m" ] || continue
        name=$(basename "$(dirname "$m")")
        check_manifest "$name" || drift=1
    done
    [ "$drift" -eq 0 ] || exit 1
    exit 0
    ;;
gen)
    [ $# -ge 2 ] || { echo "externals.sh: gen needs NAME" >&2; exit 1; }
    rewrite_sources "$2"
    ;;
*)
    echo "externals.sh: unknown mode $mode" >&2
    exit 1
    ;;
esac

exit 0
