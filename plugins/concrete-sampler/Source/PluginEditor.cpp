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

    addAndMakeVisible(keyboardComponent);

    setSize(700, 460);
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
