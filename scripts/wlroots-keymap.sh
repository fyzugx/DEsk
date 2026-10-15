#!/bin/sh
# Apply the parent-keymap fix only to the private wlroots source build.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
patch_file=$root/patches/wlroots-0.18-parent-keymap.patch
if [ "$#" -ne 1 ]; then
    echo "usage: $0 --fingerprint | WLROOTS_SOURCE" >&2
    exit 2
fi
if [ "$1" = --fingerprint ]; then
    patch_sum=$(sha256sum "$patch_file")
    printf '%s\n' "${patch_sum%% *}"
    exit 0
fi
patch --batch --forward --directory="$1" -p1 < "$patch_file"
