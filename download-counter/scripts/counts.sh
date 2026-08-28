#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

if [ ! -f .admin-token ]; then
  echo "Missing .admin-token in download-counter/ — run 'npx wrangler secret put ADMIN_TOKEN' and save the value there first." >&2
  exit 1
fi

WORKER_URL="https://wildjag-downloads.mr-adambeck.workers.dev"
SNAPSHOT_FILE=".last-counts.json"

current="$(curl -sf "$WORKER_URL/stats?token=$(cat .admin-token)")"

python3 - "$SNAPSHOT_FILE" <<'EOF' "$current"
import json
import sys

snapshot_file, current_json = sys.argv[1], sys.argv[2]
current = json.loads(current_json)

try:
    with open(snapshot_file) as f:
        previous = json.load(f)
except (FileNotFoundError, json.JSONDecodeError):
    previous = {}

print("Current counts:")
print(json.dumps(current, indent=4, sort_keys=True))

print("\nSince last check:")
keys = sorted(set(current) | set(previous))
if not keys:
    print("  (no data)")
else:
    any_change = False
    for key in keys:
        cur_formats = current.get(key, {})
        prev_formats = previous.get(key, {})
        formats = sorted(set(cur_formats) | set(prev_formats))
        parts = []
        for fmt in formats:
            cur_n = cur_formats.get(fmt, 0)
            prev_n = prev_formats.get(fmt, 0)
            delta = cur_n - prev_n
            if delta != 0:
                any_change = True
            sign = "+" if delta >= 0 else ""
            parts.append(f"{fmt}: {cur_n} ({sign}{delta})")
        if parts:
            print(f"  {key}: " + ", ".join(parts))
    if not any_change:
        print("  no changes")

with open(snapshot_file, "w") as f:
    json.dump(current, f, indent=2, sort_keys=True)
EOF
