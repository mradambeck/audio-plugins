#!/usr/bin/env bash
# Shared Developer ID signing + notarization helpers, sourced by every
# plugin's installer/build.sh and by installers/build-all.sh.
#
# Required env vars:
#   DEVELOPER_ID_APPLICATION   e.g. "Developer ID Application: Adam Beck (TEAMID)"
#   DEVELOPER_ID_INSTALLER     e.g. "Developer ID Installer: Adam Beck (TEAMID)"
#
# Notarization credentials (either works; CI uses the API key form):
#   NOTARY_PROFILE              a profile registered via `xcrun notarytool store-credentials`
#   NOTARY_KEY_PATH / NOTARY_KEY_ID / NOTARY_ISSUER_ID   an App Store Connect API key
#
# Set SKIP_NOTARIZE=1 to sign but skip submission (useful for fast local iteration).

: "${DEVELOPER_ID_APPLICATION:?Set DEVELOPER_ID_APPLICATION to your 'Developer ID Application: ...' identity}"
: "${DEVELOPER_ID_INSTALLER:?Set DEVELOPER_ID_INSTALLER to your 'Developer ID Installer: ...' identity}"

sign_bundle() {
    local path="$1"
    echo "==> Signing $path"
    codesign --force --deep --options runtime --timestamp \
        --sign "$DEVELOPER_ID_APPLICATION" "$path"
}

notarize_and_staple() {
    local pkg="$1"

    if [[ "${SKIP_NOTARIZE:-0}" == "1" ]]; then
        echo "==> SKIP_NOTARIZE=1, not submitting $pkg"
        return 0
    fi

    echo "==> Submitting $pkg for notarization"
    if [[ -n "${NOTARY_KEY_PATH:-}" ]]; then
        xcrun notarytool submit "$pkg" \
            --key "$NOTARY_KEY_PATH" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER_ID" \
            --wait
    elif [[ -n "${NOTARY_PROFILE:-}" ]]; then
        xcrun notarytool submit "$pkg" --keychain-profile "$NOTARY_PROFILE" --wait
    else
        echo "error: set NOTARY_PROFILE or NOTARY_KEY_PATH/NOTARY_KEY_ID/NOTARY_ISSUER_ID" >&2
        exit 1
    fi

    echo "==> Stapling $pkg"
    xcrun stapler staple "$pkg"
}
