#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ConcreteLookAndFeel.h"
#include "PluginProcessor.h"

// Concrete's LCD screen - Phase 8: the boot sequence and all four screen pages, translated
// pixel-for-pixel from the approved React mockup (~/code/lcd-mockup's LcdScreen.tsx/
// BootScreen.tsx/WaveSurfer.tsx/Machine.tsx/Filter.tsx/Capture.tsx - see
// plugins/concrete-sampler/ui-plan.md). The pad grid, performance strip, and the rest of the panel
// chrome (chassis, session buttons, machine selector) are later slices - those live outside the
// screen itself.
//
// Fixed at the mockup's own 500x250 - see ui-plan.md's "Screen resolution" open item, resolved to
// keep the prototype's resolution rather than an integer-scaled 240x64. One juce::Timer drives
// every animation from a single elapsed-time clock, replicating the mockup's several independent
// setInterval/Framer-Motion timelines off the same constants (500ms start delay, 55ms step
// interval, the exact flicker keyframes, the exact per-dust-glyph pseudo-random blink desync) -
// see the .cpp for each constant's mockup source.
class ConcreteScreen : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    ConcreteScreen(ConcreteAudioProcessor& processorIn, ConcreteLookAndFeel& lookAndFeelIn);
    ~ConcreteScreen() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Shared by this component's own drag-and-drop handling and PluginEditor's Load button/
    // top-level drop target - see this method's own .cpp comment for why the async-load-plus-
    // loading-animation behavior lives here rather than being duplicated at each call site.
    void loadFile(const juce::File& file);

    // The Session block's "Clear Sample" button (mockup: useSample.tsx's requestClear(), minus
    // the confirmation overlay it arms first - ClearConfirmOverlay.tsx is a real LCD-page addition
    // deferred to a later slice, not built here). Clears only whichever zone this screen is
    // currently showing (see showZoneForNote()) - a pad's own sample, or the main sample - never
    // the whole zone list at once.
    void clearSample();

    // Physical DataKnob's entry point (see ConcreteDataKnob) - identical to the mouse-wheel stand-
    // in adjustSelectedField() already used for headless/no-hardware testing.
    void adjustSelected(int delta) { adjustSelectedField(delta); }

    // Physical DirectionalPad's entry points - navigate the current page's field grid exactly like
    // useEditableFields.tsx's moveVertical/moveHorizontal (Up/Down keep column, wrap by row;
    // Left/Right keep row, wrap by column).
    void moveSelectionVertical(int delta);
    void moveSelectionHorizontal(int delta);

    // Physical SoftKeys' entry points (see ConcreteSoftKeys) - exposed as a plain index rather
    // than ScreenPage so that component doesn't need this enum. onPageChanged fires whenever the
    // page changes from EITHER side (a SoftKey press or a footer-tab click on the screen itself),
    // so SoftKeys' own LED can't drift out of sync with whichever caused the change.
    int getPageIndex() const noexcept { return (int) currentPage; }
    void setPageIndex(int index) { setCurrentPage((ScreenPage) index); }
    std::function<void()> onPageChanged;

    // ConcretePadGrid's per-pad-trigger entry point (see its own onPadTriggered) - if `note`
    // resolves (via the real ConcreteSampleSet::lookup(), same as an actual note-on) to a pad's own
    // independent zone, the Sample page switches to showing THAT zone's info (filename, waveform,
    // Root/One-Shot/Loop/Volume) instead of the main sample, so hitting a pad with its own sample
    // immediately shows what you'd be adjusting - see Panel.tsx-adjacent design intent: the screen
    // is how every sample's settings get edited, and that has to include a pad's own sample once
    // one exists. Hitting a "plain" pad (no zone of its own) switches back to showing the main
    // sample. See getDisplayedZoneIndex() for how every other bit of Sample-page UI (and the
    // physical One-Shot/Loop buttons + Sample Volume fader in PluginEditor) stays in sync with
    // whichever zone this last selected.
    void showZoneForNote(int note);

    // Which zone index the Sample page (and the physical controls that mirror its own fields -
    // One-Shot/Loop/Sample Volume) should currently read/write: either the main zone (0) or, after
    // showZoneForNote() was last called for a pad with its own zone, that pad's zone - resolved
    // FRESH every call (not cached) so it automatically falls back to the main zone if that pad's
    // zone gets cleared out from under it. Returns 0 if there's no sample set at all yet.
    int getDisplayedZoneIndex() const;

