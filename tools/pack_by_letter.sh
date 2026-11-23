#!/usr/bin/env bash
#
# pack_by_letter.sh [-x] [DIR]
#
# Bucket a flat pile of module folders into A..Z, so a directory holding a
# couple of thousand entries becomes twenty-odd. Names that start with a digit
# go to "0-9"; anything else to "MISC".
#
# Dry run unless -x is given: it prints what it would do and touches nothing.
#
# Safe to re-run -- existing buckets are left alone, and a folder is never
# moved onto one that is already there.
set -euo pipefail

go=0
if [ "${1-}" = "-x" ]; then go=1; shift; fi
dir="${1-$PWD}"
[ -d "$dir" ] || { echo "no such directory: $dir" >&2; exit 1; }

bucket_of() {
    local c
    c=$(printf '%s' "${1:0:1}" | tr 'a-z' 'A-Z')
    case "$c" in
        [A-Z]) printf '%s' "$c" ;;
        [0-9]) printf '0-9'     ;;
        *)     printf 'MISC'    ;;
    esac
}

moved=0 skipped=0
declare -A counts=()

while IFS= read -r -d '' path; do
    name=${path##*/}
    case "$name" in
        [A-Z]|0-9|MISC) skipped=$((skipped+1)); continue ;;   # already a bucket
    esac
    b=$(bucket_of "$name")
    if [ -e "$dir/$b/$name" ]; then
        echo "skip (already there): $b/$name" >&2
        skipped=$((skipped+1)); continue
    fi
    if [ "$go" = 1 ]; then
        mkdir -p -- "$dir/$b"
        mv -n -- "$path" "$dir/$b/"
    fi
    counts[$b]=$(( ${counts[$b]-0} + 1 ))
    moved=$((moved+1))
done < <(find "$dir" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z)

for b in $(printf '%s\n' "${!counts[@]}" | sort); do
    printf '  %-4s %4d\n' "$b" "${counts[$b]}"
done

if [ "$go" = 1 ]; then
    sync
    echo "moved $moved folders, skipped $skipped"
else
    echo "would move $moved folders, skip $skipped   -- re-run with -x to do it"
fi
