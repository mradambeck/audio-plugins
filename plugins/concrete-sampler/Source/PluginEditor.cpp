#include "PluginEditor.h"

#include <map>

namespace
{
    // Inverse of juce::MidiMessage::getMidiNoteName(note, useSharps, true, octaveNumForMiddleC=3)'s
    // own formula (octave = note/12 + (octaveNumForMiddleC - 5)), so typing "C3" back into the
    // root note box round-trips exactly. Returns -1 on anything unparseable.
    int parseNoteName(const juce::String& text)
    {
        const auto trimmed = text.trim().toUpperCase();
        if (trimmed.isEmpty())
            return -1;

        static const std::map<juce::juce_wchar, int> letterToSemitone {
            { 'C', 0 }, { 'D', 2 }, { 'E', 4 }, { 'F', 5 }, { 'G', 7 }, { 'A', 9 }, { 'B', 11 },
        };

        const auto it = letterToSemitone.find(trimmed[0]);
        if (it == letterToSemitone.end())
            return -1;

        auto semitone = it->second;
        int index = 1;
        if (index < trimmed.length() && trimmed[index] == '#')
        {
            semitone += 1;
            ++index;
        }
        else if (index < trimmed.length() && trimmed[index] == 'B')
        {
            semitone -= 1;
            ++index;
        }

        const auto octaveText = trimmed.substring(index);
        if (octaveText.isEmpty() || !octaveText.containsOnly("-0123456789"))
            return -1;

        const auto octave = octaveText.getIntValue();
        const auto note = (octave + 2) * 12 + semitone;
        return juce::isPositiveAndBelow(note, 128) ? note : -1;
    }
}

