#pragma once

#include "HardwarePanelLookAndFeel.h"

class FluxLookAndFeel : public wildjag::HardwarePanelLookAndFeel
{
public:
    FluxLookAndFeel();

protected:
    // Flux's single wide Blend fader needs a narrower thumb than the base class's 0.8x default
    // (which is tuned for Caverns/Corrosion/Gradient's paired Dry/Wet faders) would otherwise
    // produce -- matches the mockup's .fader-thumb width for this layout.
    float getLinearSliderThumbWidth(juce::Rectangle<float> bounds) const override { return bounds.getWidth() * 0.8f - 20.0f; }
};
