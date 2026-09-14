#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "ConcreteKnob.h"
#include "ConcreteLookAndFeel.h"
#include "ConcretePadGrid.h"
#include "ConcreteScreen.h"
#include "PluginProcessor.h"

// Phase 1's utility standalone UI: load a file (button or drag-and-drop), see the waveform, play
// via the pad grid. Everything below is still Phase 1's plain Attachment-based controls EXCEPT the
// screen and the pad grid, which are Phase 8's real ConcreteScreen/ConcretePadGrid (see those
// classes) - hosted here temporarily until the rest of the panel (performance strip, session
// buttons, machine selector, full chassis-less chrome) replaces this whole editor. See
// concrete-sampler-plugin-plan.md's Phase 8 and plugins/concrete-sampler/ui-plan.md.
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

    ConcreteScreen concreteScreen;

    // Phase 7's Machine selector (see ConcreteMachines.h) - "a single Machine selector as the
    // primary control," per the plan, hence its own row right under the load/reset controls,
    // above every other (secondary) control below. A real APVTS parameter, so this uses the
    // standard Attachment convention like Pitch Engine/Filter Model/etc. Root Note/One-Shot (zone-
    // list state, not parameters) no longer have separate controls here - ConcreteScreen's Sample
    // page owns them now, calling setRootNoteForZone()/setOneShotForZone() directly, the same
    // manual-wiring pattern this comment used to describe for the controls that used to be here.
    juce::Label machineLabel { {}, "Machine" };
    juce::ComboBox machineCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> machineAttachment;

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

    // Phase 4's capture pass (see ConcreteCapturePass.h) - same Attachment convention as the
    // Phase 2/3 controls above. Pitch Compensate/Bypass are AudioParameterBool, so these use
    // ToggleButton + ButtonAttachment rather than a Slider/ComboBox.
    juce::Label captureTransposeLabel { {}, "Capture Transpose" };
    juce::Slider captureTransposeSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> captureTransposeAttachment;

    juce::Label captureDriveLabel { {}, "Capture Drive" };
    juce::Slider captureDriveSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> captureDriveAttachment;

    juce::ToggleButton captureAutoCompensateButton { "Pitch Compensate" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> captureAutoCompensateAttachment;

    juce::ToggleButton captureBypassButton { "Capture Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> captureBypassAttachment;

    juce::Label captureIterationsLabel { {}, "Capture Iterations" };
    juce::Slider captureIterationsSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> captureIterationsAttachment;

    // Phase 5's playback-side filter (see ConcreteFilterModels.h) - same Attachment convention as
    // every control above. STANDALONE CHECK 5 needs exactly this: a model selector plus cutoff/
    // resonance exposed, so the filter models can be A/B'd by ear.
    juce::Label filterModelLabel { {}, "Filter Model" };
    juce::ComboBox filterModelCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterModelAttachment;

    // Cutoff/Resonance are Phase 8's real performance-strip knobs now (see ConcreteKnob and
    // Panel.tsx's own comment: "Performance is just Cutoff/Resonance") rather than throwaway
    // Attachment-based sliders - the one place in this still-mostly-Phase-1 editor a real panel
    // control has landed alongside the screen and pad grid.
    ConcreteKnob cutoffKnob;
    ConcreteKnob resonanceKnob;

    juce::Label filterEnvAmountLabel { {}, "Filter Env Amount" };
    juce::Slider filterEnvAmountSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterEnvAmountAttachment;

    juce::Label filterKeyTrackLabel { {}, "Filter Key Track" };
    juce::Slider filterKeyTrackSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterKeyTrackAttachment;

    // Phase 6's voice architecture (see ConcreteVoiceAllocator.h/ConcreteContourEnvelope.h) - same
    // Attachment convention as every control above.
    juce::Label voiceCountLabel { {}, "Voice Count" };
    juce::Slider voiceCountSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> voiceCountAttachment;

    juce::Label ampEnvelopeModeLabel { {}, "Amp Envelope" };
    juce::ComboBox ampEnvelopeModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampEnvelopeModeAttachment;

    ConcretePadGrid padGrid;
};
