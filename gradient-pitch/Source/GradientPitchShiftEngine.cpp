#include "GradientPitchShiftEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace
{
    // Matches delayTimeMsA/B's parameter ceiling (set when that parameter is registered in
    // PluginProcessor) - duplicated here as a plain constant since the engine doesn't read APVTS.
    constexpr float maxDelayTimeMs = 1000.0f;

    // Ceiling for Drift's excursion (Milestone 5) - reserved back at Milestone 2 so the buffer
    // never needs resizing (resizing on the audio thread isn't safe, and this engine's buffer is
    // only ever sized once, in prepare()); now the actual max excursion Drift's 100% setting uses.
    // 25ms, not the original placeholder 5ms: by-ear testing found the smaller value (combined with
    // the original slower lowpass cutoff) produced a rate of change too tiny to be audible at all,
    // even at 100% - see driftLowpassCutoffHz's comment for why RATE, not just excursion size,
    // is what actually determines audible depth here. 25ms + 1Hz cutoff targets roughly a 5%
    // instantaneous pitch deviation at 100% Drift - clearly audible without being extreme.
    constexpr float maxDriftExcursionMs = 25.0f;

    constexpr float safetyMarginMs = 50.0f;

    constexpr float pi = 3.14159265358979323846f;
}

void GradientPitchShiftEngine::prepare(double sampleRate) noexcept
{
    sampleRateHz = sampleRate;

    buffer.setSize(requiredBufferCapacitySamples(sampleRate));

    rampWindowSamples = rampWindowMs * 0.001f * (float) sampleRate;
    maxSpliceSearchSamples = (int) std::round(maxSpliceSearchMs * 0.001f * (float) sampleRate);

    // One-pole lowpass coefficient for the fixed darkening filter - coeff = 1 - exp(-2*pi*fc/fs).
    darkeningCoeff = 1.0f - std::exp(-2.0f * pi * darkeningCutoffHz / (float) sampleRate);

    // Classic one-pole DC-blocker pole radius for the given cutoff.
    dcBlockerR = 1.0f - (2.0f * pi * dcBlockerCutoffHz / (float) sampleRate);

    // One-pole lowpass coefficient for Drift's random walk - deliberately very slow (see the
    // constant's own comment).
    driftCoeff = 1.0f - std::exp(-2.0f * pi * driftLowpassCutoffHz / (float) sampleRate);
    maxDriftExcursionSamples = maxDriftExcursionMs * 0.001f * (float) sampleRate;

    // Address-based seed (once, here, not on every reset()) so two engine instances - dual mode,
    // Milestone 6 - never wander in lockstep. Never zero: xorshift is stuck at 0 forever otherwise.
    driftRngState = (uint32_t) reinterpret_cast<uintptr_t>(this) ^ 0x9E3779B9u;
    if (driftRngState == 0)
        driftRngState = 1;

    reset();
}

void GradientPitchShiftEngine::setDriftSeedForTesting(uint32_t seed) noexcept
{
    driftRngState = (seed == 0) ? 1 : seed; // never zero - xorshift is stuck at 0 forever otherwise
}

void GradientPitchShiftEngine::reset() noexcept
{
    buffer.reset();

    tapDelay[0] = 0.0f;
    tapDelay[1] = rampWindowSamples * 0.5f; // 180 degrees out of phase with tap 0
    awaitingSplice[0] = awaitingSplice[1] = false;
    spliceSearchCountdown[0] = spliceSearchCountdown[1] = 0;
    lastTapSample[0] = lastTapSample[1] = 0.0f;
    searchDebt[0] = searchDebt[1] = 0.0f;
    lastWetSample = 0.0f;
    darkeningFilterState = 0.0f;
    dcBlockerPrevInput = 0.0f;
    dcBlockerPrevOutput = 0.0f;
    driftState = 0.0f;
}

