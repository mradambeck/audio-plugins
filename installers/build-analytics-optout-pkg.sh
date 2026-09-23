#!/usr/bin/env bash
# Shared helper, sourced by every plugin's installer/build.sh and by installers/build-all.sh, to
# build AnalyticsOptOut-Component.pkg: a payload-less component package whose postinstall script
# (Scripts/analytics-optout-postinstall.sh) sets telemetryOptedOut=1 in the single properties
# file every Wild Jag plugin's TelemetrySettings reads (see
# plugins/common/Telemetry/TelemetrySettings.cpp). Its <choice> is unselected by default in every
# distribution.xml(.in) - telemetry is opt-out, so this component (and its script) only runs at
# all when the user actively checks the box to decline it.
#
# Requires DEVELOPER_ID_INSTALLER (see sign-and-notarize.sh).

_ANALYTICS_OPTOUT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

build_analytics_optout_pkg() {
    local out_dir="$1"
    local scripts_stage
    scripts_stage="$(mktemp -d)"

    cp "$_ANALYTICS_OPTOUT_DIR/Scripts/analytics-optout-postinstall.sh" "$scripts_stage/postinstall"
    chmod +x "$scripts_stage/postinstall"

    pkgbuild \
        --sign "$DEVELOPER_ID_INSTALLER" \
        --nopayload \
        --scripts "$scripts_stage" \
        --identifier "com.wildjag.analytics-optout.pkg" \
        --version "1.0" \
        "$out_dir/AnalyticsOptOut-Component.pkg"

    rm -rf "$scripts_stage"
}
