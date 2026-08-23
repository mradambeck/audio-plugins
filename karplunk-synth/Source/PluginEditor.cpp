#include "PluginEditor.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency - a new
// PluginProcessor-driven test target can link only juce_audio_processors, no editor/LookAndFeel/
// fonts, matching alloy-bass's KarplunkTests/AlloyTests split (see CMakeLists.txt).
juce::AudioProcessorEditor* KarplunkAudioProcessor::createEditor()
{
    return new KarplunkAudioProcessorEditor(*this);
}

namespace
{
    constexpr int editorWidth = 810;
    constexpr int editorHeight = 260;
    constexpr int sliderSize = 100;
    constexpr int labelHeight = 20;
}

KarplunkAudioProcessorEditor::KarplunkAudioProcessorEditor(KarplunkAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("KARPLUNK", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);

    // Temporary build-verification marker (see KarplunkBuildNumber.h) - lets a build be confirmed
    // as actually loaded rather than a stale cached instance, without needing to reason about it.
    buildNumberLabel.setText("build " + juce::String(karplunkBuildNumber), juce::dontSendNotification);
    buildNumberLabel.setJustificationType(juce::Justification::bottomRight);
    buildNumberLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    buildNumberLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(buildNumberLabel);

    setupSlider(dampingSlider, dampingLabel, "Decay");
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::dampingParamID, dampingSlider);

    setupSlider(outputLevelSlider, outputLevelLabel, "Output Level");
    outputLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::outputLevelParamID, outputLevelSlider);

    setupSlider(brightnessSlider, brightnessLabel, "Pluck Brightness");
    brightnessAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::brightnessParamID, brightnessSlider);

    setupSlider(bowAmountSlider, bowAmountLabel, "Pluck / Bow");
    bowAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::bowAmountParamID, bowAmountSlider);

    setupSlider(structureSlider, structureLabel, "Structure");
    structureAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::structureParamID, structureSlider);

    setupSlider(positionSlider, positionLabel, "Position");
    positionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::positionParamID, positionSlider);

    monoButton.setButtonText("Mono");
    addAndMakeVisible(monoButton);
    monoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::monoParamID, monoButton);

    setSize(editorWidth, editorHeight);
}

KarplunkAudioProcessorEditor::~KarplunkAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void KarplunkAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label,
                                                const juce::String& labelText)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, sliderSize, labelHeight);
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void KarplunkAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour{0xff1a1a1a});
}

void KarplunkAudioProcessorEditor::resized()
{
    buildNumberLabel.setBounds(getLocalBounds().removeFromBottom(16).removeFromRight(72).reduced(4, 0));

    auto bounds = getLocalBounds().reduced(16);

    auto titleRow = bounds.removeFromTop(32);
    monoButton.setBounds(titleRow.removeFromRight(90));
    titleLabel.setBounds(titleRow);
    bounds.removeFromTop(12);

    const auto sliderCount = 6;
    const auto columnWidth = bounds.getWidth() / sliderCount;

    auto layoutColumn = [&](juce::Rectangle<int> columnBounds, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds(columnBounds.removeFromTop(labelHeight));
        slider.setBounds(columnBounds.withSizeKeepingCentre(sliderSize, sliderSize + labelHeight));
    };

    layoutColumn(bounds.removeFromLeft(columnWidth), dampingLabel, dampingSlider);
    layoutColumn(bounds.removeFromLeft(columnWidth), outputLevelLabel, outputLevelSlider);
    layoutColumn(bounds.removeFromLeft(columnWidth), brightnessLabel, brightnessSlider);
    layoutColumn(bounds.removeFromLeft(columnWidth), bowAmountLabel, bowAmountSlider);
    layoutColumn(bounds.removeFromLeft(columnWidth), structureLabel, structureSlider);
    layoutColumn(bounds, positionLabel, positionSlider);
}
