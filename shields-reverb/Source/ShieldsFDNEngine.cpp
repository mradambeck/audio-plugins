#include "ShieldsFDNEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float pi = 3.14159265358979323846f;

    float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

    int msToSamples(float ms, double sampleRateHz)
    {
        return std::max(1, (int) std::round(ms * 0.001f * (float) sampleRateHz));
    }

    // Linear interpolation between the two integer taps bracketing a fractional delay - used only
    // by Wobble (see setWobble()'s comment); the plain integer-tap read elsewhere in this file is
    // untouched, so Wobble=0 never routes through this at all.
    float readFractionalDelay(const std::vector<float>& buf, int writePos, float delaySamplesFrac)
    {
        const auto bufSize = (int) buf.size();
        const auto delayFloor = (int) delaySamplesFrac;
        const auto frac = delaySamplesFrac - (float) delayFloor;
        const auto pos0 = (writePos + bufSize - delayFloor) % bufSize;
        const auto pos1 = (pos0 + bufSize - 1) % bufSize;
        const auto y0 = buf[(size_t) pos0];
        const auto y1 = buf[(size_t) pos1];
        return y0 + (y1 - y0) * frac;
    }
}

// Sylvester construction: H1 = [1]; H(2N) = [[H_N, H_N], [H_N, -H_N]]. Listed here fully expanded
// (not built at runtime) since it's a fixed 8x8 constant - normalisation by 1/sqrt(8) happens once
// per sample in processStereo() rather than being baked into these entries, so the entries stay
// exactly +-1 and easy to eyeball against the standard construction.
const std::array<std::array<float, ShieldsFDNEngine::numLines>, ShieldsFDNEngine::numLines> ShieldsFDNEngine::hadamard {{
    {{ 1,  1,  1,  1,  1,  1,  1,  1 }},
    {{ 1, -1,  1, -1,  1, -1,  1, -1 }},
    {{ 1,  1, -1, -1,  1,  1, -1, -1 }},
    {{ 1, -1, -1,  1,  1, -1, -1,  1 }},
    {{ 1,  1,  1,  1, -1, -1, -1, -1 }},
    {{ 1, -1,  1, -1, -1,  1, -1,  1 }},
    {{ 1,  1, -1, -1, -1, -1,  1,  1 }},
    {{ 1, -1, -1,  1, -1,  1,  1, -1 }},
}};

void ShieldsFDNEngine::AllpassStage::prepare(int maxDelaySamples)
{
    buffer.assign((size_t) std::max(maxDelaySamples, 1), 0.0f);
    delaySamples = (int) buffer.size();
    writePos = 0;
}

void ShieldsFDNEngine::AllpassStage::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float ShieldsFDNEngine::AllpassStage::processSample(float x)
{
    const auto readPos = (writePos + (int) buffer.size() - delaySamples) % (int) buffer.size();
    const auto delayed = buffer[(size_t) readPos];

    const auto y = -coefficient * x + delayed;
    buffer[(size_t) writePos] = x + coefficient * y;

    writePos = (writePos + 1) % (int) buffer.size();
    return y;
}

void ShieldsFDNEngine::BurstCombLine::prepare(int maxDelaySamples)
{
    buffer.assign((size_t) std::max(maxDelaySamples, 1), 0.0f);
    writePos = 0;
}

void ShieldsFDNEngine::BurstCombLine::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
    fadeWeight = 0.0f;
}

float ShieldsFDNEngine::BurstCombLine::processSample(float x)
{
    // Read-before-write, same ordering as the main tank's lines: the OUTPUT is whatever was
    // written delaySamples ago, not this sample's input. That's what makes y[0] == 0 for an
    // impulse into an empty buffer - the input only starts appearing in the output D samples
    // later, which is the entire point (an immediate, undiminished pass-through here would let
    // the full click straight through and defeat the burst stage's whole purpose).
    const auto bufSize = (int) buffer.size();
    const auto readPos = (writePos + bufSize - delaySamples) % bufSize;
    auto y = buffer[(size_t) readPos];

    // Mid-crossfade after a Size change (see lengthChangeFadeMs) - blend in the old tap position,
    // fading it out linearly. Both taps read from the SAME still-live buffer (nothing was cleared),
    // so this is a genuine crossfade between two valid delayed signals, not a fade from/to silence.
    if (fadeWeight > 0.0f)
    {
        const auto oldReadPos = (writePos + bufSize - fadeFromDelay) % bufSize;
        const auto oldY = buffer[(size_t) oldReadPos];
        y += (oldY - y) * fadeWeight;
        fadeWeight = std::max(0.0f, fadeWeight - fadeStep);
    }

    buffer[(size_t) writePos] = x + feedbackGain * y;
    writePos = (writePos + 1) % bufSize;
    return y;
}

