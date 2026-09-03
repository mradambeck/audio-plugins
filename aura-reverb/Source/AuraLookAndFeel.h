#pragma once

#include "HardwarePanelLookAndFeel.h"

class AuraLookAndFeel : public wildjag::HardwarePanelLookAndFeel
{
public:
    AuraLookAndFeel();

protected:
    // The base class's 0.8x-of-bounds default read too wide on Aura's Wet/Dry faders (Adam,
    // 2026-09-03) - a fixed 36px instead, same "override per plugin" convention as Damage's 0.4x
    // and Flux's 0.8x-20px.
    float getLinearSliderThumbWidth(juce::Rectangle<float>) const override { return 36.0f; }
};
