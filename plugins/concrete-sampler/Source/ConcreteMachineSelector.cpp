#include "ConcreteMachineSelector.h"

#include <cmath>

#include "ConcreteMachines.h"

namespace
{
    // ---- MachineSelector.module.css ----
    const float labelHeight = ConcreteLookAndFeel::kSilkscreenLabelHeight;
    // .name(13px)+.sub(9px) are both plain <div>s, so BOTH inherit the mockup's fixed ~26px line-
    // height (see ConcreteLookAndFeel::kSmallTextLineHeight) rather than sizing to their own
    // font - two of those (52) + .readout's own 4px top/bottom padding (8) + .sub's 1px margin-top
    // = 61, confirmed against the live mockup's real rendered row height (not the ~36px a first
    // pass assumed from eyeballing the font sizes alone).
    const float rowHeight = 2.0f * ConcreteLookAndFeel::kSmallTextLineHeight + 8.0f + 1.0f;
    constexpr float arrowWidth = 26.0f;
    constexpr float rowGap = 6.0f;

    const juce::Colour arrowFill { 0xff232323 };
    const juce::Colour arrowBorderTop { 0xff3a3a3a };
    const juce::Colour arrowBorderBottom { 0xff050505 };
    const juce::Colour arrowText { 0xff9a9a9a };
    const juce::Colour readoutFill { 0xff0a0a0a };
    const juce::Colour nameColour { 0xffc4cef9 };
    const juce::Colour subColour { 0xff5a72b0 };
}

ConcreteMachineSelector::ConcreteMachineSelector (ConcreteAudioProcessor& processorIn, ConcreteLookAndFeel& lookAndFeelIn)
    : processor (processorIn), lookAndFeel (lookAndFeelIn)
{
    setSize (300, (int) (labelHeight + rowHeight));
}

juce::Rectangle<float> ConcreteMachineSelector::leftArrowBounds() const noexcept
{
    return { 0.0f, labelHeight, arrowWidth, rowHeight };
}

juce::Rectangle<float> ConcreteMachineSelector::rightArrowBounds() const noexcept
{
    return { (float) getWidth() - arrowWidth, labelHeight, arrowWidth, rowHeight };
}

juce::Rectangle<float> ConcreteMachineSelector::readoutBounds() const noexcept
{
    return { arrowWidth + rowGap, labelHeight, (float) getWidth() - 2.0f * (arrowWidth + rowGap), rowHeight };
}

void ConcreteMachineSelector::step (int delta)
{
    auto* param = processor.apvts.getParameter (ConcreteAudioProcessor::machineParamID);
    if (param == nullptr)
        return;
    const auto numChoices = param->getNumSteps();
    const auto current = (int) std::lround (param->convertFrom0to1 (param->getValue()));
    const auto next = juce::jlimit (0, numChoices - 1, current + delta);
    param->setValueNotifyingHost (param->convertTo0to1 ((float) next));
    repaint();
}

void ConcreteMachineSelector::paint (juce::Graphics& g)
{
    lookAndFeel.paintSilkscreenLabel (g, getLocalBounds().toFloat().withHeight (labelHeight), "Machine", false);

    auto* param = processor.apvts.getParameter (ConcreteAudioProcessor::machineParamID);
    const auto index = param != nullptr ? (int) std::lround (param->convertFrom0to1 (param->getValue())) : 0;
    const auto numChoices = param != nullptr ? param->getNumSteps() : 1;

    for (const bool isLeft : { true, false })
    {
        const auto bounds = isLeft ? leftArrowBounds() : rightArrowBounds();
        const bool disabled = isLeft ? index <= 0 : index >= numChoices - 1;

        g.setColour (arrowFill.withAlpha (disabled ? 0.5f : 1.0f));
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (arrowBorderTop.withAlpha (disabled ? 0.5f : 1.0f));
        g.drawLine (bounds.getX(), bounds.getY() + 0.5f, bounds.getRight(), bounds.getY() + 0.5f, 1.0f);
        g.setColour (arrowBorderBottom.withAlpha (disabled ? 0.5f : 1.0f));
        g.drawLine (bounds.getX(), bounds.getBottom() - 0.5f, bounds.getRight(), bounds.getBottom() - 0.5f, 1.0f);

        g.setColour (arrowText.withAlpha (disabled ? 0.3f : 1.0f));
        g.setFont (lookAndFeel.getSmallPrintFont (11.0f));
        g.drawText (isLeft ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x80"))
                           : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6")),
                    bounds, juce::Justification::centred);
    }

    const auto readout = readoutBounds();
    g.setColour (readoutFill);
    g.fillRoundedRectangle (readout, 3.0f);

    const auto displayName = param != nullptr ? param->getCurrentValueAsText() : juce::String();
    juce::String subline (" ");
    if (index > 0 && (size_t) (index - 1) < getConcreteMachines().size())
    {
        const auto& machine = getConcreteMachines()[(size_t) (index - 1)];
        juce::String pitchEngineName;
        if (auto* pitchParam = processor.apvts.getParameter (ConcreteAudioProcessor::pitchEngineModeParamID))
        {
            const auto strings = pitchParam->getAllValueStrings();
            const auto modeIndex = (int) machine.pitchEngineMode;
            if (modeIndex >= 0 && modeIndex < strings.size())
                pitchEngineName = strings[modeIndex];
        }
        subline = pitchEngineName + " \xc2\xb7 " + juce::String (machine.bitDepthBits) + "-bit";
    }

    auto textArea = readout.reduced (10.0f, 4.0f);
    g.setColour (nameColour);
    g.setFont (lookAndFeel.getSmallPrintFont (13.0f));
    g.drawText (displayName, textArea.removeFromTop (ConcreteLookAndFeel::kSmallTextLineHeight + 1.0f),
                juce::Justification::centredLeft);
    g.setColour (subColour);
    g.setFont (lookAndFeel.getSmallPrintFont (9.0f));
    g.drawText (subline, textArea, juce::Justification::topLeft);
}

void ConcreteMachineSelector::mouseDown (const juce::MouseEvent& event)
{
    if (leftArrowBounds().contains (event.position))
        step (-1);
    else if (rightArrowBounds().contains (event.position))
        step (1);
}
