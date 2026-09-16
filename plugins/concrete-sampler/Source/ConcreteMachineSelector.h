#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConcreteLookAndFeel.h"
#include "PluginProcessor.h"

// The one prominent, distinctly-shaped control on the panel - translated from ~/code/lcd-mockup's
// MachineSelector.tsx/.module.css. A real automatable parameter (machineParamID), not a host
// preset menu, per ui-plan.md's "Parameter allocation" - left/right arrows step through the twelve
// machines plus the "(Custom)" sentinel at index 0, same as the mockup.
//
// The mockup also appends "*" to the name and tracks an isDirty flag (anything besides the loaded
// sample file changing since this machine was selected). That needs every machine-affected
// parameter's change routed through a shared "mark dirty" hook - real, but strictly cosmetic,
// infrastructure this pass doesn't add; the name renders without the dirty asterisk for now (a
// documented scope cut, not an oversight).
class ConcreteMachineSelector : public juce::Component
{
public:
    ConcreteMachineSelector(ConcreteAudioProcessor& processorIn, ConcreteLookAndFeel& lookAndFeelIn);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> leftArrowBounds() const noexcept;
    juce::Rectangle<float> rightArrowBounds() const noexcept;
    juce::Rectangle<float> readoutBounds() const noexcept;
    void step(int delta);

    ConcreteAudioProcessor& processor;
    ConcreteLookAndFeel& lookAndFeel;
};
