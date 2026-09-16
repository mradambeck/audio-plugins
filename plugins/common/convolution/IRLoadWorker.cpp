#include "IRLoadWorker.h"

#include <algorithm>
#include <cmath>

namespace wildjag::conv
{

namespace
{
    // Quiet period a request must survive before the worker acts on it. A Length or Attack drag
    // emits a new request on every audio block; without this the worker would decode and re-shape
    // dozens of times across one gesture and land the final one late. Long enough to swallow a
    // drag, short enough that a deliberate single change still feels immediate.
    constexpr int debounceMs = 80;
}

IRLoadWorker::IRLoadWorker(const ConvolutionVariant& variantToUse)
    : juce::Thread("Convolution IR loader"), library(variantToUse)
{
}

IRLoadWorker::~IRLoadWorker()
{
    stop();
}

void IRLoadWorker::start()
{
    if (! isThreadRunning())
        startThread(juce::Thread::Priority::low);
}

void IRLoadWorker::stop()
{
    stopThread(2000);
}

void IRLoadWorker::requestShape(int irIndex, IRShaper::Params params, double sampleRate) noexcept
{
    requestedIndex.store(irIndex, std::memory_order_relaxed);
    requestedLengthPercent.store(params.lengthPercent, std::memory_order_relaxed);
    requestedAttackMs.store(params.attackMs, std::memory_order_relaxed);
    requestedSampleRate.store(sampleRate, std::memory_order_relaxed);

    // Release so the worker's acquire load of the counter sees all four fields above.
    requestCounter.fetch_add(1, std::memory_order_release);

    notify();
}

bool IRLoadWorker::tryPopShapedIR(juce::AudioBuffer<float>& destination) noexcept
{
    const juce::SpinLock::ScopedTryLockType lock(slotLock);

    if (! lock.isLocked() || ! pendingReady)
        return false;

    destination = std::move(pendingIR);
    pendingIR = juce::AudioBuffer<float>();
    pendingReady = false;
    return true;
}

std::shared_ptr<const WaveformSnapshot> IRLoadWorker::getWaveformSnapshot() const
{
    const juce::SpinLock::ScopedLockType lock(snapshotLock);
    return snapshot;
}

juce::AudioBuffer<float> IRLoadWorker::shapeSynchronously(int irIndex, IRShaper::Params params, double sampleRate)
{
    library.setTargetSampleRate(sampleRate);

    auto decoded = library.getDecodedIR(irIndex);
    if (decoded == nullptr)
        return {};

    auto shaped = IRShaper::shape(*decoded, sampleRate, params);
    publishSnapshot(irIndex, *decoded, shaped);
    return shaped;
}

void IRLoadWorker::publishSnapshot(int irIndex, const juce::AudioBuffer<float>& decoded,
                                   const juce::AudioBuffer<float>& shaped)
{
    auto frame = std::make_shared<WaveformSnapshot>();
    frame->irIndex = irIndex;
    frame->sourceSeconds = library.getTargetSampleRate() > 0.0
                             ? (double) decoded.getNumSamples() / library.getTargetSampleRate()
                             : 0.0;
    frame->source = IRShaper::computePeakEnvelope(decoded, envelopePoints);

    // The shaped envelope is drawn on the source's time axis, so it gets proportionally fewer
    // points rather than being stretched back out to full width - that is what makes a Length cut
    // read as "the tail is gone" instead of "the whole IR got shorter".
    const auto shapedPoints = decoded.getNumSamples() > 0
                                ? (int) std::lround((double) envelopePoints * (double) shaped.getNumSamples()
                                                    / (double) decoded.getNumSamples())
                                : 0;
    frame->shaped = IRShaper::computePeakEnvelope(shaped, std::max(shapedPoints, 1));

    const juce::SpinLock::ScopedLockType lock(snapshotLock);
    snapshot = std::move(frame);
}

void IRLoadWorker::pushPendingIR(juce::AudioBuffer<float>&& shaped)
{
    const juce::SpinLock::ScopedLockType lock(slotLock);
    pendingIR = std::move(shaped);
    pendingReady = true;
}

void IRLoadWorker::run()
{
    while (! threadShouldExit())
    {
        if (requestCounter.load(std::memory_order_acquire) == servedCounter)
        {
            wait(-1);
            continue;
        }

        // Ride out the rest of the gesture before doing any work.
        const auto counterBeforeWait = requestCounter.load(std::memory_order_acquire);
        wait(debounceMs);

        if (threadShouldExit())
            return;

        if (requestCounter.load(std::memory_order_acquire) != counterBeforeWait)
            continue; // still moving - start the quiet period again

        const auto serving = requestCounter.load(std::memory_order_acquire);
        const auto index = requestedIndex.load(std::memory_order_relaxed);
        const IRShaper::Params params { requestedLengthPercent.load(std::memory_order_relaxed),
                                        requestedAttackMs.load(std::memory_order_relaxed) };
        const auto sampleRate = requestedSampleRate.load(std::memory_order_relaxed);

        if (sampleRate <= 0.0)
        {
            servedCounter = serving;
            continue;
        }

        library.setTargetSampleRate(sampleRate);

        auto decoded = library.getDecodedIR(index);

        // An undecodable blob leaves the current IR alone rather than loading silence - a variant
        // shipped with one bad file should lose that one entry, not the whole reverb.
        if (decoded == nullptr)
        {
            servedCounter = serving;
            continue;
        }

        auto shaped = IRShaper::shape(*decoded, sampleRate, params);

        if (shaped.getNumSamples() > 0)
        {
            publishSnapshot(index, *decoded, shaped);
            pushPendingIR(std::move(shaped));
        }

        servedCounter = serving;
    }
}

} // namespace wildjag::conv
