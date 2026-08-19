#!/usr/bin/env bash
set -uo pipefail

# Builds and runs every plugin's headless test suite (the <Name>Tests console app, driven by
# juce::UnitTestRunner). Each plugin is configured and built independently -- same rationale as
# scripts/build-all.sh. Exits non-zero if any plugin's test suite fails, so this is CI-friendly.
#
# Usage: scripts/test-all.sh

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
overall_status=0

for entry in "caverns-delay:Caverns" "damage-fuzz:Damage" "corrosion-drive:Corrosion" "flux-phaser:Flux" "alloy-bass:Alloy" "gradient-pitch:Gradient" "bloom-reverb:Bloom"; do
    repo="${entry%%:*}"
    name="${entry##*:}"
    echo "==> Configuring $name"
    if ! cmake -S "$ROOT_DIR/$repo" -B "$ROOT_DIR/$repo/build" -G Xcode; then
        echo "==> FAILED: $name configure"
        overall_status=1
        continue
    fi
    echo "==> Building ${name}Tests"
    if ! cmake --build "$ROOT_DIR/$repo/build" --config Release --target "${name}Tests"; then
        echo "==> FAILED: $name build"
        overall_status=1
        continue
    fi
    echo "==> Running ${name}Tests"
    if ! "$ROOT_DIR/$repo/build/${name}Tests_artefacts/Release/${name}Tests"; then
        echo "==> FAILED: $name tests"
        overall_status=1
    fi
done

if [ "$overall_status" -eq 0 ]; then
    echo "==> All plugin test suites passed."
else
    echo "==> One or more plugin test suites failed."
fi

exit "$overall_status"
