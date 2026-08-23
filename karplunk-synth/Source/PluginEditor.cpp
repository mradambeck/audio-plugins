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
    constexpr int editorWidth = 1150;
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

    setupSlider(glideTimeSlider, glideTimeLabel, "Glide Time");
    glideTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::glideTimeParamID, glideTimeSlider);

    setupSlider(waveshapeSlider, waveshapeLabel, "Waveshape");
    waveshapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::waveshapeParamID, waveshapeSlider);

    // The runtime dropdown for the Waveshaper seam's four concrete types (see
    // KarplunkWaveshaper.h's own comment for why this one seam is a runtime choice rather than a
    // compile-time template parameter like the other three). Item IDs are 1-based (JUCE
    // ComboBox convention) and map to AudioParameterChoice's 0-based indices in order - "Fold" is
    // index 0, "Fuzz" is index 1, "Saturate" is index 2, "BitCrush" is index 3, matching
    // createParameterLayout()'s own choice list.
    waveshaperTypeBox.addItem("Fold", 1);
    waveshaperTypeBox.addItem("Fuzz", 2);
    waveshaperTypeBox.addItem("Saturate", 3);
    waveshaperTypeBox.addItem("BitCrush", 4);
    addAndMakeVisible(waveshaperTypeBox);
    waveshaperTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::waveshaperTypeParamID, waveshaperTypeBox);

    setupSlider(ringModAmountSlider, ringModAmountLabel, "Ring Mod");
    ringModAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::ringModAmountParamID, ringModAmountSlider);

    setupSlider(ringModFrequencySlider, ringModFrequencyLabel, "Ring Mod Freq");
    ringModFrequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::ringModFrequencyParamID, ringModFrequencySlider);

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

    const auto sliderCount = 10;
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
    layoutColumn(bounds.removeFromLeft(columnWidth), positionLabel, positionSlider);
    layoutColumn(bounds.removeFromLeft(columnWidth), glideTimeLabel, glideTimeSlider);

    auto waveshapeColumn = bounds.removeFromLeft(columnWidth);
    waveshaperTypeBox.setBounds(waveshapeColumn.removeFromBottom(labelHeight + 4).reduced(4, 2));
    layoutColumn(waveshapeColumn, waveshapeLabel, waveshapeSlider);

    layoutColumn(bounds.removeFromLeft(columnWidth), ringModAmountLabel, ringModAmountSlider);
    layoutColumn(bounds, ringModFrequencyLabel, ringModFrequencySlider);
}
