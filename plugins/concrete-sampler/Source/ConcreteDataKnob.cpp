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

    // box-shadow: 0 4px 8px rgba(0,0,0,0.6) - a real juce::DropShadow, not a crude flat offset
    // copy (see ConcreteKnob's own comment on why that read as a hard double-edge, not a shadow).
    {
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.6f), 8, { 0, 4 });
        juce::Path capPath;
        capPath.addEllipse (capBounds);
        shadow.drawForPath (g, capPath);
    }
    g.setColour (capFill);
    g.fillEllipse (capBounds);

    // border-top/border-bottom on a border-radius:50% element blends smoothly around the curve in
    // a browser, not as two flat-coloured halves meeting at a hard seam - see ConcreteKnob's own
    // comment on this exact bug (found by Adam: "black outline on the bottom half, light grey on
    // the top half"). A single vertical gradient stroked around the whole circle approximates the
    // real blend; stroke width splits the difference between the CSS 1px top/4px bottom values
    // since a gradient-filled stroke can't easily vary its own width around the path.
    juce::Path ring;
    ring.addEllipse (capBounds.reduced (1.0f));
    juce::ColourGradient borderGradient (capBorderTop, centre.x, capBounds.getY(),
                                          capBorderBottom, centre.x, capBounds.getBottom(), false);
    g.setGradientFill (borderGradient);
    g.strokePath (ring, juce::PathStrokeType (2.0f));

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
