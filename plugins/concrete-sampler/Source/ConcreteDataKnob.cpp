#include "ConcreteDataKnob.h"

#include <cmath>

namespace
{
    // ---- DataKnob.module.css ----
    constexpr float capDiameter = 96.0f;
    // .cap{border-top:1px;border-bottom:4px} on top of an explicit 96px width/height with no
    // box-sizing:border-box reset - the border ADDS to the rendered box (content-box default),
    // same pattern already established for the LCD screen's MainContent/Footer and for
    // ConcreteKnob's own 32px cap - so the real layout footprint is 96+1+4=101, not 96. The
    // DRAWN circle stays 96 (that's the true visual diameter); this only affects how much space
    // is reserved below it before the legend.
    constexpr float capLayoutHeight = capDiameter + 1.0f + 4.0f;
    constexpr float gap = 6.0f;
    // Real ~26px tall, not ~11px - see ConcreteLookAndFeel::kSmallTextLineHeight's comment.
    const float legendHeight = ConcreteLookAndFeel::kSmallTextLineHeight;

    const juce::Colour capFill { 0xff2c2c2c };
    const juce::Colour capBorderTop { 0xff4a4a4a };
    const juce::Colour capBorderBottom { 0xff050505 };
    const juce::Colour indicatorColour { 0xff0555eb };
    const juce::Colour legendColour { 0xff6a6a6a };
}

ConcreteDataKnob::ConcreteDataKnob (ConcreteLookAndFeel& lookAndFeelIn) : lookAndFeel (lookAndFeelIn)
{
    setSize ((int) capDiameter, (int) (capLayoutHeight + gap + legendHeight));
}

void ConcreteDataKnob::paint (juce::Graphics& g)
{
    const auto capBounds = juce::Rectangle<float> (0.0f, 0.0f, capDiameter, capDiameter);
    const auto centre = capBounds.getCentre();

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (capBounds.translated (0.0f, 2.0f));
    g.setColour (capFill);
    g.fillEllipse (capBounds);

    // Two-tone top/bottom border arc (CSS border-top/border-bottom-4px) - same technique/angle
    // convention as ConcreteKnob's cap (see that class's own comment on JUCE's clockwise-from-top
    // addCentredArc angles).
    const auto halfPi = juce::MathConstants<float>::halfPi;
    const auto pi = juce::MathConstants<float>::pi;
    juce::Path topArc, bottomArc;
    const auto r = capDiameter * 0.5f - 1.0f;
    topArc.addCentredArc (centre.x, centre.y, r, r, 0.0f, -halfPi, halfPi, true);
    bottomArc.addCentredArc (centre.x, centre.y, r, r, 0.0f, halfPi, halfPi + pi, true);
    g.setColour (capBorderTop);
    g.strokePath (topArc, juce::PathStrokeType (1.0f));
    g.setColour (capBorderBottom);
    g.strokePath (bottomArc, juce::PathStrokeType (3.0f));

    // .indicator: a 5x32 bar near the cap's top edge, rotating continuously - an endless encoder
    // has no fixed "value angle" the way ConcreteKnob's pointer does, so this is purely cosmetic
    // drag feedback (rotationDeg only ever reflects recent drag distance, never a parameter value).
    const auto angleRad = juce::degreesToRadians (rotationDeg);
    const auto innerPoint = centre.getPointOnCircumference (8.0f, angleRad);
    const auto outerPoint = centre.getPointOnCircumference (36.0f, angleRad);
    g.setColour (indicatorColour);
    g.drawLine ({ innerPoint, outerPoint }, 5.0f);

    auto legendArea = getLocalBounds().toFloat();
    legendArea.removeFromTop (capLayoutHeight + gap);
    g.setColour (legendColour);
    g.setFont (lookAndFeel.getSmallPrintFont (9.0f).withExtraKerningFactor (0.11f));
    g.drawText ("VALUE", legendArea, juce::Justification::centred);
}

void ConcreteDataKnob::mouseDown (const juce::MouseEvent& event)
{
    dragStartY = event.position.y;
    dragStartRotation = rotationDeg;
    stepsSinceDragStart = 0;
}

void ConcreteDataKnob::mouseDrag (const juce::MouseEvent& event)
{
    const auto deltaY = dragStartY - event.position.y;
    rotationDeg = dragStartRotation + deltaY * (degreesPerStep / pxPerStep);
    repaint();

    const auto totalSteps = (int) std::trunc (deltaY / pxPerStep);
    const auto stepsToApply = totalSteps - stepsSinceDragStart;
    if (stepsToApply != 0 && onAdjust)
    {
        onAdjust (stepsToApply);
        stepsSinceDragStart = totalSteps;
    }
}
