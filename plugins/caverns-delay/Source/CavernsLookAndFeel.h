#pragma once

#include "HardwarePanelLookAndFeel.h"

class CavernsLookAndFeel : public wildjag::HardwarePanelLookAndFeel
{
public:
    CavernsLookAndFeel();

protected:
    float getLinearSliderThumbWidth(juce::Rectangle<float>) const override { return 36.0f; }
};
