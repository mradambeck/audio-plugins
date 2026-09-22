#pragma once

#include <juce_core/juce_core.h>

namespace wildjag::Telemetry
{
    // Fire-and-forget: every send happens on a short-lived background thread with a short
    // connect/read timeout and no retry, and any failure (opted out, no network, DNS failure,
    // timeout, non-2xx response) is silently discarded. Safe to call unconditionally from a
    // PluginProcessor constructor or setCurrentProgram() on the message thread - it never blocks
    // the caller, never throws, and has no effect on plugin loading or audio if it fails.
    void sendLaunch(const juce::String& plugin, const juce::String& version);
    void sendPresetLoaded(const juce::String& plugin, const juce::String& version,
                           const juce::String& presetName);
}