void ShieldsFDNEngine::Biquad::setLowShelf(float freqHz, float gainDb, double sampleRateHzIn)
{
    // Standard RBJ Audio EQ Cookbook low-shelf, shelf slope S=1 (the "as steep as possible without
    // overshoot" case - there's no reason to want resonance/overshoot for a broadband tonal-balance
    // correction like this one).
    const auto A = std::pow(10.0f, gainDb / 40.0f);
    const auto w0 = 2.0f * pi * freqHz / (float) sampleRateHzIn;
    const auto cosw0 = std::cos(w0);
    const auto sinw0 = std::sin(w0);
    constexpr float shelfSlope = 1.0f;
    const auto alpha = sinw0 * 0.5f * std::sqrt((A + 1.0f / A) * (1.0f / shelfSlope - 1.0f) + 2.0f);
    const auto twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

    const auto rawB0 =        A * ((A + 1.0f) - (A - 1.0f) * cosw0 + twoSqrtAAlpha);
    const auto rawB1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
    const auto rawB2 =        A * ((A + 1.0f) - (A - 1.0f) * cosw0 - twoSqrtAAlpha);
    const auto rawA0 =            (A + 1.0f) + (A - 1.0f) * cosw0 + twoSqrtAAlpha;
    const auto rawA1 =    -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
    const auto rawA2 =            (A + 1.0f) + (A - 1.0f) * cosw0 - twoSqrtAAlpha;

    b0 = rawB0 / rawA0;
    b1 = rawB1 / rawA0;
    b2 = rawB2 / rawA0;
    a1 = rawA1 / rawA0;
    a2 = rawA2 / rawA0;
}

void ShieldsFDNEngine::Biquad::setHighShelf(float freqHz, float gainDb, double sampleRateHzIn)
{
    // RBJ Audio EQ Cookbook high-shelf, same S=1 slope rationale as setLowShelf().
    const auto A = std::pow(10.0f, gainDb / 40.0f);
    const auto w0 = 2.0f * pi * freqHz / (float) sampleRateHzIn;
    const auto cosw0 = std::cos(w0);
    const auto sinw0 = std::sin(w0);
    constexpr float shelfSlope = 1.0f;
    const auto alpha = sinw0 * 0.5f * std::sqrt((A + 1.0f / A) * (1.0f / shelfSlope - 1.0f) + 2.0f);
    const auto twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

    const auto rawB0 =         A * ((A + 1.0f) + (A - 1.0f) * cosw0 + twoSqrtAAlpha);
    const auto rawB1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
    const auto rawB2 =         A * ((A + 1.0f) + (A - 1.0f) * cosw0 - twoSqrtAAlpha);
    const auto rawA0 =             (A + 1.0f) - (A - 1.0f) * cosw0 + twoSqrtAAlpha;
    const auto rawA1 =     2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
    const auto rawA2 =             (A + 1.0f) - (A - 1.0f) * cosw0 - twoSqrtAAlpha;

    b0 = rawB0 / rawA0;
    b1 = rawB1 / rawA0;
    b2 = rawB2 / rawA0;
    a1 = rawA1 / rawA0;
    a2 = rawA2 / rawA0;
}