ConcreteAudioProcessorEditor::ConcreteAudioProcessorEditor(ConcreteAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      waveformDisplay(p),
      keyboardComponent(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(loadButton);
    loadButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Load a sample...", juce::File(), "*.wav;*.aif;*.aiff");

        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this](const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();
                                      if (file != juce::File())
                                          loadFile(file);
                                  });
    };

    addAndMakeVisible(waveformDisplay);

    addAndMakeVisible(rootNoteLabel);
    rootNoteLabel.attachToComponent(&rootNoteSlider, true);

    rootNoteSlider.setRange(0.0, 127.0, 1.0);
    rootNoteSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    rootNoteSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    rootNoteSlider.textFromValueFunction = [](double value)
    {
        return juce::MidiMessage::getMidiNoteName((int) std::round(value), true, true, 3);
    };
    rootNoteSlider.valueFromTextFunction = [this](const juce::String& text)
    {
        const auto parsed = parseNoteName(text);
        return (double) (parsed >= 0 ? parsed : (int) rootNoteSlider.getValue());
    };
    // setValue() only calls updateText() when the value actually CHANGES (see Slider::Pimpl::
    // setValue() - it's gated behind an equality check against the previous value), so setting it
    // to 60 here (its already-initial value of 0 -> 60 does change, so this alone would normally
    // be enough) is followed by an explicit updateText() as a deliberate belt-and-suspenders: it's
    // what actually forces the text box to re-render with the functions just assigned above,
    // rather than relying on setValue()'s side effect.
    rootNoteSlider.setValue(60.0, juce::dontSendNotification);
    rootNoteSlider.updateText();
    rootNoteSlider.onValueChange = [this]
    {
        processor.setRootNoteForZone(0, (int) rootNoteSlider.getValue());
        waveformDisplay.repaint();
    };
    addAndMakeVisible(rootNoteSlider);

    addAndMakeVisible(resetButton);
    resetButton.onClick = [this]
    {
        // sendNotificationSync (not dontSendNotification) so onValueChange actually fires and
        // calls setRootNoteForZone() - a silent setValue() here would just move the slider's
        // displayed number without ever telling the processor about it. A no-op if root note is
        // already 60, same as the APVTS resets below being no-ops at their own defaults.
        rootNoteSlider.setValue(60.0, juce::sendNotificationSync);

        auto resetParam = [this](const juce::String& paramID)
        {
            if (auto* param = processor.apvts.getParameter(paramID))
                param->setValueNotifyingHost(param->getDefaultValue());
        };
        resetParam(ConcreteAudioProcessor::pitchEngineModeParamID);
        resetParam(ConcreteAudioProcessor::baseRateParamID);
        resetParam(ConcreteAudioProcessor::coarseTuneParamID);
        resetParam(ConcreteAudioProcessor::fineTuneParamID);
        resetParam(ConcreteAudioProcessor::bitDepthParamID);
        resetParam(ConcreteAudioProcessor::quantizerModeParamID);
        resetParam(ConcreteAudioProcessor::captureTransposeParamID);
        resetParam(ConcreteAudioProcessor::captureDriveParamID);
        resetParam(ConcreteAudioProcessor::captureAutoCompensateParamID);
        resetParam(ConcreteAudioProcessor::captureBypassParamID);
        resetParam(ConcreteAudioProcessor::captureIterationsParamID);
    };

    addAndMakeVisible(pitchEngineLabel);
    pitchEngineLabel.attachToComponent(&pitchEngineCombo, true);
    // Populated from the parameter's own choices rather than a hardcoded second copy of the list -
    // see ConcreteAudioProcessor::createParameterLayout()'s AudioParameterChoice.
    if (auto* param = processor.apvts.getParameter(ConcreteAudioProcessor::pitchEngineModeParamID))
        pitchEngineCombo.addItemList(param->getAllValueStrings(), 1);
    addAndMakeVisible(pitchEngineCombo);
    pitchEngineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, ConcreteAudioProcessor::pitchEngineModeParamID, pitchEngineCombo);

    addAndMakeVisible(baseRateLabel);
    baseRateLabel.attachToComponent(&baseRateSlider, true);
    baseRateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    baseRateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    addAndMakeVisible(baseRateSlider);
    baseRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::baseRateParamID, baseRateSlider);

    addAndMakeVisible(coarseTuneLabel);
    coarseTuneLabel.attachToComponent(&coarseTuneSlider, true);
    coarseTuneSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    coarseTuneSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(coarseTuneSlider);
    coarseTuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::coarseTuneParamID, coarseTuneSlider);

    addAndMakeVisible(fineTuneLabel);
    fineTuneLabel.attachToComponent(&fineTuneSlider, true);
    fineTuneSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fineTuneSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(fineTuneSlider);
    fineTuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::fineTuneParamID, fineTuneSlider);

    addAndMakeVisible(bitDepthLabel);
    bitDepthLabel.attachToComponent(&bitDepthSlider, true);
    bitDepthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bitDepthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(bitDepthSlider);
    bitDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::bitDepthParamID, bitDepthSlider);

    addAndMakeVisible(quantizerModeLabel);
    quantizerModeLabel.attachToComponent(&quantizerModeCombo, true);
    if (auto* param = processor.apvts.getParameter(ConcreteAudioProcessor::quantizerModeParamID))
        quantizerModeCombo.addItemList(param->getAllValueStrings(), 1);
    addAndMakeVisible(quantizerModeCombo);
    quantizerModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, ConcreteAudioProcessor::quantizerModeParamID, quantizerModeCombo);

    addAndMakeVisible(captureTransposeLabel);
    captureTransposeLabel.attachToComponent(&captureTransposeSlider, true);
    captureTransposeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    captureTransposeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(captureTransposeSlider);
    captureTransposeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::captureTransposeParamID, captureTransposeSlider);

    addAndMakeVisible(captureDriveLabel);
    captureDriveLabel.attachToComponent(&captureDriveSlider, true);
    captureDriveSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    captureDriveSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(captureDriveSlider);
    captureDriveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::captureDriveParamID, captureDriveSlider);

    addAndMakeVisible(captureAutoCompensateButton);
    captureAutoCompensateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, ConcreteAudioProcessor::captureAutoCompensateParamID, captureAutoCompensateButton);

    addAndMakeVisible(captureBypassButton);
    captureBypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, ConcreteAudioProcessor::captureBypassParamID, captureBypassButton);

    addAndMakeVisible(captureIterationsLabel);
    captureIterationsLabel.attachToComponent(&captureIterationsSlider, true);
    captureIterationsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    captureIterationsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    addAndMakeVisible(captureIterationsSlider);
    captureIterationsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::captureIterationsParamID, captureIterationsSlider);

    addAndMakeVisible(keyboardComponent);

    setSize(700, 560);
}

