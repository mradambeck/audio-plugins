#include "ConcreteFader.h"

namespace
{
    // ---- Fader.module.css ----
    constexpr float componentWidth = 44.0f;
    constexpr float trackWidth = 9.0f;
    constexpr float trackHeight = 132.0f;
    constexpr float capWidth = 32.0f;
    constexpr float capHeight = 16.0f;
    constexpr float gap = 6.0f;
    // Real ~26px tall, not ~11-12px - see ConcreteLookAndFeel::kSmallTextLineHeight's comment.
    const float readoutHeight = ConcreteLookAndFeel::kSmallTextLineHeight;
    const float legendHeight = ConcreteLookAndFeel::kSmallTextLineHeight;

    const juce::Colour trackFill { 0xff0a0a0a };
    const juce::Colour fillTop { 0xff0555eb };
    const juce::Colour fillBottom { 0xff1231de };
    const juce::Colour capFill { 0xff2c2c2c };
    const juce::Colour capBorderTop { 0xff4a4a4a };
    const juce::Colour capBorderBottom { 0xff050505 };
    const juce::Colour capLine { 0xff6a6a6a };
    const juce::Colour readoutColour { 0xff8a8a8a };
    const juce::Colour legendColour { 0xff6a6a6a };
}

ConcreteFader::ConcreteFader (ConcreteLookAndFeel& lookAndFeelIn, juce::String labelIn, float minValueIn, float maxValueIn,
                               std::function<float()> getValueIn, std::function<void(float)> setValueIn,
                               std::function<juce::String(float)> formatValueIn)
    : lookAndFeel (lookAndFeelIn), label (std::move (labelIn)), minValue (minValueIn), maxValue (maxValueIn),
      getValue (std::move (getValueIn)), setValue (std::move (setValueIn)), formatValue (std::move (formatValueIn))
{
    setSize ((int) componentWidth, (int) (trackHeight + gap + readoutHeight + gap + legendHeight));
}

juce::Rectangle<float> ConcreteFader::trackBounds() const noexcept
{
    return juce::Rectangle<float> (trackWidth, trackHeight).withCentre ({ componentWidth * 0.5f, trackHeight * 0.5f });
}

void ConcreteFader::updateFromY (float y)
{
    const auto track = trackBounds();
    const auto fraction = 1.0f - (y - track.getY()) / track.getHeight();
    const auto next = minValue + juce::jlimit (0.0f, 1.0f, fraction) * (maxValue - minValue);
    if (setValue)
        setValue (next);
    repaint();
}

void ConcreteFader::paint (juce::Graphics& g)
{
    const auto value = getValue ? getValue() : minValue;
    const auto norm = juce::jlimit (0.0f, 1.0f, (value - minValue) / (maxValue - minValue));
    const auto track = trackBounds();

    g.setColour (trackFill);
    g.fillRoundedRectangle (track, 4.0f);

    const auto fillHeight = track.getHeight() * norm;
    const auto fillBounds = juce::Rectangle<float> (track.getX(), track.getBottom() - fillHeight, track.getWidth(), fillHeight);
    if (fillHeight > 0.0f)
    {
        juce::ColourGradient gradient (fillTop, fillBounds.getX(), fillBounds.getY(),
                                        fillBottom, fillBounds.getX(), fillBounds.getBottom(), false);
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (fillBounds, 3.0f);
    }

    const auto capCentreY = track.getBottom() - fillHeight;
    const auto capBounds = juce::Rectangle<float> (capWidth, capHeight).withCentre ({ componentWidth * 0.5f, capCentreY });
    g.setColour (capFill);
    g.fillRoundedRectangle (capBounds, 2.0f);
    g.setColour (capBorderTop);
    g.drawLine (capBounds.getX(), capBounds.getY() + 0.5f, capBounds.getRight(), capBounds.getY() + 0.5f, 1.0f);
    g.setColour (capBorderBottom);
    g.drawLine (capBounds.getX(), capBounds.getBottom() - 1.0f, capBounds.getRight(), capBounds.getBottom() - 1.0f, 2.0f);
    g.setColour (capLine);
    g.drawLine (capBounds.getX() + 4.0f, capBounds.getCentreY(), capBounds.getRight() - 4.0f, capBounds.getCentreY(), 1.0f);

    auto textArea = getLocalBounds().toFloat();
    textArea.removeFromTop (trackHeight + gap);
    g.setColour (readoutColour);
    g.setFont (lookAndFeel.getSmallPrintFont (10.0f));
    g.drawText (formatValue ? formatValue (value) : juce::String (value),
                textArea.removeFromTop (readoutHeight), juce::Justification::centred);

    textArea.removeFromTop (gap);
    g.setColour (legendColour);
    g.setFont (lookAndFeel.getSmallPrintFont (9.0f).withExtraKerningFactor (0.11f));
    g.drawText (label.toUpperCase(), textArea, juce::Justification::centred);
}

void ConcreteFader::mouseDown (const juce::MouseEvent& event)
{
    updateFromY (event.position.y);
}

void ConcreteFader::mouseDrag (const juce::MouseEvent& event)
{
    updateFromY (event.position.y);
}
