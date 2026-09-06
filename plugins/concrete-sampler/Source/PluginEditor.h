#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "ConcreteLookAndFeel.h"
#include "PluginProcessor.h"

// Phase 1's utility standalone UI: load a file (button or drag-and-drop), see the waveform, play
// via MIDI or this temporary on-screen keyboard. Explicitly unstyled - Phase 8 replaces this
// entirely with the real hardware-panel UI (a pad grid, the waveform view with loop markers, the
// embed-override control, etc - see concrete-sampler-plugin-plan.md's Phase 8) once the DSP phases
// have settled what controls exist. There is deliberately no missing-file relocate UI, no embed-
// override toggle, and no per-zone editing here - Architecture #2's backing logic for all of that
// already exists on ConcreteAudioProcessor and is covered by ConcreteProcessorTests; only Phase 8
// gives it a control surface.
class ConcreteWaveformDisplay : public juce::Component
{
public:
    explicit ConcreteWaveformDisplay(ConcreteAudioProcessor& processorIn) : processor(processorIn) {}

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);

        const auto sampleSet = processor.getCurrentSampleSet();
        if (sampleSet->zones.empty() || sampleSet->zones[0].buffer == nullptr)
        {
            g.setColour(juce::Colours::grey);
            g.drawText(sampleSet->zones.empty() || !sampleSet->zones[0].sourceMissing
                           ? "No sample loaded - drop a file here or click Load..."
                           : "Sample missing: " + sampleSet->zones[0].sourcePath,
                       getLocalBounds(), juce::Justification::centred);
            return;
        }

        const auto& buffer = *sampleSet->zones[0].buffer;
        const auto numSamples = buffer.getNumSamples();
        const auto bounds = getLocalBounds().toFloat();
        const auto midY = bounds.getCentreY();
        const auto halfHeight = bounds.getHeight() * 0.5f;
        const auto width = getWidth();

        g.setColour(juce::Colours::lightgreen);
        for (int x = 0; x < width; ++x)
        {
            const auto rangeStart = (int) ((double) x / (double) width * numSamples);
            const auto rangeEnd = juce::jmax(rangeStart + 1,
                                              (int) ((double) (x + 1) / (double) width * numSamples));
            float minVal = 0.0f, maxVal = 0.0f;
            for (int i = rangeStart; i < juce::jmin(rangeEnd, numSamples); ++i)
            {
                const auto sample = buffer.getSample(0, i);
                minVal = juce::jmin(minVal, sample);
                maxVal = juce::jmax(maxVal, sample);
            }
            g.drawVerticalLine(x, midY - maxVal * halfHeight, midY - minVal * halfHeight);
        }
    }

private:
    ConcreteAudioProcessor& processor;
};

class ConcreteAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public juce::FileDragAndDropTarget
{
public:
    explicit ConcreteAudioProcessorEditor(ConcreteAudioProcessor&);
    ~ConcreteAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void loadFile(const juce::File& file);

    ConcreteAudioProcessor& processor;
    ConcreteLookAndFeel lookAndFeel;

    juce::TextButton loadButton { "Load..." };
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Resets root note and the four Phase 2 pitch-engine parameters back to their known-good
    // testing baseline (see README.md's "Standalone app remembers..." section) - the Standalone
    // app persists whatever was last set to disk between launches, not the plugin's own compiled-
    // in defaults, so this is the fast in-app equivalent of deleting that settings file.
    juce::TextButton resetButton { "Reset" };

    ConcreteWaveformDisplay waveformDisplay;

    // Root note is zone-list state (Architecture #1), not an APVTS parameter, so this is a plain
    // Slider with no Attachment - setRootNoteForZone() is called directly on change. Displayed as
    // a note name (e.g. "C3") rather than a raw MIDI number, per octaveNumForMiddleC=3 (note 60 =
    // C3, the Yamaha/Roland convention) - see juce::MidiMessage::getMidiNoteName().
    juce::Label rootNoteLabel { {}, "Root Note" };
    juce::Slider rootNoteSlider;

    // Phase 2's pitch-engine controls - real APVTS parameters, so these use the standard
    // Attachment classes rather than manual get/set wiring. STANDALONE CHECK 2 needs exactly this:
    // a mode selector, exposed, so the three machine modes can be A/B'd by ear.
    juce::Label pitchEngineLabel { {}, "Pitch Engine" };
    juce::ComboBox pitchEngineCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pitchEngineAttachment;

    juce::Label baseRateLabel { {}, "Base Rate" };
    juce::Slider baseRateSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> baseRateAttachment;

    juce::Label coarseTuneLabel { {}, "Coarse Tune" };
    juce::Slider coarseTuneSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> coarseTuneAttachment;

    juce::Label fineTuneLabel { {}, "Fine Tune" };
    juce::Slider fineTuneSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fineTuneAttachment;

    // Phase 3's quantization controls (see ConcreteQuantizer.h) - same Attachment convention as
    // the Phase 2 controls above.
    juce::Label bitDepthLabel { {}, "Bit Depth" };
    juce::Slider bitDepthSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bitDepthAttachment;

    juce::Label quantizerModeLabel { {}, "Quantizer Mode" };
    juce::ComboBox quantizerModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> quantizerModeAttachment;

    juce::MidiKeyboardComponent keyboardComponent;
};