ConcreteAudioProcessorEditor::~ConcreteAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void ConcreteAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1f20));
}

void ConcreteAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    auto topRow = bounds.removeFromTop(30);
    loadButton.setBounds(topRow.removeFromLeft(80));
    topRow.removeFromLeft(90); // space for the root note label, attached to the left of its slider
    rootNoteSlider.setBounds(topRow.removeFromLeft(200));
    topRow.removeFromLeft(20);
    resetButton.setBounds(topRow.removeFromLeft(80));

    bounds.removeFromTop(8);

    auto pitchRow = bounds.removeFromTop(28);
    pitchRow.removeFromLeft(90); // space for the "Pitch Engine" label
    pitchEngineCombo.setBounds(pitchRow.removeFromLeft(220));

    bounds.removeFromTop(8);

    auto tuneRow = bounds.removeFromTop(28);
    tuneRow.removeFromLeft(90); // space for the "Base Rate" label
    baseRateSlider.setBounds(tuneRow.removeFromLeft(150));
    tuneRow.removeFromLeft(90); // "Coarse Tune" label
    coarseTuneSlider.setBounds(tuneRow.removeFromLeft(130));
    tuneRow.removeFromLeft(80); // "Fine Tune" label
    fineTuneSlider.setBounds(tuneRow.removeFromLeft(130));

    bounds.removeFromTop(8);

    auto quantRow = bounds.removeFromTop(28);
    quantRow.removeFromLeft(90); // "Bit Depth" label
    bitDepthSlider.setBounds(quantRow.removeFromLeft(150));
    quantRow.removeFromLeft(110); // "Quantizer Mode" label
    quantizerModeCombo.setBounds(quantRow.removeFromLeft(150));

    bounds.removeFromTop(8);

    auto captureRow1 = bounds.removeFromTop(28);
    captureRow1.removeFromLeft(120); // "Capture Transpose" label
    captureTransposeSlider.setBounds(captureRow1.removeFromLeft(150));
    captureRow1.removeFromLeft(100); // "Capture Drive" label
    captureDriveSlider.setBounds(captureRow1.removeFromLeft(130));

    bounds.removeFromTop(8);

    auto captureRow2 = bounds.removeFromTop(28);
    captureAutoCompensateButton.setBounds(captureRow2.removeFromLeft(160));
    captureRow2.removeFromLeft(10);
    captureBypassButton.setBounds(captureRow2.removeFromLeft(140));
    captureRow2.removeFromLeft(130); // "Capture Iterations" label
    captureIterationsSlider.setBounds(captureRow2.removeFromLeft(100));

    bounds.removeFromTop(10);

    keyboardComponent.setBounds(bounds.removeFromBottom(80));

    bounds.removeFromBottom(10);
    waveformDisplay.setBounds(bounds);
}

bool ConcreteAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.hasFileExtension("wav;aif;aiff"))
            return true;
    }
    return false;
}

void ConcreteAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.hasFileExtension("wav;aif;aiff"))
        {
            loadFile(file);
            break;
        }
    }
}

void ConcreteAudioProcessorEditor::loadFile(const juce::File& file)
{
    // Called directly on the message thread - acceptable for Phase 1's utility UI (no progress/
    // background-thread machinery yet; see this class's own header comment on what Phase 8 adds).
    // loadSample() itself is documented as real-time-unsafe/not-for-the-audio-thread, which this
    // isn't.
    if (processor.loadSample(file))
    {
        rootNoteSlider.setValue(60.0, juce::dontSendNotification);
        waveformDisplay.repaint();
    }
}

juce::AudioProcessorEditor* ConcreteAudioProcessor::createEditor()
{
    return new ConcreteAudioProcessorEditor(*this);
}
