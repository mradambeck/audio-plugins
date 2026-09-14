#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConcreteScreen.h"

// The physical half of the "both" soft-key decision (ui-plan.md's decision #3) - translated from
// ~/code/lcd-mockup's SoftKeys.tsx/.module.css. Four real buttons directly beneath the screen's
// own footer band, each hit-testing the same page switch as the on-screen cell above it - no
// visible text (same as the mockup, whose <button> only ever renders an LED span), styled as
// hardware (dark cap, LED) rather than a second copy of the LCD's inverse-video look. Reads/writes
// the ConcreteScreen it's given directly rather than owning any page state of its own - see that
// class's getPageIndex()/setPageIndex()/onPageChanged.
class ConcreteSoftKeys : public juce::Component
{
public:
    explicit ConcreteSoftKeys(ConcreteScreen& screenIn);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> keyBounds(int index) const noexcept;

    ConcreteScreen& screen;

    static constexpr int keyCount = 4;
};