void ShieldsFDNEngine::Biquad::setPeak(float freqHz, float gainDb, float q, double sampleRateHzIn)
{
    // RBJ Audio EQ Cookbook peaking EQ (bell).
    const auto A = std::pow(10.0f, gainDb / 40.0f);
    const auto w0 = 2.0f * pi * freqHz / (float) sampleRateHzIn;
    const auto cosw0 = std::cos(w0);
    const auto alpha = std::sin(w0) / (2.0f * q);

    const auto rawB0 = 1.0f + alpha * A;
    const auto rawB1 = -2.0f * cosw0;
    const auto rawB2 = 1.0f - alpha * A;
    const auto rawA0 = 1.0f + alpha / A;
    const auto rawA1 = -2.0f * cosw0;
    const auto rawA2 = 1.0f - alpha / A;

    b0 = rawB0 / rawA0;
    b1 = rawB1 / rawA0;
    b2 = rawB2 / rawA0;
    a1 = rawA1 / rawA0;
    a2 = rawA2 / rawA0;
}

void ShieldsFDNEngine::Biquad::reset()
{
    x1 = x2 = y1 = y2 = 0.0f;
}

float ShieldsFDNEngine::Biquad::processSample(float x)
{
    const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
}

void ShieldsFDNEngine::prepare(double sampleRate)
{
    sampleRateHz = sampleRate;
    lengthChangeFadeStep = 1.0f / (float) std::max(1, msToSamples(lengthChangeFadeMs, sampleRateHz));

    // One-pole time-constant smoother - see targetSizeMultiplier's comment for why this exists.
    // Standard exponential smoothing coefficient for reaching ~63% of a step in sizeSmoothingMs.
    sizeSmoothingCoeff = 1.0f - std::exp(-1.0f / (sizeSmoothingMs * 0.001f * (float) sampleRateHz));

    for (int i = 0; i < numLines; ++i)
    {
        const auto capacity = msToSamples(baseLineLengthsMs[(size_t) i] * maxSizeMultiplier, sampleRateHz);
        lineBuffers[(size_t) i].assign((size_t) capacity, 0.0f);
    }

    for (size_t i = 0; i < allpassL.size(); ++i)
    {
        const auto capacity = msToSamples(allpassDelaysMs[i], sampleRateHz);
        allpassL[i].prepare(capacity);
        allpassR[i].prepare(capacity);
    }

    for (size_t i = 0; i < burstL.size(); ++i)
    {
        const auto capacity = msToSamples(baseBurstLengthsMs[i] * maxSizeMultiplier, sampleRateHz);
        burstL[i].prepare(capacity);
        burstR[i].prepare(capacity);
        burstL[i].fadeStep = lengthChangeFadeStep;
        burstR[i].fadeStep = lengthChangeFadeStep;
    }

    updateLineLengths();
    updateBurstLines();
    setBandwidthHz(15000.0f);
    lowShelfL.setLowShelf(lowShelfFreqHz, lowShelfGainDb, sampleRateHz);
    lowShelfR.setLowShelf(lowShelfFreqHz, lowShelfGainDb, sampleRateHz);
    highShelfL.setHighShelf(highShelfFreqHz, highShelfGainDb, sampleRateHz);
    highShelfR.setHighShelf(highShelfFreqHz, highShelfGainDb, sampleRateHz);
    midPeakL.setPeak(midPeakFreqHz, midPeakGainDb, midPeakQ, sampleRateHz);
    midPeakR.setPeak(midPeakFreqHz, midPeakGainDb, midPeakQ, sampleRateHz);
    reset();
    prepared = true;
}

void ShieldsFDNEngine::reset()
{
    for (auto& buf : lineBuffers)
        std::fill(buf.begin(), buf.end(), 0.0f);

    writePos.fill(0);
    dampingState.fill(0.0f);
    fadeWeight.fill(0.0f);
    wobblePhase.fill(0.0f);

    for (auto& stage : allpassL) stage.reset();
    for (auto& stage : allpassR) stage.reset();

    for (auto& line : burstL) line.reset();
    for (auto& line : burstR) line.reset();

    bandwidthStateL.fill(0.0f);
    bandwidthStateR.fill(0.0f);

    lowShelfL.reset();
    lowShelfR.reset();
    highShelfL.reset();
    highShelfR.reset();
    midPeakL.reset();
    midPeakR.reset();
}

void ShieldsFDNEngine::setDiffusion(float diffusion)
{
    diffusionCoefficient = std::max(0.3f, std::min(0.7f, diffusion));

    for (auto& stage : allpassL) stage.coefficient = diffusionCoefficient;
    for (auto& stage : allpassR) stage.coefficient = diffusionCoefficient;
}

