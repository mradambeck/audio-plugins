#!/bin/bash
set -e

# Runs only when the "Send anonymous usage analytics" choice is UNCHECKED - this component's
# payload is empty, its only job is this script. Sets telemetryOptedOut=1 in the single
# properties file every Wild Jag plugin's TelemetrySettings reads (see
# plugins/common/Telemetry/TelemetrySettings.cpp), regardless of which plugin(s) this particular
# installer run actually installs - one opt-out, not eleven.
#
# Must resolve the REAL logged-in user's home directory rather than $HOME: a system-wide
# ("install for all users of this Mac") pkg install runs every script as root, and $HOME would
# then be /var/root instead of the person actually running the installer.
REAL_USER="$(stat -f%Su /dev/console)"
REAL_HOME="$(dscl . -read "/Users/$REAL_USER" NFSHomeDirectory | awk '{print $2}')"

PROPS_DIR="$REAL_HOME/Library/Application Support/WildJag"
PROPS_FILE="$PROPS_DIR/WildJag.properties"

mkdir -p "$PROPS_DIR"

if [ -f "$PROPS_FILE" ] && grep -q 'name="telemetryOptedOut"' "$PROPS_FILE"; then
    # Already has the key (e.g. a previous plugin's component already ran this same script in
    # this installer session, or this is a re-run) - flip it to opted-out in place, preserving
    # whatever else is in the file (in particular, installId).
    sed -i '' -E 's/<VALUE name="telemetryOptedOut" val="[^"]*"\/>/<VALUE name="telemetryOptedOut" val="1"\/>/' "$PROPS_FILE"
elif [ -f "$PROPS_FILE" ]; then
    # File exists (e.g. installId from a prior plugin launch) but has no opt-out key yet - insert
    # one rather than overwriting the file and losing that installId.
    sed -i '' 's/<\/PROPERTIES>/  <VALUE name="telemetryOptedOut" val="1"\/>\
<\/PROPERTIES>/' "$PROPS_FILE"
else
    cat > "$PROPS_FILE" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>

<PROPERTIES>
  <VALUE name="telemetryOptedOut" val="1"/>
</PROPERTIES>
EOF
fi

chown -R "$REAL_USER" "$PROPS_DIR"

exit 0
