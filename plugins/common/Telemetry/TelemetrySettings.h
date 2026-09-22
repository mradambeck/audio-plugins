#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace wildjag
{
    // The single opt-out flag + random install ID that wildjag-plugin-installer writes once, at
    // ~/Library/Application Support/WildJag/telemetry.properties on macOS (JUCE's per-platform
    // equivalent elsewhere) - shared by every Wild Jag plugin rather than each keeping its own
    // copy, so accepting or declining telemetry happens once, at install time, not per plugin.
    //
    // Opt-out model: telemetry defaults to enabled. A plugin run without ever going through the
    // installer (built from source, or installed before this existed) still gets a fresh install
    // ID generated and persisted here on first read, and is treated as opted-in until the
    // installer's checkbox says otherwise - there is deliberately no separate "first run" state.
    class TelemetrySettings
    {
    public:
        static bool isOptedOut();

        // Empty return means the settings file couldn't be read or created (e.g. a permissions
        // issue) - callers must treat that the same as opted-out rather than sending an event with
        // no install ID.
        static juce::String getInstallId();

    private:
        // Returns nullptr if the file couldn't be created (e.g. a permissions issue) - every
        // caller must treat that as "telemetry unavailable" rather than crashing or retrying.
        static juce::PropertiesFile* getFile();
    };
}
