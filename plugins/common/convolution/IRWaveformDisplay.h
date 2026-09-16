#pragma once

#include "IRLoadWorker.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Draws the impulse response the engine is actually using: the shaped IR in full, over a dimmed
// outline of the whole recording, so Length and Attack visibly carve into something rather than
// just moving a number. A pre-delay marker shows where the reverb onset sits relative to the dry
// signal.
//
// Reads only WaveformSnapshot values published by IRLoadWorker on its own thread. It never touches
// the live IR buffer, the convolution, or the audio thread.
namespace wildjag::conv
{

class IRWaveformDisplay : public juce::Component
{
public:
    enum ColourIds
    {
        backgroundColourId = 0x1a10001,
        sourceWaveformColourId = 0x1a10002,   // the full recording, dimmed
        shapedWaveformColourId = 0x1a10003,   // the portion actually convolved
        preDelayMarkerColourId = 0x1a10004,
        gridColourId = 0x1a10005
    };

    IRWaveformDisplay();

    // Called from the editor's timer. Cheap when nothing changed: a snapshot is only repainted if
    // the shared_ptr identity or the pre-delay actually moved.
    void update(std::shared_ptr<const WaveformSnapshot> newSnapshot, float preDelayMs);

    void paint(juce::Graphics& g) override;

private:
    std::shared_ptr<const WaveformSnapshot> snapshot;
    float preDelayMs = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRWaveformDisplay)
};

} // namespace wildjag::conv
