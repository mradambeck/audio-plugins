#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConcreteLookAndFeel.h"
#include "PluginProcessor.h"

// A small flat sampler-style trim knob - translated from ~/code/lcd-mockup's Knob.tsx/.module.css.
// Deliberately NOT wildjag::HardwarePanelLookAndFeel's rack-effect knob (88/112px, flat matte fill,
// fluted rim, static ticks) - see ui-plan.md's "Three divergences": Concrete is a tabletop sampler,
// not a rack effect, and "knobs do not dominate" here the way they do in the rest of the catalog.
// This is the performance strip's shape for the two controls that stay physical (Cutoff/Resonance -
// see Panel.tsx's own comment: "Performance is just Cutoff/Resonance"). Vertical-drag-only, like a
// real pot, matching Knob.tsx's own DRAG_RANGE_PX convention exactly rather than this codebase's
// usual step-accumulator pattern (Fader/DataKnob) - this control maps drag distance CONTINUOUSLY
// to the parameter's normalized value, not in discrete steps.
class ConcreteKnob : public juce::Component
{
public:
    // formatValue receives the parameter's own real (denormalized) value - e.g. Hz for Cutoff.
    ConcreteKnob(ConcreteAudioProcessor& processorIn, ConcreteLookAndFeel& lookAndFeelIn,
                 const juce::String& paramID, juce::String label,
                 std::function<juce::String(float)> formatValue);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    ConcreteAudioProcessor& processor;
    ConcreteLookAndFeel& lookAndFeel;
    juce::RangedAudioParameter* param = nullptr;
    juce::String label;
    std::function<juce::String(float)> formatValue;

    float dragStartNorm = 0.0f;
    float dragStartY = 0.0f;

    static constexpr float dragRangePx = 150.0f; // Knob.tsx's own DRAG_RANGE_PX
    static constexpr float minAngleDeg = -135.0f;
    static constexpr float maxAngleDeg = 135.0f;
};
