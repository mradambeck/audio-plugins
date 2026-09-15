#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "ConcreteDataKnob.h"
#include "ConcreteDirectionalPad.h"
#include "ConcreteFader.h"
#include "ConcreteKnob.h"
#include "ConcreteLookAndFeel.h"
#include "ConcreteMachineSelector.h"
#include "ConcretePadGrid.h"
#include "ConcretePanelButton.h"
#include "ConcreteScreen.h"
#include "ConcreteSoftKeys.h"
#include "PluginProcessor.h"

#include "../../common/UI/ResizableZoom.h"

// Phase 8's real panel chrome - ~/code/lcd-mockup's Panel.tsx/.module.css translated to JUCE:
// header wordmark, screen well + soft keys + a Pads/Performance/Volume row on the left, Machine
// selector + Edit section + Session buttons on the right, footer. See concrete-sampler-plugin-
// plan.md's Phase 8 and ui-plan.md for the design this replaces the old throwaway Phase 1 layout
// with (that layout's own header comment used to say exactly that - "Phase 8 replaces this
// entirely").
//
// Every parameter that used to have its own Attachment-based control directly on this editor
// (Pitch Engine, Base Rate, Coarse/Fine Tune, Bit Depth, Quantizer Mode, the Capture-pass
// controls, Filter Model, Filter Env Amount/Key Track, Voice Count, Amp Envelope) now lives
// exclusively on the LCD's Machine/Filter/Capture pages, edited via the physical
// ConcreteDirectionalPad + ConcreteDataKnob - see ConcreteScreen::adjustSelected()/
// moveSelectionVertical()/moveSelectionHorizontal() for what field id does what. Only Cutoff/
// Resonance keep a dedicated physical knob (the performance strip) and Machine keeps its own
// dedicated selector - matching Panel.tsx's own comments on both.
//
// All real painting/layout lives here, at a fixed native size (see the setSize() call in the
// constructor) that never changes again - see ConcreteAudioProcessorEditor below for why, and
// common/UI/ResizableZoom.h for the resizable/zoom mechanism this split exists to support
// (matches every other hardware-panel plugin in the catalog - see e.g. strike-synth's own
// StrikeEditorContent/StrikeAudioProcessorEditor split).
class ConcreteEditorContent : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               private juce::Timer
{
public:
    explicit ConcreteEditorContent(ConcreteAudioProcessor&);
    ~ConcreteEditorContent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void loadFile(const juce::File& file);
    bool hasSampleLoaded() const;

    ConcreteAudioProcessor& processor;
    ConcreteLookAndFeel lookAndFeel;

    ConcreteScreen concreteScreen;
    ConcreteSoftKeys softKeys;

    ConcretePadGrid padGrid;
    ConcreteKnob cutoffKnob;
    ConcreteKnob resonanceKnob;
    ConcreteFader sampleVolumeFader;

    // Panel.tsx's own useVolume() masterVolume - purely local UI state. Unlike Sample Volume
    // (now backed by the zone's own real `level` field - see PluginProcessor::setLevelForZone),
    // there's no output-gain stage anywhere in the signal path for Master to drive - see
    // useVolume.tsx's own comment: "Master isn't machine/preset state... it doesn't mark the
    // machine dirty the way Sample Volume does." Wiring a real output-gain DSP stage to this is
    // later work.
    float masterVolumePercent = 100.0f;
    ConcreteFader masterVolumeFader;

    ConcreteMachineSelector machineSelector;
    ConcreteDirectionalPad directionalPad;
    ConcreteDataKnob dataKnob;

    // Session block - see Panel.tsx's own comment on which of these get an LED (One-Shot/Loop,
    // genuine persistent on/off state) versus none at all (Resample/Save Sample/Load-or-Clear,
    // actions or one-time choices, not state).
    ConcretePanelButton oneShotButton;
    ConcretePanelButton loopButton;
    ConcretePanelButton resampleButton;
    ConcretePanelButton saveSampleButton;
    ConcretePanelButton loadClearButton;

    juce::TooltipWindow tooltipWindow { this };
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConcreteEditorContent)
};

// Thin shell around ConcreteEditorContent: owns the plugin window's actual (resizable/zoomable)
// size. wildjag::ResizableZoomHandler (see common/UI/ResizableZoom.h) makes this editor natively
// resizable within a fixed aspect ratio and keeps content scaled via AffineTransform to fill it -
// corner-grip/window-edge drag, with no custom zoom UI drawn by the plugin itself. Always reopens
// at 100% (native size) - the resized size is deliberately not persisted.
class ConcreteAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ConcreteAudioProcessorEditor(ConcreteAudioProcessor&);

private:
    ConcreteEditorContent content;
    wildjag::ResizableZoomHandler zoomHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConcreteAudioProcessorEditor)
};
