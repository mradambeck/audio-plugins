#pragma once

#include "HardwarePanelLookAndFeel.h"

class InhaltLookAndFeel : public wildjag::HardwarePanelLookAndFeel
{
public:
    InhaltLookAndFeel();

protected:
    // Same wrinkle as Aura's own override: the mockup's fader visual track is a narrow ~12-22px
    // SVG drawn inside a much wider component (the built-in value textbox needs room for
    // "200.0%"-length text - see PluginEditor.cpp's own faderCellWidth comment), so the base
    // class's 0.8x-of-bounds default reads far too wide. Fixed 36px instead, matching Aura's own
    // Wet/Dry faders exactly (this mockup's fader SVG was copied from Aura's directly).
    float getLinearSliderThumbWidth(juce::Rectangle<float>) const override { return 36.0f; }
};
