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
    // Both real ~26px tall, not ~11-13px - see ConcreteLookAndFeel::kSmallTextLineHeight's comment
    // (measured via getBoundingClientRect() on the live mockup, not derived from the 9/10px
    // font-size directly - a plain <div> at these sizes inherits a fixed, much taller line-height
    // from the mockup's root font/line-height rule).
    const float readoutHeight = ConcreteLookAndFeel::kSmallTextLineHeight;
    const float legendHeight = ConcreteLookAndFeel::kSmallTextLineHeight;
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

    // box-shadow: 0 1px 3px rgba(0,0,0,0.6) - a real juce::DropShadow (offset+blur), not the crude
    // flat offset-copy this used to draw (which had no blur at all and read as a hard double-edge
    // rather than a soft shadow - found by Adam: "drop shadows seem to be missing").
    {
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.6f), 3, { 0, 1 });
        juce::Path capPath;
        capPath.addEllipse (capBounds);
        shadow.drawForPath (g, capPath);
    }
    g.setColour (capFill);
    g.fillEllipse (capBounds);

    // border-top/border-bottom on a border-radius:50% element - a browser blends the two colours
    // smoothly all the way around the curve, not as two flat halves meeting at a hard seam (which
    // is what two separately-coloured arcs drew here before, at exactly 9 and 3 o'clock - found by
    // Adam: "black outline on the bottom half, light grey on the top half... on the mock there is
    // a smooth transition"). A single vertical linear gradient stroked around the WHOLE circle
    // approximates that: top-centre samples pure capBorderTop, bottom-centre pure capBorderBottom,
    // and the two side points naturally fall at the gradient's midpoint, same as a real blend.
    juce::Path ring;
    ring.addEllipse (capBounds.reduced (0.5f));
    juce::ColourGradient borderGradient (capBorderTop, centre.x, capBounds.getY(),
                                          capBorderBottom, centre.x, capBounds.getBottom(), false);
    g.setGradientFill (borderGradient);
    g.strokePath (ring, juce::PathStrokeType (1.0f));

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