int GradientPitchShiftEngine::requiredBufferCapacitySamples(double sampleRate) noexcept
{
    // Sizing rule from the implementation plan: capacity = sample rate x the SUM (not max) of the
    // ranges that can all be near their ceiling simultaneously - Delay maxed out while the pitch
    // ramp and (eventually) Drift are also near their own ceilings.
    const auto totalMs = maxDelayTimeMs + rampWindowMs + maxDriftExcursionMs + safetyMarginMs;
    return (int) std::ceil(totalMs * 0.001 * sampleRate);
}

void GradientPitchShiftEngine::setPitchSemitones(float coarseSemitones, float fineCents) noexcept
{
    // PluginProcessor calls this every block regardless of whether the knob moved - skip the pow()
    // below when it hasn't. pitchSemitones/pitchFineCents already double as the "last seen" cache
    // (nothing else in this class reads them), and their 0.0f/0.0f default coincides exactly with
    // rampRate's own 0.0f default (pow(2,0)==1, rampRate==1-1==0), so skipping a first call of
    // (0,0) is provably correct, not an accidental gap. std::abs(...) <= 0.0f, not ==, to avoid
    // -Wfloat-equal (this repo's convention - see ShieldsFDNEngine's lastAppliedSizeMultiplier).
    if (std::abs(coarseSemitones - pitchSemitones) <= 0.0f && std::abs(fineCents - pitchFineCents) <= 0.0f)
        return;

    pitchSemitones = coarseSemitones;
    pitchFineCents = fineCents;

    const auto totalSemitones = pitchSemitones + pitchFineCents * 0.01f;
    const auto pitchRatio = std::pow(2.0f, totalSemitones / 12.0f);

    // Reading the buffer back at pitchRatio x real-time speed means the read position must move
    // through the delay's own timeline at (pitchRatio - 1) samples per sample faster than the
    // write head does - i.e. the tap's delay value must change by -(pitchRatio - 1) samples per
    // sample. Ratio > 1 (pitch up) -> rampRate negative -> delay decreases toward 0. Ratio < 1
    // (pitch down) -> rampRate positive -> delay increases toward rampWindowSamples.
    rampRate = 1.0f - pitchRatio;
}

void GradientPitchShiftEngine::setDelayTimeMs(float delayMs) noexcept
{
    delayTimeSamples = delayMs * 0.001f * (float) sampleRateHz;
}

void GradientPitchShiftEngine::setFeedback(float feedbackPercent) noexcept
{
    feedbackGain = feedbackPercent * 0.01f;
}

void GradientPitchShiftEngine::setMix(float mixPercent) noexcept
{
    mixAmount = mixPercent * 0.01f;
}

void GradientPitchShiftEngine::setOutputTrimDb(float trimDb) noexcept
{
    // Same redundant-pow() guard as setPitchSemitones() above. lastOutputTrimDb's 0.0f default
    // coincides with outputGain's own default (pow(10,0/20)==1.0f), so skipping a first call of
    // 0.0f is correct for the same reason.
    if (std::abs(trimDb - lastOutputTrimDb) <= 0.0f)
        return;

    lastOutputTrimDb = trimDb;
    outputGain = std::pow(10.0f, trimDb / 20.0f);
}

void GradientPitchShiftEngine::setSpliceMode(SpliceMode mode) noexcept
{
    spliceMode = mode;
}

void GradientPitchShiftEngine::setCrossfadeLengthMs(float ms) noexcept
{
    crossfadeLengthMs = ms;
}

void GradientPitchShiftEngine::setDrift(float driftPercent) noexcept
{
    driftAmount = driftPercent * 0.01f;
}

float GradientPitchShiftEngine::readTap(int tapIndex, float driftOffsetSamples) const noexcept
{
    return buffer.readInterpolated(delayTimeSamples + tapDelay[tapIndex] + driftOffsetSamples);
}

