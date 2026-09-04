#pragma once

#include "HardwarePanelLookAndFeel.h"

class GradientLookAndFeel : public wildjag::HardwarePanelLookAndFeel
{
public:
    GradientLookAndFeel();

protected:
    // Matches the mockup's .knob-value (10px) -- every other plugin uses the base class's
    // 10.5px default.
    float getSliderTextBoxFontHeight() const override { return 10.0f; }
};
