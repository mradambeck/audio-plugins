#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConcreteLookAndFeel.h"

// A cream-free, sampler-style pushbutton - translated from ~/code/lcd-mockup's
// PanelButton.tsx/.module.css. Flat dark cap, silkscreen legend below it, and (only when
// isLitSource is actually given) an LED dot that lights blue when active - see Panel.tsx's own
// comment on which Session buttons get one. `labelSource`, when given, overrides the fixed label
// each paint (used for "Load Sample"/"Clear Sample" toggling on whether a sample is loaded) -
// evaluated fresh every paint rather than pushed in from outside, so this component never goes
// stale without needing its own change-listening plumbing.
class ConcretePanelButton : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    ConcretePanelButton(ConcreteLookAndFeel& lookAndFeelIn, juce::String label, std::function<void()> onClick,
                         std::function<bool()> isLitSource = nullptr);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<juce::String()> labelSource; // overrides `label` each paint when set

private:
    ConcreteLookAndFeel& lookAndFeel;
    juce::String label;
    std::function<void()> onClick;
    std::function<bool()> isLitSource;
    bool pressed = false;
};
