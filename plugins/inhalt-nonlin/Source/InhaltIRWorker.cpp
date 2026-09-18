#include "InhaltIRWorker.h"
#include "InhaltParameterMap.h"

#include <cmath>

namespace inhalt
{

namespace
{
    // Same debounce value and reasoning as IRLoadWorker.cpp: long enough to swallow a knob drag
    // (which emits a new request on every audio block), short enough a deliberate single change
    // still feels immediate.
    constexpr int debounceMs = 80;

    // Copy of wildjag::conv::IRLibrary::normaliseToUnitEnergy() - not linked directly, since that
    // class pulls in ConvolutionVariant.h/juce_audio_formats for the "decode a bundled IR blob"
    // concept Inhalt doesn't use at all (its IRs are synthesized, never decoded from a file). Keep
    // this in sync with that function if it ever changes - both exist purely so
    // ConvolutionEngine::loadIR() (Normalise::no - see that class's own comment) receives an
    // already-normalized buffer, whichever pipeline produced it.
    void normaliseToUnitEnergy(juce::AudioBuffer<float>& buffer)
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();
        if (numChannels <= 0 || numSamples <= 0)
            return;

        double energy = 0.0;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* samples = buffer.getReadPointer(channel);
            for (int i = 0; i < numSamples; ++i)
                energy += (double) samples[i] * (double) samples[i];
        }

        const auto perChannelEnergy = energy / (double) numChannels;
        if (perChannelEnergy <= 0.0)
            return;

        const auto gain = 1.0f / (float) std::sqrt(perChannelEnergy);
        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::multiply(buffer.getWritePointer(channel), gain, numSamples);
    }

    // Length of the synthesized IR. Comfortably covers the longest measured gate window (~300ms
    // at Time=9.8, see InhaltParameterMap's gateLengthMsCurve) plus margin for the fall to clear
    // the noise floor - matches ml-toolkit/effects/nonlin/fit_nonlin.py's own FIT_DURATION_S
    // reasoning (long enough to cover every capture, not tied to any one Time setting).
    constexpr double irDurationSeconds = 1.0;

    juce::AudioBuffer<float> synthesizeAndNormalize(float timeKnob, float highKnob, double sampleRate)
    {
        const auto gate = InhaltParameterMap::mapTimeAndHighToGateParams(timeKnob, highKnob);
        const auto tank = InhaltParameterMap::mapTimeKnobToTankParams(timeKnob);
        const auto tilt = InhaltParameterMap::mapHighKnobToTilt(highKnob);

        InhaltIRSynth::Params params;
        params.feedbackGain = tank.feedbackGain;
        params.dampingWeight = tank.dampingWeight;
        params.diffuserGain = tank.diffuserGain;
        params.directGain = tank.directGain;
        params.tiltLowGain = tilt.lowGain;
        params.tiltHighGain = tilt.highGain;
        params.tiltPivotHz = tilt.pivotHz;
        params.buildUpMs = gate.buildUpMs;
        params.kneeTimeMs = gate.kneeTimeMs;
        params.kneeSoftnessMs = gate.kneeSoftnessMs;
        params.plateauDroopLowDbPerSec = gate.plateauDroopLowDbPerSec;
        params.plateauDroopMidDbPerSec = gate.plateauDroopMidDbPerSec;
        params.plateauDroopHighDbPerSec = gate.plateauDroopHighDbPerSec;
        params.fallRateLowDbPerSec = gate.fallRateLowDbPerSec;
        params.fallRateMidDbPerSec = gate.fallRateMidDbPerSec;
        params.fallRateHighDbPerSec = gate.fallRateHighDbPerSec;
        params.earlyExcessDb = gate.earlyExcessDb;

        const auto numSamples = (int) (irDurationSeconds * sampleRate);
        std::vector<float> left, right;
        InhaltIRSynth::render(params, sampleRate, numSamples, left, right);

        juce::AudioBuffer<float> buffer(2, numSamples);
        buffer.copyFrom(0, 0, left.data(), numSamples);
        buffer.copyFrom(1, 0, right.data(), numSamples);

        // Matches ConvolutionEngine's own contract exactly (see that class's own comment):
        // loadIR() takes Normalise::no and expects an already-normalized buffer, so every IR - a
        // decoded capture there, a synthesized one here - reaches it at a comparable level and one
        // Wet knob stays meaningful across every Time/High setting.
        normaliseToUnitEnergy(buffer);
        return buffer;
    }
} // namespace

InhaltIRWorker::InhaltIRWorker() : juce::Thread("Inhalt IR synth")
{
}

InhaltIRWorker::~InhaltIRWorker()
{
    stop();
}

void InhaltIRWorker::start()
{
    if (! isThreadRunning())
        startThread(juce::Thread::Priority::low);
}

void InhaltIRWorker::stop()
{
    stopThread(2000);
}

void InhaltIRWorker::requestSynthesis(float timeKnob, float highKnob, double sampleRate) noexcept
{
    requestedTimeKnob.store(timeKnob, std::memory_order_relaxed);
    requestedHighKnob.store(highKnob, std::memory_order_relaxed);
    requestedSampleRate.store(sampleRate, std::memory_order_relaxed);

    // Release so the worker's acquire load of the counter sees all three fields above.
    requestCounter.fetch_add(1, std::memory_order_release);

    notify();
}

bool InhaltIRWorker::tryPopSynthesizedIR(juce::AudioBuffer<float>& destination) noexcept
{
    const juce::SpinLock::ScopedTryLockType lock(slotLock);

    if (! lock.isLocked() || ! pendingReady)
        return false;

    destination = std::move(pendingIR);
    pendingIR = juce::AudioBuffer<float>();
    pendingReady = false;
    return true;
}

juce::AudioBuffer<float> InhaltIRWorker::synthesizeSynchronously(float timeKnob, float highKnob,
                                                                   double sampleRate, int numSamples)
{
    juce::ignoreUnused(numSamples); // kept for API symmetry with a future variable-length render;
    // irDurationSeconds is currently fixed - see that constant's own comment.
    return synthesizeAndNormalize(timeKnob, highKnob, sampleRate);
}

void InhaltIRWorker::pushPendingIR(juce::AudioBuffer<float>&& synthesized)
{
    const juce::SpinLock::ScopedLockType lock(slotLock);
    pendingIR = std::move(synthesized);
    pendingReady = true;
}

void InhaltIRWorker::run()
{
    while (! threadShouldExit())
    {
        if (requestCounter.load(std::memory_order_acquire) == servedCounter)
        {
            wait(-1);
            continue;
        }

        const auto counterBeforeWait = requestCounter.load(std::memory_order_acquire);
        wait(debounceMs);

        if (threadShouldExit())
            return;

        if (requestCounter.load(std::memory_order_acquire) != counterBeforeWait)
            continue; // still moving - restart the quiet period

        const auto serving = requestCounter.load(std::memory_order_acquire);
        const auto timeKnob = requestedTimeKnob.load(std::memory_order_relaxed);
        const auto highKnob = requestedHighKnob.load(std::memory_order_relaxed);
        const auto sampleRate = requestedSampleRate.load(std::memory_order_relaxed);

        if (sampleRate <= 0.0)
        {
            servedCounter = serving;
            continue;
        }

        auto synthesized = synthesizeAndNormalize(timeKnob, highKnob, sampleRate);
        if (synthesized.getNumSamples() > 0)
            pushPendingIR(std::move(synthesized));

        servedCounter = serving;
    }
}

} // namespace inhalt
