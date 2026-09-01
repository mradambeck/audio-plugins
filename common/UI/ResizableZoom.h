#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace wildjag
{
    // Hooks a hardware-panel plugin editor up to native resize/zoom (corner-grip / window-edge drag -
    // Logic's own "View: 100%" percentage menu is exclusive to its internal plugins, not reachable by
    // any third-party AU/AUv3) instead of drawing a custom zoom control: makes editorToResize resizable
    // within [minZoom, maxZoom] of nativeSize, locked to nativeSize's aspect ratio so it can only ever
    // be scaled proportionally, and keeps zoomableContent - a Component sized once at nativeSize in its
    // own constructor and never resized again, per the juce-hardware-panel-ui pattern's EditorContent
    // split - scaled via AffineTransform to exactly fill whatever size the corner grip produces.
    //
    // zoomableContent must already be added as editorToResize's child (addAndMakeVisible) and sized to
    // nativeSize before constructing this. initialZoomFactor is the starting size (1.0f = native) -
    // deliberately not persisted across editor opens, so the plugin always reopens at its native size
    // rather than remembering a previous resize.
    class ResizableZoomHandler : private juce::ComponentListener
    {
    public:
        ResizableZoomHandler(juce::AudioProcessorEditor& editorToResize, juce::Component& zoomableContent,
                              juce::Point<int> nativeSizeIn, float initialZoomFactor = 1.0f,
                              float minZoomIn = 0.5f, float maxZoomIn = 2.0f)
            : editor(editorToResize), content(zoomableContent), nativeSize(nativeSizeIn),
              minZoom(minZoomIn), maxZoom(maxZoomIn)
        {
            applyConstrainerLimits();
            editor.setConstrainer(&constrainer);
            editor.setResizable(true, true);

            editor.addComponentListener(this);

            currentZoomFactor = juce::jlimit(minZoom, maxZoom, initialZoomFactor);
            editor.setSize(juce::roundToInt((float) nativeSize.x * currentZoomFactor),
                            juce::roundToInt((float) nativeSize.y * currentZoomFactor));
        }

        ~ResizableZoomHandler() override
        {
            editor.removeComponentListener(this);
            editor.setConstrainer(nullptr);
        }

        // For editors whose content's native size itself changes at runtime (e.g. a paged layout
        // that's a different height per page - see alloy-bass's setShowingPageOne()): call this right
        // after content.setSize(newNativeSize) so the constrainer's limits/aspect ratio and the
        // editor's actual window size get updated to match. Preserves the current zoom factor rather
        // than resetting to 100%, so resizing/zooming isn't undone by switching pages.
        void setNativeSize(juce::Point<int> newNativeSize)
        {
            nativeSize = newNativeSize;
            applyConstrainerLimits();
            editor.setSize(juce::roundToInt((float) nativeSize.x * currentZoomFactor),
                            juce::roundToInt((float) nativeSize.y * currentZoomFactor));
        }

    private:
        void applyConstrainerLimits()
        {
            constrainer.setSizeLimits(juce::roundToInt((float) nativeSize.x * minZoom),
                                       juce::roundToInt((float) nativeSize.y * minZoom),
                                       juce::roundToInt((float) nativeSize.x * maxZoom),
                                       juce::roundToInt((float) nativeSize.y * maxZoom));
            constrainer.setFixedAspectRatio((double) nativeSize.x / (double) nativeSize.y);
        }

        void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
        {
            if (! wasResized || nativeSize.x <= 0 || nativeSize.y <= 0)
                return;

            currentZoomFactor = (float) editor.getWidth() / (float) nativeSize.x;
            content.setTransform(juce::AffineTransform::scale((float) editor.getWidth() / (float) nativeSize.x,
                                                                (float) editor.getHeight() / (float) nativeSize.y));
        }

        juce::AudioProcessorEditor& editor;
        juce::Component& content;
        juce::Point<int> nativeSize;
        float minZoom, maxZoom;
        float currentZoomFactor = 1.0f;
        juce::ComponentBoundsConstrainer constrainer;
    };
}