float GradientPitchShiftEngine::trapezoidGain(float delay, float window, float crossfadeLength) noexcept
{
    // Clamped at 0: Drift can push the effective (tapDelay + drift) position slightly outside
    // [0, window] between wraps, which would otherwise make this go negative instead of reading
    // as "already at the boundary, zero gain".
    const auto distanceToNearestBoundary = std::max(0.0f, std::min(delay, window - delay));
    if (crossfadeLength <= 0.0f)
        return distanceToNearestBoundary > 0.0f ? 1.0f : 0.0f;
    return std::min(1.0f, distanceToNearestBoundary / crossfadeLength);
}

float GradientPitchShiftEngine::nextDriftNoise() noexcept
{
    // xorshift32 - fast, tiny, no allocation, good enough statistically for a lowpassed noise walk.
    driftRngState ^= driftRngState << 13;
    driftRngState ^= driftRngState >> 17;
    driftRngState ^= driftRngState << 5;
    return (float) driftRngState / (float) UINT32_MAX * 2.0f - 1.0f;
}

float GradientPitchShiftEngine::getEffectiveCrossfadeSamples() const noexcept
{
    // A DISTANCE (samples of delay), not a time duration - see the class comment for why this
    // design doesn't need it to track any particular playback rate. Clamped to just under half
    // the window as a safety margin, not a hard correctness requirement (the normalized gain sum
    // stays exactly 1 regardless), so it doesn't need to vary with pitch at all.
    const auto requestedMs = (spliceMode == SpliceMode::glitch) ? fixedCrossfadeMs : crossfadeLengthMs;
    const auto requestedSamples = requestedMs * 0.001f * (float) sampleRateHz;
    return std::min(requestedSamples, rampWindowSamples * 0.49f);
}

std::optional<float> GradientPitchShiftEngine::advanceTap(int tapIndex, bool pitchingUp, float driftOffsetSamples) noexcept
{
    const int otherTapIndex = 1 - tapIndex;

    if (awaitingSplice[tapIndex])
    {
        // Frozen exactly at the boundary - buffer content advances at real-time rate while we
        // search for a favourable (zero-crossing/low-energy) moment to complete the splice. Gain
        // is already ~0 here (see trapezoidGain), so this hold is inaudible on its own.
        const auto candidateSample = buffer.readInterpolated(delayTimeSamples + tapDelay[tapIndex] + driftOffsetSamples);
        const auto crossedZero = (lastTapSample[tapIndex] > 0.0f && candidateSample <= 0.0f)
                               || (lastTapSample[tapIndex] < 0.0f && candidateSample >= 0.0f);
        const auto lowEnergy = std::abs(candidateSample) < lowEnergyThreshold;

        --spliceSearchCountdown[tapIndex];
        searchDebt[tapIndex] += 1.0f;
        lastTapSample[tapIndex] = candidateSample; // unconditional either way below, so set it up front

        if (crossedZero || lowEnergy || spliceSearchCountdown[tapIndex] <= 0)
        {
            tapDelay[tapIndex] = pitchingUp ? rampWindowSamples : 0.0f;
            awaitingSplice[tapIndex] = false;
            return std::nullopt; // tapDelay just changed - candidateSample is now stale, see header comment
        }

        return candidateSample; // still frozen at the position a readTap() call would read
    }

    tapDelay[tapIndex] += rampRate;

    // Drift is purely additive onto this test (per the implementation plan's Milestone 2 decision
    // to keep reset detection value-driven specifically so Drift could be added this way) - it can
    // nudge a wrap slightly early or late, which is the actual "wow and flutter" character, without
    // touching how tapDelay itself is bookkept below.
    const auto effectivePosition = tapDelay[tapIndex] + driftOffsetSamples;
    const bool crossedBoundary = (pitchingUp && effectivePosition <= 0.0f)
                               || (!pitchingUp && effectivePosition >= rampWindowSamples);
    if (!crossedBoundary)
        return std::nullopt;

    if (spliceMode == SpliceMode::deglitchSmart)
    {
        tapDelay[tapIndex] = pitchingUp ? 0.0f : rampWindowSamples;

        // Throttle this wrap's search budget by how far this tap has already drifted ahead of its
        // partner in accumulated search time - a negative-feedback correction that keeps the two
        // taps' mutual half-window phase relationship from random-walking apart over many wraps
        // (see the searchDebt member comment). A tap that's already spent more time searching than
        // its partner gets a shorter, or zero, budget this time, letting the partner catch up.
        const auto relativeDebt = searchDebt[tapIndex] - searchDebt[otherTapIndex];
        const auto throttledBudget = maxSpliceSearchSamples - (int) std::round(relativeDebt);

        if (throttledBudget <= 0)
        {
            // No budget left this wrap - wrap immediately, same as glitch/soft, so the partner can
            // catch up (its own debt keeps growing while this tap's stays flat).
            tapDelay[tapIndex] += pitchingUp ? rampWindowSamples : -rampWindowSamples;
            return std::nullopt;
        }

        awaitingSplice[tapIndex] = true;
        spliceSearchCountdown[tapIndex] = throttledBudget;
        const auto sample = buffer.readInterpolated(delayTimeSamples + tapDelay[tapIndex] + driftOffsetSamples);
        lastTapSample[tapIndex] = sample;
        return sample; // tapDelay is frozen here and untouched for the rest of this call
    }

    // Glitch / de-glitch soft: wrap immediately, preserving the tiny overshoot past the
    // boundary - matches Milestone 2's already-verified behaviour exactly.
    tapDelay[tapIndex] += pitchingUp ? rampWindowSamples : -rampWindowSamples;
    return std::nullopt;
}

