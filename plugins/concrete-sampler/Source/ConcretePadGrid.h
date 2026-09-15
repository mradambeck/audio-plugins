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
// Per-pad sample assignment (drag-and-drop, right-click Load/Clear/Copy/Paste - see
// PadContextMenu.tsx/usePadZones.tsx in the mockup) IS built here: a pad with no zone of its own
// still plays the one main sample, transposed (v1's original, still-default behavior); dropping a
// file onto a pad (or using its right-click menu's Load...) "promotes" it to an independent
// one-shot zone via PluginProcessor::assignSampleToPad() - see that method's own comment for how a
// pad's zone is represented in the real (not mockup-fake) zone list.
class ConcretePadGrid : public juce::Component,
                         public juce::FileDragAndDropTarget
{
public:
    ConcretePadGrid(ConcreteAudioProcessor& processorIn, ConcreteLookAndFeel& lookAndFeelIn);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

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
    void showContextMenu(int index);
    void loadFileOntoPad(int index);

    // Looks up (via the real ConcreteSampleSet::lookup(), the same resolution a note-on actually
    // uses) whether this pad's note currently resolves to a zone of its own (keyLo==keyHi==that
    // note) rather than the inherited main zone - and if so, that zone's own file name. Returns an
    // empty string when the pad has no zone of its own.
    juce::String ownZoneFileNameForPad(int index) const;

    ConcreteAudioProcessor& processor;
    ConcreteLookAndFeel& lookAndFeel;

    int litPadIndex = -1;
    int dragOverPadIndex = -1;

    // Copy/Paste's "clipboard" is just the last-copied pad's own source file path - pasting
    // re-loads from that same path onto the target pad rather than duplicating any in-memory
    // audio data, matching how Load... already works and avoiding a second in-memory-clone code
    // path that would only ever be exercised by Paste.
    juce::File clipboardFile;

    std::unique_ptr<juce::FileChooser> fileChooser;
};
