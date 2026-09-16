#pragma once

#include "IRLibrary.h"
#include "IRShaper.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

// Owns the "which IR, shaped how" question and answers it off the audio thread.
//
// Decoding, resampling and shaping all allocate and all take milliseconds, so none of them can
// happen in processBlock. The audio thread only ever states what it wants (requestShape, lock-free)
// and collects a finished buffer when one is ready (tryPopShapedIR, try-lock). Handing that buffer
// to juce::dsp::Convolution is left to the audio thread because juce_Convolution.h's threading note
// is explicit that load calls must be synchronised with process calls.
namespace wildjag::conv
{

// One frame of waveform data for the editor. Both envelopes share a single time axis - that of the
// full, unshaped IR - so the display can draw the shaped IR over a dimmed outline of what was
// recorded, and Length and Attack visibly carve into it.
struct WaveformSnapshot
{
    std::vector<float> source;      // the full decoded IR
    std::vector<float> shaped;      // the shaped IR, occupying the first (length%) of the same axis
    double sourceSeconds = 0.0;
    int irIndex = -1;
};

class IRLoadWorker final : private juce::Thread
{
public:
    explicit IRLoadWorker(const ConvolutionVariant& variantToUse);
    ~IRLoadWorker() override;

    // Horizontal resolution of the waveform snapshots. Comfortably above any panel width the editor
    // will ask for, so the display downsamples rather than interpolates.
    static constexpr int envelopePoints = 1024;

    void start();
    void stop();

    // Lock-free; safe from the audio thread. Repeated calls coalesce, and a burst (a knob drag)
    // settles into a single re-shape once it stops - see debounceMs in the .cpp.
    void requestShape(int irIndex, IRShaper::Params params, double sampleRate) noexcept;

    // Audio thread. Moves a finished IR out if one is waiting, leaving `destination` untouched and
    // returning false otherwise. Never blocks: if the worker happens to hold the slot, the IR simply
    // arrives a block later.
    bool tryPopShapedIR(juce::AudioBuffer<float>& destination) noexcept;

    // Message thread. Null until the first shape completes.
    std::shared_ptr<const WaveformSnapshot> getWaveformSnapshot() const;

    // Synchronous equivalent of the whole pipeline, with no thread involved - for the unit tests,
    // the offline render harness, and prepareToPlay's first IR, all of which need the result now
    // rather than in 80 ms. Updates the waveform snapshot but deliberately does NOT queue the result
    // for tryPopShapedIR(): the caller already has the buffer, and leaving a copy in the slot would
    // make the next processBlock load the same IR a second time and crossfade for no reason.
    juce::AudioBuffer<float> shapeSynchronously(int irIndex, IRShaper::Params params, double sampleRate);

    IRLibrary& getLibrary() noexcept { return library; }

private:
    void run() override;
    void publishSnapshot(int irIndex, const juce::AudioBuffer<float>& decoded, const juce::AudioBuffer<float>& shaped);
    void pushPendingIR(juce::AudioBuffer<float>&& shaped);

    IRLibrary library;

    // The pending request. Separate atomics rather than one guarded struct so requestShape() can be
    // called from the audio thread. A torn read across fields is possible and harmless: any write
    // bumps requestCounter, and the worker only records a request as served if the counter held
    // still across the whole shaping pass, so a torn set is superseded on the next iteration.
    std::atomic<int> requestedIndex { 0 };
    std::atomic<float> requestedLengthPercent { 100.0f };
    std::atomic<float> requestedAttackMs { 0.0f };
    std::atomic<double> requestedSampleRate { 0.0 };
    std::atomic<juce::uint32> requestCounter { 0 };
    juce::uint32 servedCounter = 0;

    juce::SpinLock slotLock;
    juce::AudioBuffer<float> pendingIR;
    bool pendingReady = false;

    juce::SpinLock snapshotLock;
    std::shared_ptr<const WaveformSnapshot> snapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRLoadWorker)
};

} // namespace wildjag::conv
