#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConcreteLookAndFeel.h"

// The endless encoder - translated from ~/code/lcd-mockup's DataKnob.tsx/.module.css. The only
// thing that changes a field's value (ConcreteDirectionalPad only ever moves which field is
// selected - see that component's own comment). Not a positional control like ConcreteKnob/
// ConcreteFader - there's no such thing as "0.7 of the way through Bit Depth" - so the cap just
// keeps spinning cosmetically; onAdjust receives whole +1/-1 steps, matching the mockup's own
// PX_PER_STEP-based drag-to-step conversion exactly (see the .cpp).
class ConcreteDataKnob : public juce::Component
{
public:
    explicit ConcreteDataKnob(ConcreteLookAndFeel& lookAndFeelIn);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    std::function<void(int)> onAdjust;

private:
    ConcreteLookAndFeel& lookAndFeel;

    float rotationDeg = 0.0f;
    float dragStartY = 0.0f;
    float dragStartRotation = 0.0f;
    int stepsSinceDragStart = 0;

    static constexpr float pxPerStep = 10.0f;   // DataKnob.tsx's own PX_PER_STEP
    static constexpr float degreesPerStep = 18.0f; // DataKnob.tsx's own DEGREES_PER_STEP (cosmetic only)
};
