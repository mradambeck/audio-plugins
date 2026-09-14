#pragma once

#include "HardwarePanelLookAndFeel.h"

// Concrete's screen font (VCR OSD Mono) is deliberately NOT part of wildjag::HardwarePanelTheme -
// see ui-plan.md's "Three divergences": the LCD screen is this plugin's own thing, not shared
// catalog convention the way the display/small-print typefaces are, so it's loaded and exposed
// entirely here rather than growing the shared theme struct for one plugin's screen.
class ConcreteLookAndFeel : public wildjag::HardwarePanelLookAndFeel
{
public:
    ConcreteLookAndFeel();

    // For ConcreteScreen's hand-painted LCD text (page labels, field values, the boot sequence) -
    // see getLcdFont()'s own .cpp comment for the height-correction ratio this font needs.
    juce::Font getLcdFont(float height) const;

private:
    juce::Typeface::Ptr lcdTypeface;
};
