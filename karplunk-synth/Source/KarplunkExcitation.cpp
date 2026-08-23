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

void KarplunkExcitation::prepare(double sampleRate) noexcept
{
    sampleRateHz = sampleRate;
    reset();
}

void KarplunkExcitation::reset() noexcept
{
    lowpassState = 0.0f;
    bowNoiseLowpassState = 0.0f;
    stage = Stage::Idle;
    envelope = 0.0f;
}

void KarplunkExcitation::setBrightness(float amount01) noexcept
{
    brightness = amount01;
}

void KarplunkExcitation::setBowAmount(float amount01) noexcept
{
    bowAmount = amount01;
}

void KarplunkExcitation::setBaseDuration(int delaySamples) noexcept
{
    baseDurationSamples = (float) delaySamples;
    stage = Stage::Attack;
    envelope = 0.0f;
}

void KarplunkExcitation::noteOff() noexcept
{
    if (stage != Stage::Idle)
        stage = Stage::Release;
}

float KarplunkExcitation::nextNoiseSample() noexcept
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

float KarplunkExcitation::nextBowNoiseSample() noexcept
{
    // xorshift32, same technique as nextNoiseSample() but its own independent state - see this
    // class's own header comment for why the friction model's noise floor needs to stay independent
    // of the Pluck side's (Brightness-shaped) noise source. Lowpassed (fixed coefficient, not tied
    // to Brightness - see bowNoiseLowpassCoeff's own comment) - raw white noise measured, by ear, as
    // reading like continuous hiss rather than bow character once mixed into the resonant tone.
    bowNoiseRngState ^= bowNoiseRngState << 13;
    bowNoiseRngState ^= bowNoiseRngState >> 17;
    bowNoiseRngState ^= bowNoiseRngState << 5;
    const auto whiteNoise = (float) bowNoiseRngState / (float) UINT32_MAX * 2.0f - 1.0f;
    bowNoiseLowpassState += bowNoiseLowpassCoeff * (whiteNoise - bowNoiseLowpassState);
    return bowNoiseLowpassState;
}

float KarplunkExcitation::nextFrictionSample(float stringSignal) noexcept
{
    // Bow Force -> friction-curve slope, STK's own Bow Pressure mapping/span - see this class's
    // own header comment for the full derivation and citation.
    const auto frictionSlope = maxFrictionSlope - bowForce * (maxFrictionSlope - minFrictionSlope);

    // vBow: commanded bow velocity, ADSR-shaped by THIS class's own existing `envelope` (already
    // computed every tick by the unchanged Attack/DecayToSustain/Release state machine in
    // nextExcitationSample()) - reuses the same envelope driving the Pluck side rather than a
    // second, separately-tuned one, avoiding this file's own historical bug #3 (two curves racing).
    const auto vBow = envelope * maxBowVelocityAnalog;
    const auto vDelta = vBow - stringSignal * stringVelocityGain;

    // STK's BowTable::tick(), verbatim formula - see this class's own header comment for the
    // unconditional-boundedness argument.
    const auto x = std::abs(frictionSlope * vDelta + frictionOffset) + 0.75f;
    const auto rho = std::clamp(std::pow(x, -4.0f), frictionMinOutput, frictionMaxOutput);

    // bowNoiseAmount * noise, scaled by `envelope` so a fresh note's noise floor fades in with the
    // same attack shape as the friction curve itself rather than clicking on at full amplitude on
    // the very first tick - see this class's own header comment (nextFrictionSample's) for why this
    // term exists at all: without it, a held bow note's delay-line content converges to uniform DC
    // and Position's own tap cancels it to exact silence.
    const auto bowNoise = bowNoiseAmount * envelope * nextBowNoiseSample();

    return vDelta * rho + bowNoise;
}

float KarplunkExcitation::nextExcitationSample(float velocity01, float stringSignal) noexcept
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

    const auto pluckComponent = nextNoiseSample() * velocity01 * envelope;

    // Explicit gate, not reliance on 0*x=0 - matches this codebase's own established convention
    // (Structure/Waveshape/Ring Mod all skip their respective computation entirely at amount=0
    // rather than trusting the arithmetic to cancel out) and guarantees bowAmount=0 stays bit-exact
    // with this class's pre-friction-model behavior, sample for sample.
    if (bowAmount <= 0.0f)
        return pluckComponent;

    const auto frictionComponent = nextFrictionSample(stringSignal) * frictionLevelAnalog;
    return (1.0f - bowAmount) * pluckComponent + bowAmount * frictionComponent;
}