void ShieldsFDNEngine::setFeedback(float feedback01)
{
    // Ceiling held safely below 1.0: the Hadamard mix is orthogonal (energy-preserving), so this
    // scalar alone governs whether the network decays or sustains indefinitely.
    constexpr float maxFeedbackGain = 0.985f;
    feedbackGain = clamp01(feedback01) * maxFeedbackGain;
}

void ShieldsFDNEngine::setSize(float multiplier)
{
    // Just the target - see targetSizeMultiplier's comment. Actually applied per-sample in
    // processStereo(), which glides sizeMultiplier toward this and recomputes lengths as it moves.
    targetSizeMultiplier = std::max(0.25f, std::min(maxSizeMultiplier, multiplier));
}

void ShieldsFDNEngine::updateBurstLines()
{
    const auto attackTimeSamples = (float) msToSamples(baseAttackMs * sizeMultiplier, sampleRateHz);

    for (size_t i = 0; i < burstL.size(); ++i)
    {
        const auto capacity = (int) burstL[i].buffer.size();
        const auto wanted = msToSamples(baseBurstLengthsMs[i] * sizeMultiplier, sampleRateHz);
        const auto newLength = std::max(1, std::min(capacity, wanted));

        // g^(attackTimeSamples / D) = burstFloor  =>  g = burstFloor^(D / attackTimeSamples). Every
        // line reaches the same floor at the same wall-clock time despite having a different D, so
        // the overall attack duration is set by baseAttackMs*Size, not by any one line's own length
        // - EXCEPT clamped at maxBurstGain (see that constant's comment), which the shortest line
        // would otherwise blow past into audible-ringing territory.
        const auto g = std::min(maxBurstGain, std::pow(burstFloor, (float) newLength / attackTimeSamples));

        // Only start a new crossfade once the previous one has fully settled (fadeWeight == 0) -
        // see updateLineLengths()'s comment on the main tank's identical guard for why: retriggering
        // mid-fade was itself producing a discontinuity, which per-sample smoothing (now changing
        // the target far more often than the fade's own duration) made far more frequent.
        if (newLength != burstL[i].delaySamples)
        {
            if (burstL[i].fadeWeight <= 0.0f)
            {
                // Crossfade into the new tap rather than clearing - see lengthChangeFadeMs's comment.
                burstL[i].fadeFromDelay = burstL[i].delaySamples;
                burstR[i].fadeFromDelay = burstR[i].delaySamples;
                burstL[i].fadeWeight = 1.0f;
                burstR[i].fadeWeight = 1.0f;
                burstL[i].delaySamples = newLength;
                burstR[i].delaySamples = newLength;
            }
            else
            {
                // Blocked by the in-flight fade - retry next sample (see lengthUpdateDeferred).
                lengthUpdateDeferred = true;
            }
        }

        burstL[i].feedbackGain = g;
        burstR[i].feedbackGain = g;
    }
}

void ShieldsFDNEngine::updateLineLengths()
{
    for (int i = 0; i < numLines; ++i)
    {
        const auto capacity = (int) lineBuffers[(size_t) i].size();
        const auto wanted = msToSamples(baseLineLengthsMs[(size_t) i] * sizeMultiplier, sampleRateHz);
        const auto newLength = std::max(1, std::min(capacity, wanted));

        // The fadeWeight <= 0 guard matters once sizeMultiplier is smoothed continuously (see
        // targetSizeMultiplier's comment): without it, a still-in-progress fade gets discarded and
        // restarted from scratch every time the smoothed value crosses another integer-sample
        // boundary - which, under continuous smoothing, can happen well before the previous fade's
        // lengthChangeFadeMs has elapsed. Restarting mid-fade snaps straight to 100% of whatever the
        // OLD fade's primary tap was, abruptly dropping that fade's own in-progress blend - a real
        // discontinuity, and a more frequent one than the clicks this mechanism was built to fix.
        // Waiting for the current fade to finish first means every fade always runs uninterrupted.
        if (newLength != delaySamples[(size_t) i])
        {
            if (fadeWeight[(size_t) i] <= 0.0f)
            {
                // Crossfade into the new tap rather than clearing - see lengthChangeFadeMs's comment
                // (this used to clear the buffer and reset writePos on every length change, which was
                // itself an audible click: the buffer already holds a continuous, valid rolling history
                // at every offset up to its capacity, so there was never any "stale" data to protect
                // against, only a hard jump to silence introduced right where the tail was still live).
                fadeFromDelay[(size_t) i] = delaySamples[(size_t) i];
                fadeWeight[(size_t) i] = 1.0f;
                delaySamples[(size_t) i] = newLength;
            }
            else
            {
                // Blocked by the in-flight fade - retry next sample (see lengthUpdateDeferred).
                lengthUpdateDeferred = true;
            }
        }
    }
}

