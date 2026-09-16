#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConcreteLookAndFeel.h"

// MPC-style cursor cluster - translated from ~/code/lcd-mockup's DirectionalPad.tsx/.module.css.
// Purely for moving the selection between the current screen page's editable fields; changing the
// selected field's value is ConcreteDataKnob's job exclusively (same division of labor as the
// mockup - see that component's own comment).
class ConcreteDirectionalPad : public juce::Component
{
public:
    explicit ConcreteDirectionalPad(ConcreteLookAndFeel& lookAndFeelIn);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    std::function<void()> onUp, onDown, onLeft, onRight;

private:
    juce::Rectangle<float> cellBounds(int row, int col) const noexcept;

    ConcreteLookAndFeel& lookAndFeel;
};
