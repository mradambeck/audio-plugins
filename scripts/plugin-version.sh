#!/usr/bin/env bash
set -euo pipefail

# Shared VERSION/WILDJAG_RELEASE_CHANNEL extraction for a plugin's CMakeLists.txt, factored out
# so every installer/build.sh (and installers/build-all.sh, and any future tooling) reads the
# same single source of truth the same way instead of re-deriving grep/sed patterns per call site.
#
# Usage: scripts/plugin-version.sh <plugin-dir> <version|channel>

PLUGIN_DIR="${1:?Usage: scripts/plugin-version.sh <plugin-dir> <version|channel>}"
FIELD="${2:?Usage: scripts/plugin-version.sh <plugin-dir> <version|channel>}"

CMAKE_FILE="$PLUGIN_DIR/CMakeLists.txt"

case "$FIELD" in
    version)
        grep -m1 'project(' "$CMAKE_FILE" | sed -E 's/.*VERSION ([0-9.]+).*/\1/'
        ;;
    channel)
        # Defaults to "stable" if the marker is absent, so this doesn't hard-fail on a
        # CMakeLists.txt that predates the WILDJAG_RELEASE_CHANNEL convention.
        channel="$(grep -m1 'WILDJAG_RELEASE_CHANNEL' "$CMAKE_FILE" | sed -E 's/.*"([a-z]+)".*/\1/' || true)"
        echo "${channel:-stable}"
        ;;
    *)
        echo "Unknown field '$FIELD' (expected 'version' or 'channel')" >&2
        exit 1
        ;;
esac