void ShieldsFDNEngine::setDamping(float damping01)
{
    // Capped just below 1.0 rather than clamp01()'s full range. The damping filter is
    //   state += (1 - dampingCoefficient) * (lineOut - state)
    // so at exactly 1.0 the update term is multiplied by 0: the state stops tracking its input
    // entirely and stays frozen at whatever it last held - which, since reset() zeroes it and
    // nothing else can ever write it, is 0. Every line therefore feeds silence back into the tank,
    // and the reverb dies completely once the initial burst has passed: a 10s render at Treble
    // Decay = 100% measured RMS 1.5e-03 over the first 2s and then EXACTLY 0.0 for the remaining
    // 8s. That made the top of the knob's own range a kill switch rather than a very dark setting.
    // Capping here restores a heavily- but finitely-damped tail (same render now decays smoothly,
    // 4.1e-03 -> 2.4e-04 across the 10s).
    constexpr float maxDamping = 0.9995f;
    dampingCoefficient = std::min(maxDamping, clamp01(damping01));
}

void ShieldsFDNEngine::setBandwidthHz(float hz)
{
    const auto clampedHz = std::max(200.0f, std::min((float) (sampleRateHz * 0.45), hz));

    // Each of the numBandwidthStages cascaded stages uses this SAME coefficient (computed directly
    // from the requested Hz, no compensation for the cascade's own -3dB shift): cascading N
    // identical one-poles pulls the cascade's overall -3dB point below any individual stage's own
    // -3dB point, which is exactly what's wanted here (a first attempt at compensating for that
    // shift pushed the per-stage cutoff for a typical ~19kHz Bandwidth setting past Nyquist,
    // collapsing the coefficient toward zero - effectively no filtering at all, confirmed against
    // the reference IRs' spectral-difference plot making the high end WORSE, not better).
    bandwidthCoefficient = std::exp(-2.0f * pi * clampedHz / (float) sampleRateHz);
}

void ShieldsFDNEngine::setBitDepth(float bits)
{
    const auto clampedBits = std::max(4.0f, std::min(16.0f, bits));
    bitDepthLevels = std::pow(2.0f, clampedBits - 1.0f);
}

void ShieldsFDNEngine::setWobble(float wobbleAmount01)
{
    wobbleAmount = clamp01(wobbleAmount01);
}

