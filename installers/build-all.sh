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

mkdir -p "$OUT_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

for entry in "caverns-delay:Caverns" "damage-fuzz:Damage" "corrosion-drive:Corrosion" "flux-phaser:Flux" "alloy-bass:Alloy" "gradient-pitch:Gradient"; do
    repo="${entry%%:*}"
    name="${entry##*:}"
    echo "==> Building $name component packages"
    "$ROOT_DIR/$repo/installer/build.sh" --component-only
    cp "$ROOT_DIR/$repo/installer/output/${name}-AU-Component.pkg" "$STAGE_DIR/"
    cp "$ROOT_DIR/$repo/installer/output/${name}-VST3-Component.pkg" "$STAGE_DIR/"
    cp "$ROOT_DIR/$repo/installer/output/${name}-Standalone-Component.pkg" "$STAGE_DIR/"
done

echo "==> Building combined installer"
productbuild \
    --distribution "$INSTALLERS_DIR/distribution.xml" \
    --resources "$INSTALLERS_DIR/Resources" \
    --package-path "$STAGE_DIR" \
    "$OUT_DIR/WildJagPlugins-Installer.pkg"

echo "==> Done: $OUT_DIR/WildJagPlugins-Installer.pkg"
