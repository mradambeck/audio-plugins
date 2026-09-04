#pragma once

#include "HardwarePanelLookAndFeel.h"
#include "LevelMeterSlider.h"

class DamageLookAndFeel : public wildjag::HardwarePanelLookAndFeel
{
public:
    DamageLookAndFeel();

protected:
    void paintRotarySliderOverlay(juce::Graphics&, juce::Point<float> centre, float radius,
                                   float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    float getLinearSliderThumbWidth(juce::Rectangle<float>) const override { return 36.0f; }

private:
    // Live gate-level meter tick (Gate knob only) -- Damage-specific, not part of the base knob
    // spec. A neutral audio-meter green, deliberately not the plugin's red accent/LED hue so it
    // doesn't get misread as an "engaged" indicator.
    juce::Colour meterLiveColour{0xff5be0b3};
};
