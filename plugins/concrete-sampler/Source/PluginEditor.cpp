#include "PluginEditor.h"

ConcreteAudioProcessorEditor::ConcreteAudioProcessorEditor(ConcreteAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      concreteScreen(p, lookAndFeel),
      cutoffKnob(p, lookAndFeel, ConcreteAudioProcessor::filterCutoffParamID, "Cutoff",
                 [](float v) { return v >= 1000.0f ? juce::String(v / 1000.0f, 1) + "k" : juce::String(juce::roundToInt(v)) + "Hz"; }),
      resonanceKnob(p, lookAndFeel, ConcreteAudioProcessor::filterResonanceParamID, "Resonance",
                    [](float v) { return juce::String(v, 2); }),
      padGrid(p, lookAndFeel)
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

    addAndMakeVisible(concreteScreen);

    addAndMakeVisible(resetButton);
    resetButton.onClick = [this]
    {
        // No UI controls left to reset here for these two - ConcreteScreen just reads zone state
        // straight from the processor every repaint, so setting it directly is enough (a no-op if
        // root note/one-shot are already at these values, same as the APVTS resets below being
        // no-ops at their own defaults).
        processor.setRootNoteForZone(0, 60);
        processor.setOneShotForZone(0, false);

        auto resetParam = [this](const juce::String& paramID)
        {
            if (auto* param = processor.apvts.getParameter(paramID))
                param->setValueNotifyingHost(param->getDefaultValue());
        };
        // Resets to "(Custom)" - a no-op application-wise (see machineParamID's own comment), but
        // still worth doing so the combo itself visibly reflects "nothing selected" after Reset,
        // matching every other control here reverting to ITS OWN default.
        resetParam(ConcreteAudioProcessor::machineParamID);
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
        resetParam(ConcreteAudioProcessor::filterModelParamID);
        resetParam(ConcreteAudioProcessor::filterCutoffParamID);
        resetParam(ConcreteAudioProcessor::filterResonanceParamID);
        resetParam(ConcreteAudioProcessor::filterEnvAmountParamID);
        resetParam(ConcreteAudioProcessor::filterKeyTrackParamID);
        resetParam(ConcreteAudioProcessor::voiceCountParamID);
        resetParam(ConcreteAudioProcessor::ampEnvelopeModeParamID);
    };

    addAndMakeVisible(machineLabel);
    machineLabel.attachToComponent(&machineCombo, true);
    if (auto* param = processor.apvts.getParameter(ConcreteAudioProcessor::machineParamID))
        machineCombo.addItemList(param->getAllValueStrings(), 1);
    addAndMakeVisible(machineCombo);
    machineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, ConcreteAudioProcessor::machineParamID, machineCombo);

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

    addAndMakeVisible(filterModelLabel);
    filterModelLabel.attachToComponent(&filterModelCombo, true);
    if (auto* param = processor.apvts.getParameter(ConcreteAudioProcessor::filterModelParamID))
        filterModelCombo.addItemList(param->getAllValueStrings(), 1);
    addAndMakeVisible(filterModelCombo);
    filterModelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, ConcreteAudioProcessor::filterModelParamID, filterModelCombo);

    addAndMakeVisible(cutoffKnob);
    addAndMakeVisible(resonanceKnob);

    addAndMakeVisible(filterEnvAmountLabel);
    filterEnvAmountLabel.attachToComponent(&filterEnvAmountSlider, true);
    filterEnvAmountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    filterEnvAmountSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(filterEnvAmountSlider);
    filterEnvAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::filterEnvAmountParamID, filterEnvAmountSlider);

    addAndMakeVisible(filterKeyTrackLabel);
    filterKeyTrackLabel.attachToComponent(&filterKeyTrackSlider, true);
    filterKeyTrackSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    filterKeyTrackSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible(filterKeyTrackSlider);
    filterKeyTrackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::filterKeyTrackParamID, filterKeyTrackSlider);

    addAndMakeVisible(voiceCountLabel);
    voiceCountLabel.attachToComponent(&voiceCountSlider, true);
    voiceCountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    voiceCountSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    addAndMakeVisible(voiceCountSlider);
    voiceCountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, ConcreteAudioProcessor::voiceCountParamID, voiceCountSlider);

    addAndMakeVisible(ampEnvelopeModeLabel);
    ampEnvelopeModeLabel.attachToComponent(&ampEnvelopeModeCombo, true);
    if (auto* param = processor.apvts.getParameter(ConcreteAudioProcessor::ampEnvelopeModeParamID))
        ampEnvelopeModeCombo.addItemList(param->getAllValueStrings(), 1);
    addAndMakeVisible(ampEnvelopeModeCombo);
    ampEnvelopeModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, ConcreteAudioProcessor::ampEnvelopeModeParamID, ampEnvelopeModeCombo);

    addAndMakeVisible(padGrid);

    setSize(980, 726);
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
    auto fullBounds = getLocalBounds();

    // Reserved up front, as genuinely EXTRA width (see setSize() below), not scavenged from
    // whatever looked empty in the existing rows - those turned out to already use nearly this
    // window's full original 700px in several rows (Base Rate/Coarse/Fine Tune alone need ~670),
    // so there was nowhere to actually fit a 260px pad grid without it overlapping something.
    // Growing the window WIDER for this rather than TALLER (which the pad grid's own ~280px
    // height would otherwise have demanded) keeps it closer to fitting on a laptop screen - still
    // just Phase 1's utility layout, not the real compact panel this becomes later.
    auto padArea = fullBounds.removeFromRight(280);
    padGrid.setTopLeftPosition(padArea.getX() + 10, 48);

    auto bounds = fullBounds.reduced(10);

    auto topRow = bounds.removeFromTop(30);
    loadButton.setBounds(topRow.removeFromLeft(80));
    topRow.removeFromLeft(20);
    resetButton.setBounds(topRow.removeFromLeft(80));

    bounds.removeFromTop(8);

    // ConcreteScreen is fixed at the mockup's own 500x250 - centered in whatever width is left
    // rather than stretched, so it stays pixel-accurate for screenshot diffing regardless of this
    // throwaway editor's own window size.
    concreteScreen.setTopLeftPosition((bounds.getWidth() - concreteScreen.getWidth()) / 2, bounds.getY());
    bounds.removeFromTop(concreteScreen.getHeight() + 8);


    auto machineRow = bounds.removeFromTop(28);
    machineRow.removeFromLeft(90); // space for the "Machine" label
    machineCombo.setBounds(machineRow.removeFromLeft(220));

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

    bounds.removeFromTop(8);

    auto filterRow1 = bounds.removeFromTop(28);
    filterRow1.removeFromLeft(90); // "Filter Model" label
    filterModelCombo.setBounds(filterRow1.removeFromLeft(220));

    bounds.removeFromTop(8);

    // Taller than the other rows here (68px, not 28) - ConcreteKnob draws its own label/readout
    // below its cap rather than needing a separate juce::Label the way every Attachment-based
    // control on this page still does, but that means it needs real vertical room, not a single
    // slider-height row.
    auto filterRow2 = bounds.removeFromTop (cutoffKnob.getHeight());
    cutoffKnob.setTopLeftPosition (filterRow2.getX(), filterRow2.getY());
    resonanceKnob.setTopLeftPosition (filterRow2.getX() + cutoffKnob.getWidth() + 20, filterRow2.getY());

    bounds.removeFromTop(8);

    auto filterRow3 = bounds.removeFromTop(28);
    filterRow3.removeFromLeft(120); // "Filter Env Amount" label
    filterEnvAmountSlider.setBounds(filterRow3.removeFromLeft(130));
    filterRow3.removeFromLeft(100); // "Filter Key Track" label
    filterKeyTrackSlider.setBounds(filterRow3.removeFromLeft(130));

    bounds.removeFromTop(8);

    auto voiceRow = bounds.removeFromTop(28);
    voiceRow.removeFromLeft(90); // "Voice Count" label
    voiceCountSlider.setBounds(voiceRow.removeFromLeft(100));
    voiceRow.removeFromLeft(100); // "Amp Envelope" label
    ampEnvelopeModeCombo.setBounds(voiceRow.removeFromLeft(150));
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
    // Delegates to ConcreteScreen, which owns the async load + its own loading-animation display
    // now (see that class's loadFile()) - both this Load button and the top-level drop target
    // below funnel through here rather than duplicating that logic at each call site.
    concreteScreen.loadFile(file);
}

juce::AudioProcessorEditor* ConcreteAudioProcessor::createEditor()
{
    return new ConcreteAudioProcessorEditor(*this);
}
