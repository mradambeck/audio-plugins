#!/usr/bin/env bash
set -euo pipefail

# Thin wrapper so download counts can be checked from the repo root.
# Usage: scripts/counts.sh

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$ROOT_DIR/download-counter/scripts/counts.sh"