void ShieldsFDNEngine::processStereo(float* left, float* right, int numSamples)
{
    constexpr float hadamardNorm = 0.353553390593f; // 1/sqrt(8)
    constexpr float outputTapGain = 0.5f;           // 1/sqrt(4): four taps summed per channel

    // See `prepared` - without this, an unprepared call divides by a zero-length buffer (SIGFPE).
    if (! prepared)
        return;

    for (int n = 0; n < numSamples; ++n)
    {
        // Glide sizeMultiplier toward its target and re-derive lengths - see targetSizeMultiplier's
        // comment. An earlier version of this comment claimed both calls were "cheap on the vast
        // majority of samples where nothing has moved"; that was wrong. updateBurstLines() runs six
        // unconditional powf() calls (one per burst line) on EVERY call regardless of whether any
        // length changed, and updateLineLengths() rounds on every line - together ~37% of total DSP
        // time in a profiled render. Both are pure functions of sizeMultiplier, so they only need
        // re-running when it actually moves; see lastAppliedSizeMultiplier.
        sizeMultiplier += (targetSizeMultiplier - sizeMultiplier) * sizeSmoothingCoeff;
        if (std::abs(targetSizeMultiplier - sizeMultiplier) < sizeSettleEpsilon)
            sizeMultiplier = targetSizeMultiplier;

        // Written as a difference rather than `!=` because this file is deliberately JUCE-free (see
        // the header) so the usual JUCE_..._IGNORE_WARNINGS macros aren't available to suppress
        // -Wfloat-equal, and juce_recommended_warning_flags enables it. For finite values this is
        // exactly the same test: any change at all, even one ULP, means the derived lengths could
        // differ, so it is intentionally not a tolerance comparison. The snap-to-target above is
        // what guarantees the difference actually reaches zero and stays there.
        const auto sizeChanged = std::abs(sizeMultiplier - lastAppliedSizeMultiplier) > 0.0f;

        if (sizeChanged || lengthUpdateDeferred)
        {
            lengthUpdateDeferred = false;
            updateLineLengths();
            updateBurstLines();
            lastAppliedSizeMultiplier = sizeMultiplier;
        }

        auto diffusedL = left[n];
        for (auto& stage : allpassL)
            diffusedL = stage.processSample(diffusedL);

        auto diffusedR = right[n];
        for (auto& stage : allpassR)
            diffusedR = stage.processSample(diffusedR);

        // The burst comb bank turns each channel's (already smoothed) near-impulse into a
        // decorrelated train of repeats whose SUM'S windowed RMS genuinely rises for a while
        // before falling - see BurstCombLine's comment for why. This, not the main tank's own
        // (energy-preserving, provably front-loaded) cross-mix, is what actually produces Shields's
        // audible swell; the main tank below is responsible for the long decay tail only.
        constexpr float burstNorm = 1.0f / (float) numBurstLines;
        float burstOutL = 0.0f, burstOutR = 0.0f;
        for (size_t i = 0; i < burstL.size(); ++i)
        {
            burstOutL += burstL[i].processSample(diffusedL);
            burstOutR += burstR[i].processSample(diffusedR);
        }
        burstOutL *= burstNorm;
        burstOutR *= burstNorm;

        std::array<float, numLines> lineOut {};
        for (int i = 0; i < numLines; ++i)
        {
            auto& buf = lineBuffers[(size_t) i];
            const auto bufSize = (int) buf.size();

            // Wobble (see setWobble()) - phase always advances so turning it on mid-playback starts
            // from wherever it happens to be, not a reset; modSamples stays exactly 0.0 whenever
            // wobbleAmount is 0, and the branch below skips interpolation entirely in that case, so
            // Wobble=0 reads bit-identically to how this loop worked before Wobble existed at all.
            wobblePhase[(size_t) i] += wobbleRateHz[(size_t) i] * 2.0f * pi / (float) sampleRateHz;
            if (wobblePhase[(size_t) i] > 2.0f * pi)
                wobblePhase[(size_t) i] -= 2.0f * pi;

            // modSamples is only consumed inside the wobbleAmount > 0 branches below, and is
            // identically 0 when wobbleAmount is 0 - but the sinf() was previously evaluated
            // unconditionally, costing ~15% of total DSP time at the DEFAULT setting of Wobble=0
            // purely to multiply the result by zero. Phase still advances unconditionally (see the
            // comment above: enabling Wobble mid-playback must pick up wherever the phase is, not
            // reset it), so this is a pure cost removal with no behavioural change either way.
            float modSamples = 0.0f;
            if (wobbleAmount > 0.0f)
            {
                const auto wobbleDepthSamples = wobbleDepthMs * 0.001f * (float) sampleRateHz;
                modSamples = wobbleAmount * wobbleDepthSamples * std::sin(wobblePhase[(size_t) i]);
            }

            float y;
            if (wobbleAmount > 0.0f)
            {
                const auto capacity = (float) bufSize;
                const auto fracDelay = std::max(1.0f, std::min(capacity - 2.0f, (float) delaySamples[(size_t) i] + modSamples));
                y = readFractionalDelay(buf, writePos[(size_t) i], fracDelay);
            }
            else
            {
                const auto readPos = (writePos[(size_t) i] + bufSize - delaySamples[(size_t) i]) % bufSize;
                y = buf[(size_t) readPos];
            }

            // Mid-crossfade after a Size change (see lengthChangeFadeMs) - both taps read from the
            // same still-live buffer, so this blends two valid delayed signals, not fades to/from
            // silence.
            if (fadeWeight[(size_t) i] > 0.0f)
            {
                float oldY;
                if (wobbleAmount > 0.0f)
                {
                    const auto capacity = (float) bufSize;
                    const auto oldFracDelay = std::max(1.0f, std::min(capacity - 2.0f, (float) fadeFromDelay[(size_t) i] + modSamples));
                    oldY = readFractionalDelay(buf, writePos[(size_t) i], oldFracDelay);
                }
                else
                {
                    const auto oldReadPos = (writePos[(size_t) i] + bufSize - fadeFromDelay[(size_t) i]) % bufSize;
                    oldY = buf[(size_t) oldReadPos];
                }
                y += (oldY - y) * fadeWeight[(size_t) i];
                fadeWeight[(size_t) i] = std::max(0.0f, fadeWeight[(size_t) i] - lengthChangeFadeStep);
            }

            lineOut[(size_t) i] = y;
        }

        // Damping filter in each feedback path (leaky integrator: higher dampingCoefficient means
        // more high-frequency loss per round trip, i.e. faster high-frequency decay).
        std::array<float, numLines> damped {};
        for (int i = 0; i < numLines; ++i)
        {
            auto& state = dampingState[(size_t) i];
            state += (1.0f - dampingCoefficient) * (lineOut[(size_t) i] - state);
            damped[(size_t) i] = state;
        }

        std::array<float, numLines> mixed {};
        for (int i = 0; i < numLines; ++i)
        {
            float sum = 0.0f;
            for (int j = 0; j < numLines; ++j)
                sum += hadamard[(size_t) i][(size_t) j] * damped[(size_t) j];
            mixed[(size_t) i] = sum * hadamardNorm * feedbackGain;
        }

        for (int i = 0; i < numLines; ++i)
        {
            const auto injection = (i % 2 == 0) ? burstOutL : burstOutR;
            auto& buf = lineBuffers[(size_t) i];

            // Sanitise before the value re-enters the tank. A single non-finite sample (a NaN/Inf
            // arriving from the host, or from an upstream plugin misbehaving) would otherwise
            // recirculate through the Hadamard mix forever: every line feeds every other, so one NaN
            // poisons all eight within a sample and the instance stays silent-but-wedged until it is
            // destroyed and re-created. Substituting 0 costs one predictable-branch compare per line
            // and leaves well-formed audio bit-identical.
            const auto next = mixed[(size_t) i] + injection;
            buf[(size_t) writePos[(size_t) i]] = std::isfinite(next) ? next : 0.0f;
            writePos[(size_t) i] = (writePos[(size_t) i] + 1) % (int) buf.size();
        }

        float wetL = 0.0f, wetR = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            if (i % 2 == 0) wetL += lineOut[(size_t) i];
            else            wetR += lineOut[(size_t) i];
        }
        wetL *= outputTapGain;
        wetR *= outputTapGain;

        // Fixed shelving pair first (see the member comment), then the bandwidth-limiting cascade,
        // then quantization last - shape the tone before reducing its precision.
        wetL = lowShelfL.processSample(wetL);
        wetR = lowShelfR.processSample(wetR);
        wetL = highShelfL.processSample(wetL);
        wetR = highShelfR.processSample(wetR);
        wetL = midPeakL.processSample(wetL);
        wetR = midPeakR.processSample(wetR);

        for (auto& state : bandwidthStateL)
        {
            state += (1.0f - bandwidthCoefficient) * (wetL - state);
            wetL = state;
        }
        for (auto& state : bandwidthStateR)
        {
            state += (1.0f - bandwidthCoefficient) * (wetR - state);
            wetR = state;
        }

        const auto quantize = [levels = bitDepthLevels](float x)
        {
            return std::round(x * levels) / levels;
        };

        left[n] = quantize(wetL);
        right[n] = quantize(wetR);
    }
}
