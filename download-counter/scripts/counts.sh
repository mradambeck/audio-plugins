#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

if [ ! -f .admin-token ]; then
  echo "Missing .admin-token in download-counter/ — run 'npx wrangler secret put ADMIN_TOKEN' and save the value there first." >&2
  exit 1
fi

WORKER_URL="https://wildjag-downloads.mr-adambeck.workers.dev"
curl -sf "$WORKER_URL/stats?token=$(cat .admin-token)" | python3 -m json.tool
