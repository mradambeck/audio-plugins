#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A rotary slider that also carries a live, non-parameter level (in dB, same units/range as the
// slider itself) so the look-and-feel can draw a moving indicator alongside the normal knob
// value -- e.g. Damage's Gate knob uses this to show the incoming signal level against the gate
// threshold. Plugin-agnostic; any plugin needing a live-level knob overlay can use this directly.
class LevelMeterSlider : public juce::Slider
{
public:
    void setLevelDb(float newLevelDb) { levelDb = newLevelDb; }
    float getLevelDb() const { return levelDb; }

private:
    float levelDb = -100.0f;
};
