#!/usr/bin/env bash
# Fetches a redistributable homebrew GameCube .dol (GCMM, GPL) for the
# headless smoke/determinism runs. Pinned URL + sha256.
set -euo pipefail
DEST="${1:-build-web/test.dol}"

URL="https://github.com/suloku/gcmm/releases/download/1.5.2/gcmm_1.5.2.zip"
ZIP_SHA256="840dbfea6d4fbd5f266dcda92e69088d78fb95987685a2d65285d07b66986f5f"
DOL_IN_ZIP="gcmm_1.5.2/gamecube/gcmm_GC.dol"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

curl -sL "$URL" -o "$tmp/gcmm.zip"
echo "$ZIP_SHA256  $tmp/gcmm.zip" | sha256sum -c - >/dev/null
unzip -q -o "$tmp/gcmm.zip" -d "$tmp"
mkdir -p "$(dirname "$DEST")"
cp "$tmp/$DOL_IN_ZIP" "$DEST"
echo "fetched $DEST"
