#!/usr/bin/env bash
# PACE/wraptool AAX signing helper. The AAX format wiring in
# plugins/common/cmake/AddAAXSupport.cmake only produces an ad-hoc-signed .aaxplugin, which
# Pro Tools will only load in a dev-unlocked install -- this is the step that turns it into a
# real, PACE-signed one. Requires a one-time PACE Central setup per plugin (a Product + Wrap
# Configuration, giving a WILDJAG_AAX_WRAP_CONFIG_GUID to set in that plugin's CMakeLists.txt,
# e.g. plugins/corrosion-drive/CMakeLists.txt) plus account-wide setup (a PACE Tools license and
# an AAX signing certificate for your publisher ID, both deposited on an iLok via iLok License
# Manager's Synchronize).
#
# Can be sourced (for a future installer/build.sh to call sign_aax_bundle directly) or run
# standalone: installers/sign-aax.sh <plugin-dir> [path-to.aaxplugin]
#   <plugin-dir>          e.g. corrosion-drive
#   [path-to.aaxplugin]   defaults to the newest Release AAX build under <plugin-dir>/build-aax/
#
# Required env vars:
#   PACE_ACCOUNT    PACE Central account username (not secret)
#   PACE_SIGN_ID    e.g. "Developer ID Application: Adam Beck (TEAMID)" -- the same identity
#                   DEVELOPER_ID_APPLICATION uses for AU/VST3 in sign-and-notarize.sh
#
# The PACE/iLok account password is never an env var or CLI flag -- sign_aax_bundle reads it
# from the macOS Keychain at sign time (service "wildjag-pace-signing", account $USER):
#   security add-generic-password -a "$USER" -s wildjag-pace-signing -w
#
# wraptool isn't on PATH -- PACE's "Code Signing For AAX SDK" Mac installer drops it under
# /Applications/PACEAntiPiracy/Eden/Fusion/Versions/<N>/bin/wraptool.

: "${PACE_ACCOUNT:?Set PACE_ACCOUNT to your PACE Central account username}"
: "${PACE_SIGN_ID:?Set PACE_SIGN_ID to your 'Developer ID Application: ...' identity}"

WRAPTOOL="${WRAPTOOL:-$(ls /Applications/PACEAntiPiracy/Eden/Fusion/Versions/*/bin/wraptool 2>/dev/null | sort -V | tail -1)}"

sign_aax_bundle() {
    local path="$1"
    local wcguid="$2"

    [[ -n "$wcguid" ]] || { echo "error: sign_aax_bundle needs a Wrap Configuration GUID (see PACE Central)" >&2; return 1; }
    [[ -n "$WRAPTOOL" && -x "$WRAPTOOL" ]] || { echo "error: wraptool not found -- install PACE's 'Code Signing For AAX SDK' package" >&2; return 1; }

    local pace_password
    pace_password="$(security find-generic-password -a "$USER" -s "wildjag-pace-signing" -w 2>/dev/null)" \
        || { echo "error: no PACE password in Keychain -- run: security add-generic-password -a \"\$USER\" -s wildjag-pace-signing -w" >&2; return 1; }

    echo "==> Signing (AAX) $path"
    "$WRAPTOOL" sign --verbose \
        --account "$PACE_ACCOUNT" \
        --password "$pace_password" \
        --wcguid "$wcguid" \
        --dsig1-compat off \
        --signid "$PACE_SIGN_ID" \
        --in "$path" \
        --out "$path"
}

# Only run as a CLI when executed directly, not when sourced by another script.
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    set -euo pipefail

    plugin_dir="${1:?Usage: $0 <plugin-dir> [path-to.aaxplugin]}"
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    cmake_file="$repo_root/plugins/$plugin_dir/CMakeLists.txt"

    [[ -f "$cmake_file" ]] || { echo "error: no such plugin '$plugin_dir' ($cmake_file not found)" >&2; exit 1; }

    wcguid=$(grep -oE 'WILDJAG_AAX_WRAP_CONFIG_GUID "[^"]+"' "$cmake_file" | grep -oE '"[^"]+"' | tr -d '"' || true)
    [[ -n "$wcguid" ]] || {
        echo "error: $plugin_dir has no WILDJAG_AAX_WRAP_CONFIG_GUID in its CMakeLists.txt yet" >&2
        echo "       create its Product + Wrap Configuration in PACE Central first" >&2
        exit 1
    }

    if [[ -n "${2:-}" ]]; then
        aax_bundle="$2"
    else
        aax_bundle=$(find "$repo_root/plugins/$plugin_dir"/build-aax/*_artefacts/Release/AAX -maxdepth 1 -iname '*.aaxplugin' 2>/dev/null | head -1 || true)
    fi
    [[ -n "$aax_bundle" && -d "$aax_bundle" ]] || {
        echo "error: no .aaxplugin found under $plugin_dir/build-aax/ -- build the <Name>_AAX target first" >&2
        exit 1
    }

    sign_aax_bundle "$aax_bundle" "$wcguid"
fi