float GradientPitchShiftEngine::safetyDarken(float x) noexcept
{
    // Feedback off (feedbackGain==0, the shipped default) makes x exactly 0.0f every sample - skip
    // the transcendental call in that case (std::tanh(0)==0 exactly per IEEE-754/C++ <cmath>, so
    // this is bit-identical, not an approximation). Passing x through rather than a 0.0f literal
    // preserves sign-of-zero for true bit-exactness. The filter's own lowpass state still updates
    // every sample regardless, so a feedback-on-to-off transition still decays correctly.
    const auto saturated = (std::abs(x) <= 0.0f) ? x : (std::tanh(x * fixedSafetyDrive) / fixedSafetyDrive);
    darkeningFilterState += (saturated - darkeningFilterState) * darkeningCoeff;
    return darkeningFilterState;
}

float GradientPitchShiftEngine::outputSafetyLimit(float x) noexcept
{
    return std::tanh(x);
}

float GradientPitchShiftEngine::dcBlock(float x) noexcept
{
    const auto y = x - dcBlockerPrevInput + dcBlockerR * dcBlockerPrevOutput;
    dcBlockerPrevInput = x;
    dcBlockerPrevOutput = y;
    return y;
}

float GradientPitchShiftEngine::process(float drySample, float externalFeedbackSample) noexcept
{
    // Feedback junction: tap the WET signal from the previous sample (this engine's own
    // regeneration, plus - from Milestone 6d onward - the other engine's cross-feedback), scale by
    // the Feedback knob, then run it through a fixed safety saturator + darkening filter, then a DC
    // blocker (see dcBlock()'s comment - pitch-shifting DC content costs no splice energy, so it
    // can self-sustain silently at far lower Feedback than any audible tone needs, and won't decay
    // even when Feedback is lowered) before summing with the dry input. Uses lastWetSample from the
    // PREVIOUS call - it's only updated to this sample's new value at the end of this function,
    // after being read here.
    const auto feedbackBusRaw = lastWetSample + externalFeedbackSample;
    const auto feedbackBusScaled = feedbackBusRaw * feedbackGain;
    const auto feedbackBusSafe = safetyDarken(feedbackBusScaled);
    const auto feedbackBusClean = dcBlock(feedbackBusSafe);

    buffer.write(drySample + feedbackBusClean);

    // Drift: a slow, one-pole-lowpassed random walk (NOT an LFO - see the class/member comments),
    // updated once per sample regardless of splice-mode/bypass path so it stays continuous if the
    // user toggles Pitch on/off. Scaled by the user's Drift amount and the same excursion ceiling
    // the buffer was sized to accommodate back in Milestone 2.
    const auto driftNoise = nextDriftNoise();
    driftState += (driftNoise - driftState) * driftCoeff;
    const auto driftOffsetSamples = driftState * driftAmount * maxDriftExcursionSamples;

    float wetSample;

    if (std::abs(rampRate) < 1.0e-6f)
    {
        // No pitch shift requested - a single clean tap at the base delay. No ramping/crossfading
        // needed (or wanted) at all - Drift still wobbles this single read, matching a real tape
        // delay's wow/flutter affecting every read regardless of pitch shift.
        wetSample = buffer.readInterpolated(delayTimeSamples + driftOffsetSamples);
    }
    else
    {
        const bool pitchingUp = rampRate < 0.0f;

        // advanceTap() returns the sample it already read as a side effect, when the Smart-splice
        // search's own zero-crossing/low-energy probe happens to land at the exact position a
        // readTap() call below would read anyway - avoiding reading the same delay-buffer position
        // twice per sample during a search (see advanceTap()'s header comment for exactly which
        // cases qualify). std::nullopt means no such reusable read exists this sample, so the
        // readTap() fallback below reproduces the unconditional call this class always made here.
        const auto cachedRead0 = advanceTap(0, pitchingUp, driftOffsetSamples);
        const auto cachedRead1 = advanceTap(1, pitchingUp, driftOffsetSamples);

        const auto effectiveCrossfadeSamples = getEffectiveCrossfadeSamples();
        const auto gain0 = trapezoidGain(tapDelay[0] + driftOffsetSamples, rampWindowSamples, effectiveCrossfadeSamples);
        const auto gain1 = trapezoidGain(tapDelay[1] + driftOffsetSamples, rampWindowSamples, effectiveCrossfadeSamples);
        const auto gainSum = gain0 + gain1;

        // Normalized so the two taps always sum to exactly 1 (an equal-gain crossfade) - this is
        // what keeps the blend from ever going through a "both near-silent" dip or a "both near-
        // full" bump, regardless of the two independently-computed trapezoid shapes.
        const auto normGain0 = gainSum > 1.0e-6f ? gain0 / gainSum : 0.5f;
        const auto normGain1 = gainSum > 1.0e-6f ? gain1 / gainSum : 0.5f;

        const auto tapSample0 = cachedRead0 ? *cachedRead0 : readTap(0, driftOffsetSamples);
        const auto tapSample1 = cachedRead1 ? *cachedRead1 : readTap(1, driftOffsetSamples);

        // Direct, per-sample proof of the optimization's core invariant (not just an aggregate
        // output diff): whenever advanceTap() claims a cached read is reusable, it must be
        // bit-identical to what an independent, authoritative readTap() call would compute right
        // now. assert(), not JUCE's jassert - this class deliberately has no JUCE dependency (see
        // the class comment) - compiled out via NDEBUG in Release, same as GradientDelayBuffer's
        // own wrap-invariant assert.
        assert(!cachedRead0 || *cachedRead0 == readTap(0, driftOffsetSamples));
        assert(!cachedRead1 || *cachedRead1 == readTap(1, driftOffsetSamples));

        wetSample = tapSample0 * normGain0 + tapSample1 * normGain1;
    }

    lastWetSample = wetSample;

    const auto mixed = drySample * (1.0f - mixAmount) + wetSample * mixAmount;
    return outputSafetyLimit(mixed * outputGain);
}
