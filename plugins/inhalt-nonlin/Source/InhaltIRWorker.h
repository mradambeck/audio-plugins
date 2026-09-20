#pragma once

#include "InhaltIRSynth.h"

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

// Owns "what IR should currently be loaded" for Inhalt, answered off the audio thread - the
// synthesis counterpart to plugins/common/convolution/IRLoadWorker.h, whose job is "which
// BUNDLED ir, shaped how" (decode + IRShaper), not "synthesize one from DSP parameters". Mirrors
// that class's THREADING CONTRACT exactly (lock-free request, try-lock pop, a debounce so a knob
// drag doesn't re-synthesize on every audio block) rather than reusing the class itself, since the
// actual work (InhaltIRSynth::render(), which allocates and runs numSamples * numLines work) is
// unrelated to decoding/shaping a file.
namespace inhalt
{

class InhaltIRWorker final : private juce::Thread
{
public:
    InhaltIRWorker();
    ~InhaltIRWorker() override;

    void start();
    void stop();

    // Lock-free; safe from the audio thread. Repeated calls coalesce, and a burst (a knob drag)
    // settles into a single re-synthesis once it stops - see debounceMs in the .cpp.
    void requestSynthesis(float timeKnob, float highKnob, double sampleRate) noexcept;

    // Audio thread. Moves a finished IR out if one is waiting, leaving `destination` untouched and
    // returning false otherwise. Never blocks.
    bool tryPopSynthesizedIR(juce::AudioBuffer<float>& destination) noexcept;

    // Synchronous equivalent of the whole pipeline (map params -> synth -> normalize), with no
    // thread involved - for unit tests, the offline render harness, and prepareToPlay's first IR,
    // all of which need the result now rather than after the debounce window. Deliberately does
    // NOT queue the result for tryPopSynthesizedIR() (same reasoning as IRLoadWorker's own
    // shapeSynchronously()): the caller already has the buffer, and leaving a copy in the slot
    // would make the next processBlock load the same IR again and crossfade for no reason.
    static juce::AudioBuffer<float> synthesizeSynchronously(float timeKnob, float highKnob, double sampleRate,
                                                              int numSamples);

private:
    void run() override;
    void pushPendingIR(juce::AudioBuffer<float>&& synthesized);

    std::atomic<float> requestedTimeKnob { 2.2f };
    std::atomic<float> requestedHighKnob { 0.0f };
    std::atomic<double> requestedSampleRate { 0.0 };
    std::atomic<int> requestCounter { 0 };
    int servedCounter = 0;

    juce::SpinLock slotLock;
    juce::AudioBuffer<float> pendingIR;
    bool pendingReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InhaltIRWorker)
};

} // namespace inhalt