private:
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    double elapsedBootMs() const noexcept;
    float lcdFlickerAlpha(double elapsedMs) const noexcept;

    // The mockup's SelectableField distinguishes tap-to-select from drag-to-adjust (see
    // SelectableField.tsx), driven by a physical DataKnob/DirectionalPad this slice doesn't build
    // yet (deferred - see ui-plan.md). Without either, this editor would have NO way left to
    // change any field at all (the old throwaway sliders this class replaces are gone) - an
    // interim regression in an otherwise-untouched testing tool. Mouse-wheel-adjusts-the-selected-
    // field stands in for the real knob for now: tap still only selects, matching the mockup, and
    // wheel plays the DataKnob's role until a later slice adds the real one - at which point this
    // can be deleted outright, not adapted.
    void adjustSelectedField(int delta);

    enum class ScreenPage { Sample, Machine, Filter, Capture };
    ScreenPage currentPage = ScreenPage::Sample;

    // Backs getDisplayedZoneIndex()/showZoneForNote() - see those methods' own comments. Tracked
    // by NOTE (not a raw zone index) specifically so it stays meaningful even if the zone list is
    // mutated later (a pad's zone being cleared, zones being reordered) - a raw index would go
    // stale in exactly those cases, but re-resolving a note through lookup() every time can't.
    bool showingPadZone = false;
    int displayedPadNote = 0;
    int resolveDisplayedZoneIndex(const ConcreteSampleSet::Ptr& sampleSet) const noexcept;

    // Selecting a different page starts back at that page's first field - matches
    // useEditableFields.tsx's own useEffect on view change.
    void setCurrentPage(ScreenPage page);
    static juce::String firstFieldIdForPage(ScreenPage page);

    // Backs moveSelectionVertical/Horizontal - matches useEditableFields.tsx's own `grid` per view
    // exactly (same row/column shape the DirectionalPad navigates on the mockup side).
    static std::vector<std::vector<juce::String>> fieldGridForPage(ScreenPage page);

    // Header + MainContent's bordered box + Footer - identical shell shared by every booted page,
    // Sample included. Returns MainContent's inner content rect for the caller to fill in; footer
    // is fully painted here too (it never overlaps `inner`, so painting it before the caller's
    // page-specific content is fine).
    juce::Rectangle<float> paintPageChrome(juce::Graphics&, juce::Rectangle<float> content);

    void paintBootScreen(juce::Graphics&, juce::Rectangle<float> content);
    // The missing-file state's "Click to relocate" (see paintSamplePage()'s own comment for why
    // this UI exists) - opens a file chooser and forwards the result to
    // ConcreteAudioProcessor::relocateZone().
    void relocateSample();

    void paintSamplePage(juce::Graphics&, juce::Rectangle<float> content);
    void paintMachinePage(juce::Graphics&, juce::Rectangle<float> content);
    void paintFilterPage(juce::Graphics&, juce::Rectangle<float> content);
    void paintCapturePage(juce::Graphics&, juce::Rectangle<float> content);
    void paintHeader(juce::Graphics&, juce::Rectangle<float> area);
    void paintFooter(juce::Graphics&, juce::Rectangle<float> area);
    void paintLoadingOverlay(juce::Graphics&, juce::Rectangle<float> area, const juce::String& message);

    // Machine.tsx/Filter.tsx/Capture.tsx are all "StatusLine + a fixed 2-column field grid [+ one
    // extra row]" - StatusLine and the grid are genuinely shared, factored out once rather than
    // repeated three times. Sample's own field rows stay bespoke in paintSamplePage (it mixes
    // fields with the waveform/plain text/a button, same reason WaveSurfer.tsx builds its own grid
    // markup instead of consuming `grid` directly - see that page's own comment).
    void paintStatusLine(juce::Graphics&, juce::Rectangle<float> area);
    struct GridField { juce::String id, label, value; };
    // Each inner vector is one row, 1 or 2 fields (Filter's Resonance row has just one - grid
    // auto-placement leaves the second column blank, same as the mockup). Consumes exactly
    // rows.size() row-heights from the top of `area` (the removeFromTop() idiom used everywhere
    // else in this file), so callers can add their own row below afterward (Machine's Amp
    // Envelope, Capture's bake row) by just continuing to consume the same `area`.
    void paintFieldGrid(juce::Graphics&, juce::Rectangle<float>& area, const std::vector<std::vector<GridField>>& rows);

    // Extracted from what used to be a local lambda in paintSamplePage - Machine/Filter/Capture's
    // grid needs the exact same tap-to-select/inverse-video rendering.
    void drawField(juce::Graphics&, juce::Rectangle<float> cell, const juce::String& id,
                    const juce::String& label, const juce::String& value);

    struct FieldHitArea
    {
        juce::String id;
        juce::Rectangle<float> bounds;
        std::function<void()> onTap; // toggles/cycles that don't need drag-to-adjust yet - see
                                      // SelectableField's own comment in this .cpp for what's
                                      // deferred (press-and-drag adjust, per-pixel like the mockup)
    };

    ConcreteAudioProcessor& processor;
    ConcreteLookAndFeel& lookAndFeel;

    juce::int64 constructedAtMs = 0;
    bool isBooted = false;

    juce::String selectedFieldId = "root";
    std::vector<FieldHitArea> fieldHitAreas; // rebuilt every page paint, for mouseDown

    // Populated by paintFooter() every paint, checked by mouseDown() - only 4, fixed, so a plain
    // array beats reusing FieldHitArea's std::function overhead for something this simple.
    std::array<juce::Rectangle<float>, 4> footerTabBounds;

    bool isDraggingFileOver = false;
    bool isLoading = false;

    std::unique_ptr<juce::FileChooser> fileChooser; // relocateSample()'s own, separate from
                                                      // PluginEditor's Load-button one
};
