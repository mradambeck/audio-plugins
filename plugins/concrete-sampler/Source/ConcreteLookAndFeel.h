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

    // The panel chrome's small-caps section heading (~/code/lcd-mockup's SilkscreenLabel.tsx) -
    // shared by every hardware-style component that needs one (Machine/Performance/Volume/Edit/
    // Session), rather than each of those components reimplementing the same text+rule painting.
    // `centered` matches SilkscreenLabel's own prop: true draws centered text with no rule (used
    // for a label that sits directly above one specific box it names), false draws left-aligned
    // text followed by a hairline rule filling the rest of `area` (used for a label heading a wide
    // section).
    void paintSilkscreenLabel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, bool centered) const;

    // The mockup's ~/code/lcd-mockup/src/index.css sets `:root{font:18px/145%}` - a PERCENTAGE
    // line-height, which (unlike a bare unitless number) computes to an absolute px length AT THE
    // ROOT and is inherited as that fixed length by every descendant regardless of the
    // descendant's OWN smaller font-size (a real CSS inheritance quirk, confirmed via
    // getBoundingClientRect() on the live mockup - see the code-review round that found this,
    // 2026-09-14). Concretely: every plain (non-<button>) text line at 9/10/13px - SilkscreenLabel
    // captions, Knob/Fader readouts and legends, MachineSelector's name/sub, DirectionalPad/
    // DataKnob legends - renders at 18*1.45=26.1px tall, NOT a height derived from its own
    // font-size the way a first pass at these components assumed (that pass used ~11-13px and was
    // roughly HALF the real value across the board - the root cause of "everything except the LCD
    // interior looks wrong" once the panel chrome was built). `<button>`-element text is exempt
    // (browsers reset line-height to "normal" on form controls), which is why PanelButton's own
    // label was never affected by this.
    static constexpr float kSmallTextLineHeight = 26.0f;
    // SilkscreenLabel's own combined footprint: the line height above plus its own
    // margin-bottom:10px (see SilkscreenLabel.module.css) - the height every section label
    // reserves before its content starts.
    static constexpr float kSilkscreenLabelHeight = kSmallTextLineHeight + 10.0f;

private:
    juce::Typeface::Ptr lcdTypeface;
};
