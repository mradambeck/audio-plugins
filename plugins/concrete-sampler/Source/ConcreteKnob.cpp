#include "ConcreteKnob.h"

namespace
{
    // ---- Knob.module.css ----
    const juce::Colour capFill { 0xff2c2c2c };
    const juce::Colour capBorderTop { 0xff4a4a4a };
    const juce::Colour capBorderBottom { 0xff050505 };
    const juce::Colour indicatorColour { 0xff0555eb };
    const juce::Colour readoutColour { 0xff8a8a8a };
    const juce::Colour legendColour { 0xff6a6a6a };

    constexpr float capDiameter = 32.0f;
    // The mockup's own .Knob{width:44px} is only wide enough for its shortest legends (e.g.
    // "CUTOFF") - .legend has no overflow:hidden, so a browser just lets longer ones like
    // "RESONANCE" spill past that width unclipped. JUCE's drawText() has no such free lunch - it
    // ellipsizes text that doesn't fit the rectangle it's given - and a JUCE Component clips its
    // own paint() to its own bounds regardless of what rectangle you ask drawText for, so matching
    // the mockup's actual (unclipped) look means widening the component itself, not just the text
    // rectangle. 60px comfortably fits "RESONANCE" at 9px with the CSS-matching 1px letter-spacing
    // (measured via fontTools against the real embedded Oswald SemiBold, not eyeballed).
    constexpr float knobWidth = 60.0f;
    constexpr float readoutHeight = 13.0f;
    constexpr float legendHeight = 11.0f;
    constexpr float gap = 6.0f;
}

ConcreteKnob::ConcreteKnob (ConcreteAudioProcessor& processorIn, ConcreteLookAndFeel& lookAndFeelIn,
                             const juce::String& paramID, juce::String labelIn,
                             std::function<juce::String(float)> formatValueIn)
    : processor (processorIn), lookAndFeel (lookAndFeelIn),
      label (std::move (labelIn)), formatValue (std::move (formatValueIn))
{
    param = processor.apvts.getParameter (paramID);
    jassert (param != nullptr);
    setSize ((int) knobWidth, (int) (capDiameter + gap + readoutHeight + gap + legendHeight));
}

void ConcreteKnob::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto capBounds = bounds.withSizeKeepingCentre (capDiameter, capDiameter).withY (bounds.getY());
    const auto centre = capBounds.getCentre();

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (capBounds.translated (0.0f, 1.0f));
    g.setColour (capFill);
    g.fillEllipse (capBounds);

    // Two-tone top/bottom border arc (CSS border-top/border-bottom) rather than a uniform stroke -
    // same technique as ConcretePadGrid's pads, for the same reason (a flat single-colour outline
    // reads noticeably different from the mockup's subtle bevel).
    // JUCE's addCentredArc angles are clockwise from 0 = top-centre. Sweeping -90deg (left/9
    // o'clock) to +90deg (right/3 o'clock) passes through 0 (top) in between, giving the TOP
    // half; +90deg to +270deg passes through 180 (bottom), giving the BOTTOM half - a perfect
    // circle's CSS border-top/border-bottom split along the horizontal diameter, not diagonally.
    const auto halfPi = juce::MathConstants<float>::halfPi;
    const auto pi = juce::MathConstants<float>::pi;
    juce::Path topArc, bottomArc;
    topArc.addCentredArc (centre.x, centre.y, capDiameter * 0.5f - 0.5f, capDiameter * 0.5f - 0.5f,
                           0.0f, -halfPi, halfPi, true);
    bottomArc.addCentredArc (centre.x, centre.y, capDiameter * 0.5f - 0.5f, capDiameter * 0.5f - 0.5f,
                             0.0f, halfPi, halfPi + pi, true);
    g.setColour (capBorderTop);
    g.strokePath (topArc, juce::PathStrokeType (1.0f));
    g.setColour (capBorderBottom);
    g.strokePath (bottomArc, juce::PathStrokeType (1.0f));

    const auto norm = param != nullptr ? param->getValue() : 0.0f;
    const auto angleDeg = minAngleDeg + norm * (maxAngleDeg - minAngleDeg);
    const auto angleRad = juce::degreesToRadians (angleDeg);
    const auto innerPoint = centre.getPointOnCircumference (3.0f, angleRad);
    const auto outerPoint = centre.getPointOnCircumference (14.0f, angleRad);
    g.setColour (indicatorColour);
    g.drawLine ({ innerPoint, outerPoint }, 2.0f);

    auto textArea = bounds;
    textArea.removeFromTop (capDiameter + gap);
    g.setColour (readoutColour);
    g.setFont (lookAndFeel.getSmallPrintFont (10.0f));
    const auto value = param != nullptr ? param->convertFrom0to1 (param->getValue()) : 0.0f;
    g.drawText (formatValue ? formatValue (value) : juce::String (value),
                textArea.removeFromTop (readoutHeight), juce::Justification::centred);

    textArea.removeFromTop (gap);
    g.setColour (legendColour);
    g.setFont (lookAndFeel.getSmallPrintFont (9.0f).withExtraKerningFactor (0.1f));
    g.drawText (label.toUpperCase(), textArea.removeFromTop (legendHeight), juce::Justification::centred);
}

void ConcreteKnob::mouseDown (const juce::MouseEvent& event)
{
    if (param == nullptr)
        return;
    dragStartNorm = param->getValue();
    dragStartY = event.position.y;
}

void ConcreteKnob::mouseDrag (const juce::MouseEvent& event)
{
    if (param == nullptr)
        return;
    const auto deltaY = dragStartY - event.position.y;
    const auto nextNorm = juce::jlimit (0.0f, 1.0f, dragStartNorm + deltaY / dragRangePx);
    param->setValueNotifyingHost (nextNorm);
    repaint();
}
