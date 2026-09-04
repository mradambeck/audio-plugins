#!/usr/bin/env bash
set -euo pipefail

# Builds a macOS .pkg installer for Caverns.
#
# Each format (AU, VST3, Standalone) is packaged as its own relocatable
# component package, so the installer can offer independent checkboxes for
# each one and a native "install for all users" vs "install for me only"
# location choice (relative install-location + <domains> in distribution.xml
# lets Installer.app resolve the destination itself).
#
# Output:
#   installer/output/Caverns-AU-Component.pkg          - flat component packages
#   installer/output/Caverns-VST3-Component.pkg          (also consumed by the
#   installer/output/Caverns-Standalone-Component.pkg    group installer)
#   installer/output/Caverns-Installer.pkg              - double-clickable installer
#                                                          with welcome/license screens
#
# Usage: installer/build.sh [--component-only]

PLUGIN_NAME="Caverns"
PRODUCT_NAME="Caverns - Delay"
BUNDLE_ID="com.wildjag.caverns"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALLER_DIR="$ROOT_DIR/installer"
BUILD_DIR="$ROOT_DIR/build"
STAGE_DIR="$INSTALLER_DIR/stage"
OUT_DIR="$INSTALLER_DIR/output"

source "$ROOT_DIR/../../installers/sign-and-notarize.sh"

VERSION="$("$ROOT_DIR/../../scripts/plugin-version.sh" "$ROOT_DIR" version)"

COMPONENT_ONLY=false
if [[ "${1:-}" == "--component-only" ]]; then
    COMPONENT_ONLY=true
fi

echo "==> Building $PLUGIN_NAME $VERSION (Release)"
cmake --build "$BUILD_DIR" --config Release --target "${PLUGIN_NAME}_All"

ARTEFACTS="$BUILD_DIR/${PLUGIN_NAME}_artefacts/Release"

echo "==> Staging payload"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/au" "$STAGE_DIR/vst3" "$STAGE_DIR/standalone"

cp -R "$ARTEFACTS/AU/${PRODUCT_NAME}.component" "$STAGE_DIR/au/"
cp -R "$ARTEFACTS/VST3/${PRODUCT_NAME}.vst3" "$STAGE_DIR/vst3/"
cp -R "$ARTEFACTS/Standalone/${PRODUCT_NAME}.app" "$STAGE_DIR/standalone/"

sign_bundle "$STAGE_DIR/au/${PRODUCT_NAME}.component"
sign_bundle "$STAGE_DIR/vst3/${PRODUCT_NAME}.vst3"
sign_bundle "$STAGE_DIR/standalone/${PRODUCT_NAME}.app"

mkdir -p "$OUT_DIR"

echo "==> Building component packages"
pkgbuild \
    --sign "$DEVELOPER_ID_INSTALLER" \
    --root "$STAGE_DIR/au" \
    --install-location "Library/Audio/Plug-Ins/Components" \
    --identifier "${BUNDLE_ID}.au.pkg" \
    --version "$VERSION" \
    "$OUT_DIR/${PLUGIN_NAME}-AU-Component.pkg"

pkgbuild \
    --sign "$DEVELOPER_ID_INSTALLER" \
    --root "$STAGE_DIR/vst3" \
    --install-location "Library/Audio/Plug-Ins/VST3" \
    --identifier "${BUNDLE_ID}.vst3.pkg" \
    --version "$VERSION" \
    "$OUT_DIR/${PLUGIN_NAME}-VST3-Component.pkg"

pkgbuild \
    --sign "$DEVELOPER_ID_INSTALLER" \
    --root "$STAGE_DIR/standalone" \
    --install-location "Applications" \
    --identifier "${BUNDLE_ID}.standalone.pkg" \
    --version "$VERSION" \
    "$OUT_DIR/${PLUGIN_NAME}-Standalone-Component.pkg"

if [[ "$COMPONENT_ONLY" == true ]]; then
    echo "==> Done: $OUT_DIR/${PLUGIN_NAME}-{AU,VST3,Standalone}-Component.pkg"
    exit 0
fi

echo "==> Building standalone installer"
echo "==> Generating distribution.xml from template"
sed "s/__VERSION__/$VERSION/g" "$INSTALLER_DIR/distribution.xml.in" > "$OUT_DIR/distribution.xml"

productbuild \
    --sign "$DEVELOPER_ID_INSTALLER" \
    --distribution "$OUT_DIR/distribution.xml" \
    --resources "$INSTALLER_DIR/Resources" \
    --package-path "$OUT_DIR" \
    "$OUT_DIR/${PLUGIN_NAME}-Installer.pkg"

notarize_and_staple "$OUT_DIR/${PLUGIN_NAME}-Installer.pkg"

echo "==> Done: $OUT_DIR/${PLUGIN_NAME}-Installer.pkg"
