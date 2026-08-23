#include "KarplunkExcitation.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Minimum one-pole lowpass coefficient at brightness = 0 - heavy but not frozen smoothing
    // (alpha = 0 would never update the filter state at all). At brightness = 1, alpha = 1.0,
    // an exact passthrough (raw white noise, no filtering).
    constexpr float minLowpassAlpha = 0.02f;
}

void NoiseExcitation::prepare(double sampleRate) noexcept
{
    sampleRateHz = sampleRate;
    reset();
}

void NoiseExcitation::reset() noexcept
{
    lowpassState = 0.0f;
    stage = Stage::Idle;
    envelope = 0.0f;
}

void NoiseExcitation::setBrightness(float amount01) noexcept
{
    brightness = amount01;
}

void NoiseExcitation::setBowAmount(float amount01) noexcept
{
    bowAmount = amount01;
}

void NoiseExcitation::setBaseDuration(int delaySamples) noexcept
{
    baseDurationSamples = (float) delaySamples;
    stage = Stage::Attack;
    envelope = 0.0f;
}

void NoiseExcitation::noteOff() noexcept
{
    if (stage != Stage::Idle)
        stage = Stage::Release;
}

float NoiseExcitation::nextNoiseSample() noexcept
{
    // xorshift32 - fast, allocation-free, deterministic given a seed. Not juce::Random, to keep
    // this class free of any JUCE dependency (see header comment).
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;

    const auto whiteNoise = (float) rngState / (float) UINT32_MAX * 2.0f - 1.0f;

    const auto alpha = minLowpassAlpha + brightness * (1.0f - minLowpassAlpha);
    lowpassState += alpha * (whiteNoise - lowpassState);
    return lowpassState;
}

float NoiseExcitation::nextExcitationSample(float velocity01) noexcept
{
    if (stage == Stage::Idle)
        return 0.0f;

    // Attack: fastAttackSamples (pluck) -> slowAttackSeconds worth of samples (bow), clamped so a
    // short (high-pitched) note's attack never eats more than a quarter of its own base duration -
    // otherwise a high note would land at a lower peak than a low note for the same bowAmount and
    // velocity, a pitch-dependent artifact the old fixed-length burst never had.
    const auto slowAttackSamples = (float) (slowAttackSeconds * sampleRateHz);
    const auto clampedFastAttack = std::min(fastAttackSamples, baseDurationSamples * 0.25f);
    const auto attackTimeSamples = clampedFastAttack + bowAmount * (slowAttackSamples - clampedFastAttack);
    const auto attackCoeff = 1.0f - std::exp(-1.0f / attackTimeSamples);

    // Decay-to-sustain: baseDurationSamples * durationMultiplier (pluck) -> slowDecaySeconds worth
    // of samples (bow), interpolated linearly in TIME, same as attack - deliberately not linearly
    // in coefficient (see this class's header comment for why that mismatch was the actual bug).
    const auto baseDecayTimeSamples = baseDurationSamples * durationMultiplier;
    const auto slowDecaySamples = (float) (slowDecaySeconds * sampleRateHz);
    const auto decayTimeSamples = baseDecayTimeSamples + bowAmount * (slowDecaySamples - baseDecayTimeSamples);
    const auto decayCoeff = 1.0f - std::exp(-1.0f / decayTimeSamples);

    // Release: fastReleaseSeconds (pluck) -> slowReleaseSeconds worth of samples (bow), same
    // linear-in-time interpolation.
    const auto fastReleaseSamples = fastReleaseSeconds * (float) sampleRateHz;
    const auto slowReleaseSamples = slowReleaseSeconds * (float) sampleRateHz;
    const auto releaseTimeSamples = fastReleaseSamples + bowAmount * (slowReleaseSamples - fastReleaseSamples);
    const auto releaseCoeff = 1.0f - std::exp(-1.0f / releaseTimeSamples);

    // The parameter that actually fixes the loudness-consistency bug: how loud the note stays is
    // now this one explicit, monotonic value, decoupled from how fast any stage moves.
    const auto sustainLevel = bowAmount;

    switch (stage)
    {
        case Stage::Attack:
            envelope += attackCoeff * (1.0f - envelope);
            if (envelope > 0.999f)
                stage = Stage::DecayToSustain;
            break;

        case Stage::DecayToSustain:
            // Never "finishes" into a separate Sustain stage - a one-pole recurrence naturally
            // settles at and holds its target, and re-evaluating sustainLevel from bowAmount every
            // tick means a held note's loudness keeps tracking the knob live, not just at the
            // instant decay happened to complete.
            envelope += decayCoeff * (sustainLevel - envelope);
            break;

        case Stage::Release:
            envelope += releaseCoeff * (0.0f - envelope);
            break;

        case Stage::Idle:
            break;
    }

    return nextNoiseSample() * velocity01 * envelope;
}
