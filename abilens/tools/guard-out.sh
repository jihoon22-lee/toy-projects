#!/bin/sh
set -eu

usage() {
    echo "usage: guard-out.sh build|clean OUT" >&2
    exit 64
}

[ "$#" -eq 2 ] || usage
action=$1
requested=$2
[ "$action" = "build" ] || [ "$action" = "clean" ] || usage
[ -n "$requested" ] || {
    echo "refusing an empty OUT" >&2
    exit 1
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P)
canonical=$(realpath -m -- "$requested")
marker="$canonical/.abilens-out"

case "$canonical" in
    /|"$project_root")
        echo "refusing unsafe OUT: $requested" >&2
        exit 1
        ;;
esac

valid_marker() {
    [ -f "$marker" ] || return 1
    [ ! -L "$marker" ] || return 1
    [ "$(sed -n '1p' "$marker")" = "abilens output ownership v1" ] &&
        [ "$(sed -n '2p' "$marker")" = "$canonical" ]
}

owned_children_only() {
    found=0
    for child in "$canonical"/* "$canonical"/.[!.]* "$canonical"/..?*; do
        if [ ! -e "$child" ] && [ ! -L "$child" ]; then
            continue
        fi
        [ "$child" != "$marker" ] || return 1
        [ -d "$child" ] || return 1
        [ ! -L "$child" ] || return 1
        child_canonical=$(realpath -m -- "$child")
        child_marker="$child_canonical/.abilens-out"
        [ -f "$child_marker" ] || return 1
        [ ! -L "$child_marker" ] || return 1
        [ "$(sed -n '1p' "$child_marker")" = "abilens output ownership v1" ] || return 1
        [ "$(sed -n '2p' "$child_marker")" = "$child_canonical" ] || return 1
        found=1
    done
    [ "$found" -eq 1 ]
}

if [ "$action" = "clean" ]; then
    if [ ! -e "$canonical" ]; then
        exit 0
    fi
    [ ! -L "$canonical" ] || {
        echo "refusing symlink OUT: $requested" >&2
        exit 1
    }
    [ -d "$canonical" ] || {
        echo "refusing non-directory OUT: $requested" >&2
        exit 1
    }
    if ! valid_marker && ! owned_children_only; then
        if [ -z "$(find "$canonical" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
            {
                echo "abilens output ownership v1"
                echo "$canonical"
            } > "$marker"
            exit 0
        fi
        echo "refusing to clean unowned OUT: $requested" >&2
        exit 1
    fi
    rm -rf -- "$canonical"
    exit 0
fi

if [ -e "$canonical" ]; then
    [ ! -L "$canonical" ] || {
        echo "refusing symlink OUT: $requested" >&2
        exit 1
    }
    [ -d "$canonical" ] || {
        echo "refusing non-directory OUT: $requested" >&2
        exit 1
    }
    if valid_marker; then
        exit 0
    fi
    if [ -n "$(find "$canonical" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
        echo "refusing to write into unowned non-empty OUT: $requested" >&2
        exit 1
    fi
else
    mkdir -p -- "$canonical"
fi

if [ ! -e "$canonical/.abilens-out" ]; then
    {
        echo "abilens output ownership v1"
        echo "$canonical"
    } > "$canonical/.abilens-out"
fi
valid_marker || {
    echo "could not establish OUT ownership: $requested" >&2
    exit 1
}
