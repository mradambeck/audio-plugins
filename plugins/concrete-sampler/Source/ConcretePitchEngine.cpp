#include "ConcretePitchEngine.h"

#include <cmath>

namespace
{
    // Same 4-point Catmull-Rom cubic interpolation as Phase 1's original reference path (moved
    // here verbatim from ConcreteVoice.cpp), with indices clamped to [0, dataLength) at the edges.
    float cubicInterpolate(const float* data, int dataLength, double position) noexcept
    {
        const auto i1 = (int) std::floor(position);
        const auto frac = (float) (position - (double) i1);

        const auto sampleAt = [&](int index) -> float
        {
            return data[juce::jlimit(0, dataLength - 1, index)];
        };

        const auto y0 = sampleAt(i1 - 1);
        const auto y1 = sampleAt(i1);
        const auto y2 = sampleAt(i1 + 1);
        const auto y3 = sampleAt(i1 + 2);

        const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const auto a2 = -0.5f * y0 + 0.5f * y2;
        const auto a3 = y1;

        return ((a0 * frac + a1) * frac + a2) * frac + a3;
    }

    // 64x oversampling relative to baseRateHz - see the Ensoniq ASR-10 preset table entry in
    // concrete-sampler-plugin-plan.md ("~30kHz effective, 64x oversampled"). This is the single
    // biggest per-sample cost in the whole plugin (see Phase 2's CPU analysis) - deliberately a
    // named constant, not buried in a formula, since it's the first thing to reduce if profiling
    // shows it's unaffordable.
    constexpr double deltaSigmaOversamplingFactor = 64.0;

    // Single-pole lowpass used to decimate the delta-sigma modulator's raw +-1 bitstream back down
    // to a musically useful signal (see readModeC's own comment for why this step is necessary,
    // not just a shortcut). At the ASR-10's ~30kHz base rate this puts the -3dB point close to the
    // 15kHz boundary Phase 2's own in-band/out-of-band noise-floor analysis uses.
    constexpr float decimationFilterCoefficient = 0.05f;
}

float ConcretePitchEngine::readReference(const float* data, int dataLength, double pitchRatio, double sourceRateHz) noexcept
{
    const auto phaseIncrement = pitchRatio * sourceRateHz / hostSampleRate;
    const auto value = cubicInterpolate(data, dataLength, sourcePhase);
    sourcePhase += phaseIncrement;
    return value;
}

float ConcretePitchEngine::readModeA(const float* data, int dataLength, double pitchRatio, double baseRateHz) noexcept
{
    // Zero-order hold (nearest-neighbor) instead of interpolating - the deliberate artifact.
    // baseRateHz here plays the role Phase 1's zone.sourceSampleRate did: the assumed effective
    // clock the hardware would have played `data` back at, which the caller substitutes in for
    // the file's own real rate (see this class's header comment on effectiveSourceRateHz).
    const auto phaseIncrement = pitchRatio * baseRateHz / hostSampleRate;
    const auto index = juce::jlimit(0, dataLength - 1, (int) std::floor(sourcePhase));
    const auto value = data[index];
    sourcePhase += phaseIncrement;
    return value;
}

float ConcretePitchEngine::readModeB(const float* data, int dataLength, double pitchRatio, double baseRateHz) noexcept
{
    // Output ticks at a FIXED rate (baseRateHz) regardless of pitch - unlike Mode A, where the
    // effective clock itself moves with transposition. Pitch instead comes from how fast the
    // SOURCE read position advances per fixed-rate tick: dropping samples (advancing by more than
    // 1 per tick) to pitch up, repeating them (advancing by less than 1, so several ticks in a row
    // land on the same floor()'d index) to pitch down. Held (zero-order hold) between ticks either
    // way, which is what gives this mode its always-present, pitch-independent imaging character.
    baseRateTickPhase += baseRateHz / hostSampleRate;

    if (pendingFirstTick)
    {
        pendingFirstTick = false;
        const auto index = juce::jlimit(0, dataLength - 1, (int) std::floor(sourcePhase));
        heldSample = data[index];
    }

    while (baseRateTickPhase >= 1.0)
    {
        baseRateTickPhase -= 1.0;
        sourcePhase += pitchRatio;
        const auto index = juce::jlimit(0, dataLength - 1, (int) std::floor(sourcePhase));
        heldSample = data[index];
    }

    return heldSample;
}

float ConcretePitchEngine::readModeC(const float* data, int dataLength, double pitchRatio, double baseRateHz) noexcept
{
    // A raw delta-sigma bitstream is +-1 every oversampled tick - averaging/decimating it is not
    // an approximation of the real DAC, it's the same reconstruction step a real delta-sigma DAC's
    // own analog lowpass performs, and it's what actually reveals the noise-shaped signal (in-band
    // content clean, quantization noise pushed above the audible range) rather than a literal
    // square wave.
    const auto oversampledRate = baseRateHz * deltaSigmaOversamplingFactor;
    const auto sourceStepPerOversampleTick = pitchRatio / deltaSigmaOversamplingFactor;
    const auto oversampleTicksPerHostSample = oversampledRate / hostSampleRate;

    oversampleTickAccumulator += oversampleTicksPerHostSample;
    const auto numTicks = (int) oversampleTickAccumulator;
    oversampleTickAccumulator -= (double) numTicks;

    for (int i = 0; i < numTicks; ++i)
    {
        const auto index = juce::jlimit(0, dataLength - 1, (int) std::floor(sourcePhase));
        const auto inputSample = data[index];
        sourcePhase += sourceStepPerOversampleTick;

        const auto delta = inputSample - deltaSigmaFeedback;
        deltaSigmaIntegrator += delta;
        const auto bit = deltaSigmaIntegrator >= 0.0f ? 1.0f : -1.0f;
        deltaSigmaFeedback = bit;

        // Two cascaded single-pole stages, not one. A single stage's rolloff (~6dB/octave) is far
        // too gentle to sufficiently attenuate the noise-shaped bitstream's own high-frequency
        // content by the time it reaches the oversampled Nyquist (tens of octaves above the
        // audible range at 64x oversampling) - confirmed empirically: a single stage left the
        // audible-band noise floor flat and ~15dB louder than this cascade produces, because that
        // un-suppressed high-frequency energy was aliasing straight back down into the audible
        // range on every host-rate readout, exactly the failure a decimation filter exists to
        // prevent. Two stages is the minimum that measurably fixed it without simulation-only
        // over-engineering (a sharp multi-stage FIR/CIC decimator, closer to what a real
        // delta-sigma DAC uses, is more correct still but not needed to demonstrate the intended
        // character here).
        auto x = bit;
        for (auto& stage : decimationFilterState)
        {
            stage += decimationFilterCoefficient * (x - stage);
            x = stage;
        }
    }

    return decimationFilterState.back();
}
