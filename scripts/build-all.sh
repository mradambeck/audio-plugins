#!/usr/bin/env bash
set -euo pipefail

# Builds every plugin's AU/VST3/Standalone formats in Release. Each plugin is configured and
# built independently (not a unified CMake super-project) -- this mirrors the same
# per-plugin-directory pattern installers/build-all.sh already uses, and keeps every plugin
# directory buildable on its own (`cd <plugin> && cmake -B build`) for the skills/tooling that
# assume that.
#
# Usage: scripts/build-all.sh

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for entry in "caverns-delay:Caverns" "damage-fuzz:Damage" "corrosion-drive:Corrosion" "flux-phaser:Flux" "alloy-bass:Alloy" "gradient-pitch:Gradient" "shields-reverb:Shields"; do
    repo="${entry%%:*}"
    name="${entry##*:}"
    echo "==> Configuring $name"
    cmake -S "$ROOT_DIR/$repo" -B "$ROOT_DIR/$repo/build" -G Xcode
    echo "==> Building $name (AU/VST3/Standalone)"
    cmake --build "$ROOT_DIR/$repo/build" --config Release --target "${name}_All"
done

echo "==> All plugins built."
