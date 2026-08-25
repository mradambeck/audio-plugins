#!/usr/bin/env bash
set -euo pipefail

# Builds a single macOS .pkg installer that lets the user pick which Wild Jag
# plugins to install, and within each plugin which formats (AU / VST3 /
# Standalone) to install, all via checkboxes. Also offers the native
# "install for all users of this Mac" vs "install for me only" location
# choice for every selected item.
#
# Rebuilds each plugin's component packages by delegating to its own
# installer/build.sh, then wraps all of them into one distribution.
#
# Output: installers/output/WildJagPlugins-Installer.pkg

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALLERS_DIR="$ROOT_DIR/installers"
OUT_DIR="$INSTALLERS_DIR/output"
STAGE_DIR="$INSTALLERS_DIR/stage"

source "$INSTALLERS_DIR/sign-and-notarize.sh"

mkdir -p "$OUT_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/Resources"

# Accumulates one -e "s/__<NAME>_VERSION__/<version>/g" per plugin, applied below to both
# distribution.xml.in and Resources/welcome.html.in so there's a single substitution pass/token
# convention for both files instead of two.
sed_args=()

for entry in "caverns-delay:Caverns" "damage-fuzz:Damage" "corrosion-drive:Corrosion" "flux-phaser:Flux" "alloy-bass:Alloy" "gradient-pitch:Gradient" "shields-reverb:Shields"; do
    repo="${entry%%:*}"
    name="${entry##*:}"
    echo "==> Building $name component packages"
    "$ROOT_DIR/$repo/installer/build.sh" --component-only
    cp "$ROOT_DIR/$repo/installer/output/${name}-AU-Component.pkg" "$STAGE_DIR/"
    cp "$ROOT_DIR/$repo/installer/output/${name}-VST3-Component.pkg" "$STAGE_DIR/"
    cp "$ROOT_DIR/$repo/installer/output/${name}-Standalone-Component.pkg" "$STAGE_DIR/"

    version="$("$ROOT_DIR/scripts/plugin-version.sh" "$ROOT_DIR/$repo" version)"
    name_upper="$(echo "$name" | tr '[:lower:]' '[:upper:]')"
    sed_args+=(-e "s/__${name_upper}_VERSION__/$version/g")
done

echo "==> Generating distribution.xml and welcome.html from templates"
sed "${sed_args[@]}" "$INSTALLERS_DIR/distribution.xml.in" > "$STAGE_DIR/distribution.xml"
sed "${sed_args[@]}" "$INSTALLERS_DIR/Resources/welcome.html.in" > "$STAGE_DIR/Resources/welcome.html"
cp "$INSTALLERS_DIR/Resources/license.txt" "$INSTALLERS_DIR/Resources/conclusion.html" "$STAGE_DIR/Resources/"

echo "==> Building combined installer"
productbuild \
    --sign "$DEVELOPER_ID_INSTALLER" \
    --distribution "$STAGE_DIR/distribution.xml" \
    --resources "$STAGE_DIR/Resources" \
    --package-path "$STAGE_DIR" \
    "$OUT_DIR/WildJagPlugins-Installer.pkg"

notarize_and_staple "$OUT_DIR/WildJagPlugins-Installer.pkg"

echo "==> Done: $OUT_DIR/WildJagPlugins-Installer.pkg"
