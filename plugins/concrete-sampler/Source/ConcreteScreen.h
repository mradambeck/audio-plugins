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

    // Selecting a different page starts back at that page's first field - matches
    // useEditableFields.tsx's own useEffect on view change.
    void setCurrentPage(ScreenPage page);
    static juce::String firstFieldIdForPage(ScreenPage page);

    // Header + MainContent's bordered box + Footer - identical shell shared by every booted page,
    // Sample included. Returns MainContent's inner content rect for the caller to fill in; footer
    // is fully painted here too (it never overlaps `inner`, so painting it before the caller's
    // page-specific content is fine).
    juce::Rectangle<float> paintPageChrome(juce::Graphics&, juce::Rectangle<float> content);

    void paintBootScreen(juce::Graphics&, juce::Rectangle<float> content);
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

    // No backing APVTS parameter yet - ui-plan.md defers the Volume-section/Volume-parameter work
    // past this slice ("Panel — performance strip... add a Volume section" is later panel-chrome
    // work). Purely local UI state so the page's field grid proportions match the mockup now; NOT
    // wired to real audio yet.
    float localVolumePercent = 100.0f;
};
