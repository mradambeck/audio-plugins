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

void IRLoadWorker::setSessionSampleRate(double sampleRate) noexcept
{
    sessionSampleRate.store(sampleRate, std::memory_order_relaxed);

    // Drop anything already sitting in the pending slot - it was shaped for whatever rate was
    // current before this call. Left in place, the very next processBlock() would pop it and hand
    // it to loadIR(), which loads it as-is at the NEW rate with no resampling of its own.
    const juce::SpinLock::ScopedLockType lock(slotLock);
    pendingIR = juce::AudioBuffer<float>();
    pendingReady = false;
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

    auto shaped = IRShaper::shape(decoded->samples, sampleRate, params);
    publishSnapshot(irIndex, *decoded, shaped);
    return shaped;
}

void IRLoadWorker::publishSnapshot(int irIndex, const DecodedIR& decoded,
                                   const juce::AudioBuffer<float>& shaped)
{
    auto frame = std::make_shared<WaveformSnapshot>();
    frame->irIndex = irIndex;
    frame->nativeSampleRate = decoded.nativeSampleRate;
    frame->sourceChannels = decoded.samples.getNumChannels();
    frame->sourceSeconds = library.getTargetSampleRate() > 0.0
                             ? (double) decoded.samples.getNumSamples() / library.getTargetSampleRate()
                             : 0.0;
    frame->source = IRShaper::computePeakEnvelope(decoded.samples, envelopePoints);

    // The shaped envelope is drawn on the source's time axis, so it gets proportionally fewer
    // points rather than being stretched back out to full width - that is what makes a Length cut
    // read as "the tail is gone" instead of "the whole IR got shorter".
    const auto shapedPoints = decoded.samples.getNumSamples() > 0
                                ? (int) std::lround((double) envelopePoints * (double) shaped.getNumSamples()
                                                    / (double) decoded.samples.getNumSamples())
                                : 0;
    frame->shaped = IRShaper::computePeakEnvelope(shaped, std::max(shapedPoints, 1));

    // Scale both envelopes so the SOURCE's loudest point reaches full height. The display is there
    // to show an IR's shape - where the onset is, how it decays, what Length and Attack removed -
    // not its absolute level, and after IRLibrary's unit-energy normalisation the absolute peaks
    // are tiny (a second-long IR normalises to peaks well under 0.05), which drew as an almost
    // flat line. Deliberately one shared scale taken from the source, not a per-envelope maximum:
    // scaling the shaped envelope independently would hide exactly the amplitude change that
    // Attack is making.
    const auto peak = frame->source.empty()
                        ? 0.0f
                        : *std::max_element(frame->source.begin(), frame->source.end());

    if (peak > 0.0f)
    {
        const auto scale = 1.0f / peak;
        for (auto& value : frame->source) value *= scale;
        for (auto& value : frame->shaped) value *= scale;
    }

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

        auto shaped = IRShaper::shape(decoded->samples, sampleRate, params);

        if (shaped.getNumSamples() > 0)
        {
            // The waveform display legitimately reflects the last IR asked for, even one that
            // arrives too late to actually load, so this runs regardless of the rate check below.
            publishSnapshot(index, *decoded, shaped);

            // Discard rather than deliver if the session's sample rate moved on while this shape
            // was in flight (see setSessionSampleRate()'s comment) - a host re-prepare landing mid-
            // shape is a real sequence (prepareToPlay() calls setSessionSampleRate() itself, but a
            // request already being worked on here has no way to know that happened until now).
            // Written as a difference rather than == /!= to sidestep -Wfloat-equal (juce_recommended_
            // warning_flags enables it) - same idiom ShieldsFDNEngine.cpp uses for the same reason;
            // an exact, non-tolerance comparison is intended here, not a fuzzy one.
            if (std::abs(sampleRate - sessionSampleRate.load(std::memory_order_relaxed)) <= 0.0)
                pushPendingIR(std::move(shaped));
        }

        servedCounter = serving;
    }
}

} // namespace wildjag::conv
