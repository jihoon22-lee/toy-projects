#!/bin/sh
set -eu

tmpdir=/tmp
root=$(mktemp -d "$tmpdir/abilens-clean.XXXXXX")
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
guard=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)/tools/guard-out.sh

unowned=$root/unowned
mkdir "$unowned"
printf '%s\n' keep > "$unowned/sentinel"
if "$guard" build "$unowned" >/dev/null 2>&1; then
    echo "guard unexpectedly adopted an unowned directory" >&2
    exit 1
fi
if "$guard" clean "$unowned" >/dev/null 2>&1; then
    echo "guard unexpectedly cleaned an unowned directory" >&2
    exit 1
fi
[ -f "$unowned/sentinel" ] || {
    echo "unowned directory was modified" >&2
    exit 1
}

if "$guard" build / >/dev/null 2>&1; then
    echo "guard unexpectedly accepted root OUT" >&2
    exit 1
fi

owned=$root/owned
"$guard" build "$owned"
[ -f "$owned/.abilens-out" ] || {
    echo "owned marker was not created" >&2
    exit 1
}
"$guard" clean "$owned"
[ ! -e "$owned" ] || {
    echo "owned output was not cleaned" >&2
    exit 1
}
echo "test_clean: PASS"
