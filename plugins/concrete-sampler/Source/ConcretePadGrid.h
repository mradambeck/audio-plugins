#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConcreteLookAndFeel.h"
#include "PluginProcessor.h"

// The 4x4 trigger surface - translated from ~/code/lcd-mockup's PadGrid.tsx/.module.css. Fixed
// footprint on purpose (see ui-plan.md's "Trigger surface") so a two-octave keyboard could swap in
// later without reflowing the panel; both surfaces are stateless, emitting only (note, velocity)
// into processor.keyboardState, exactly like the mockup's own doc comment describes. This directly
// replaces the throwaway editor's juce::MidiKeyboardComponent - Phase 8's real trigger surface per
// concrete-sampler-plugin-plan.md.
//
// Per-pad sample assignment (drag-and-drop, right-click Load/Clear/Copy/Paste, usePadZones.tsx in
// the mockup) is NOT built here - the real plugin's zone list is still single-zone in v1
// (ConcreteSampleZone.h's own comment: "a v1 instance always has exactly one zone"), and
// ui-plan.md explicitly defers multi-zone editing past Phase 8. Every pad plays the same one zone,
// transposed - the mockup's own fallback behavior for a pad with no zone of its own.
class ConcretePadGrid : public juce::Component
{
public:
    ConcretePadGrid(ConcreteAudioProcessor& processorIn, ConcreteLookAndFeel& lookAndFeelIn);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    static constexpr int padCount = 16;
    static constexpr int rowsCount = 4;
    static constexpr int colsCount = 4;
    static constexpr int midiChannel = 1;

    // Bottom-left pad is the root; notes run chromatically left-to-right then bottom-to-top
    // (the standard MPC-style layout) - matches PadGrid.tsx's own noteMidi formula exactly.
    int noteForPad(int index) const noexcept;
    int currentRootNote() const noexcept;
    int padIndexAtPosition(juce::Point<float> position) const noexcept;
    juce::Rectangle<float> padBounds(int index) const noexcept;

    void releaseLitPad();

    ConcreteAudioProcessor& processor;
    ConcreteLookAndFeel& lookAndFeel;

    int litPadIndex = -1;
};
