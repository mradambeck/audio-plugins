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

    // Damage's fader is slimmer than Caverns/Corrosion/Gradient's paired Dry/Wet faders (see
    // the mockup's .fader-thumb) -- 0.4x bounds width instead of the base class's 0.8x default.
    float getLinearSliderThumbWidth(juce::Rectangle<float> bounds) const override { return bounds.getWidth() * 0.4f; }

private:
    // Live gate-level meter tick (Gate knob only) -- Damage-specific, not part of the base knob
    // spec. A neutral audio-meter green, deliberately not the plugin's red accent/LED hue so it
    // doesn't get misread as an "engaged" indicator.
    juce::Colour meterLiveColour{0xff5be0b3};
};
