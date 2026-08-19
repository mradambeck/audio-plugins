#include "BloomFDNEngine.h"

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
}

// Sylvester construction: H1 = [1]; H(2N) = [[H_N, H_N], [H_N, -H_N]]. Listed here fully expanded
// (not built at runtime) since it's a fixed 8x8 constant - normalisation by 1/sqrt(8) happens once
// per sample in processStereo() rather than being baked into these entries, so the entries stay
// exactly +-1 and easy to eyeball against the standard construction.
const std::array<std::array<float, BloomFDNEngine::numLines>, BloomFDNEngine::numLines> BloomFDNEngine::hadamard {{
    {{ 1,  1,  1,  1,  1,  1,  1,  1 }},
    {{ 1, -1,  1, -1,  1, -1,  1, -1 }},
    {{ 1,  1, -1, -1,  1,  1, -1, -1 }},
    {{ 1, -1, -1,  1,  1, -1, -1,  1 }},
    {{ 1,  1,  1,  1, -1, -1, -1, -1 }},
    {{ 1, -1,  1, -1, -1,  1, -1,  1 }},
    {{ 1,  1, -1, -1, -1, -1,  1,  1 }},
    {{ 1, -1, -1,  1, -1,  1,  1, -1 }},
}};

void BloomFDNEngine::AllpassStage::prepare(int maxDelaySamples)
{
    buffer.assign((size_t) std::max(maxDelaySamples, 1), 0.0f);
    delaySamples = (int) buffer.size();
    writePos = 0;
}

void BloomFDNEngine::AllpassStage::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float BloomFDNEngine::AllpassStage::processSample(float x)
{
    const auto readPos = (writePos + (int) buffer.size() - delaySamples) % (int) buffer.size();
    const auto delayed = buffer[(size_t) readPos];

    const auto y = -coefficient * x + delayed;
    buffer[(size_t) writePos] = x + coefficient * y;

    writePos = (writePos + 1) % (int) buffer.size();
    return y;
}

void BloomFDNEngine::BurstCombLine::prepare(int maxDelaySamples)
{
    buffer.assign((size_t) std::max(maxDelaySamples, 1), 0.0f);
    writePos = 0;
}

void BloomFDNEngine::BurstCombLine::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float BloomFDNEngine::BurstCombLine::processSample(float x)
{
    // Read-before-write, same ordering as the main tank's lines: the OUTPUT is whatever was
    // written delaySamples ago, not this sample's input. That's what makes y[0] == 0 for an
    // impulse into an empty buffer - the input only starts appearing in the output D samples
    // later, which is the entire point (an immediate, undiminished pass-through here would let
    // the full click straight through and defeat the burst stage's whole purpose).
    const auto readPos = (writePos + (int) buffer.size() - delaySamples) % (int) buffer.size();
    const auto y = buffer[(size_t) readPos];

    buffer[(size_t) writePos] = x + feedbackGain * y;
    writePos = (writePos + 1) % (int) buffer.size();
    return y;
}

void BloomFDNEngine::Biquad::setLowShelf(float freqHz, float gainDb, double sampleRateHzIn)
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

void BloomFDNEngine::Biquad::setHighShelf(float freqHz, float gainDb, double sampleRateHzIn)
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

void BloomFDNEngine::Biquad::setPeak(float freqHz, float gainDb, float q, double sampleRateHzIn)
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

void BloomFDNEngine::Biquad::reset()
{
    x1 = x2 = y1 = y2 = 0.0f;
}

float BloomFDNEngine::Biquad::processSample(float x)
{
    const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
}

void BloomFDNEngine::prepare(double sampleRate)
{
    sampleRateHz = sampleRate;

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
}

void BloomFDNEngine::reset()
{
    for (auto& buf : lineBuffers)
        std::fill(buf.begin(), buf.end(), 0.0f);

    writePos.fill(0);
    dampingState.fill(0.0f);

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

void BloomFDNEngine::setDiffusion(float diffusion)
{
    diffusionCoefficient = std::max(0.3f, std::min(0.7f, diffusion));

    for (auto& stage : allpassL) stage.coefficient = diffusionCoefficient;
    for (auto& stage : allpassR) stage.coefficient = diffusionCoefficient;
}

void BloomFDNEngine::setFeedback(float feedback01)
{
    // Ceiling held safely below 1.0: the Hadamard mix is orthogonal (energy-preserving), so this
    // scalar alone governs whether the network decays or sustains indefinitely.
    constexpr float maxFeedbackGain = 0.985f;
    feedbackGain = clamp01(feedback01) * maxFeedbackGain;
}

void BloomFDNEngine::setSize(float multiplier)
{
    const auto clamped = std::max(0.25f, std::min(maxSizeMultiplier, multiplier));
    if (std::abs(clamped - sizeMultiplier) < 1.0e-6f)
        return;

    sizeMultiplier = clamped;
    updateLineLengths();
    updateBurstLines();
}

void BloomFDNEngine::updateBurstLines()
{
    const auto attackTimeSamples = (float) msToSamples(baseAttackMs * sizeMultiplier, sampleRateHz);

    for (size_t i = 0; i < burstL.size(); ++i)
    {
        const auto capacity = (int) burstL[i].buffer.size();
        const auto wanted = msToSamples(baseBurstLengthsMs[i] * sizeMultiplier, sampleRateHz);
        const auto newLength = std::max(1, std::min(capacity, wanted));

        // g^(attackTimeSamples / D) = burstFloor  =>  g = burstFloor^(D / attackTimeSamples). Every
        // line reaches the same floor at the same wall-clock time despite having a different D, so
        // the overall attack duration is set by baseAttackMs*Size, not by any one line's own length.
        const auto g = std::pow(burstFloor, (float) newLength / attackTimeSamples);

        if (newLength != burstL[i].delaySamples)
        {
            std::fill(burstL[i].buffer.begin(), burstL[i].buffer.end(), 0.0f);
            std::fill(burstR[i].buffer.begin(), burstR[i].buffer.end(), 0.0f);
            burstL[i].writePos = 0;
            burstR[i].writePos = 0;
            burstL[i].delaySamples = newLength;
            burstR[i].delaySamples = newLength;
        }

        burstL[i].feedbackGain = g;
        burstR[i].feedbackGain = g;
    }
}

void BloomFDNEngine::updateLineLengths()
{
    for (int i = 0; i < numLines; ++i)
    {
        const auto capacity = (int) lineBuffers[(size_t) i].size();
        const auto wanted = msToSamples(baseLineLengthsMs[(size_t) i] * sizeMultiplier, sampleRateHz);
        const auto newLength = std::max(1, std::min(capacity, wanted));

        if (newLength != delaySamples[(size_t) i])
        {
            // A structural length change invalidates whatever stale audio sits at the newly
            // (in/ex)cluded offsets in this line's fixed-capacity buffer - clearing it avoids a
            // pop from reading old content at the wrong phase, at the cost of a brief silence in
            // that one line (inaudible against the other seven still ringing).
            std::fill(lineBuffers[(size_t) i].begin(), lineBuffers[(size_t) i].end(), 0.0f);
            writePos[(size_t) i] = 0;
            delaySamples[(size_t) i] = newLength;
        }
    }
}

void BloomFDNEngine::setDamping(float damping01)
{
    dampingCoefficient = clamp01(damping01);
}

void BloomFDNEngine::setBandwidthHz(float hz)
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

void BloomFDNEngine::setBitDepth(float bits)
{
    const auto clampedBits = std::max(4.0f, std::min(16.0f, bits));
    bitDepthLevels = std::pow(2.0f, clampedBits - 1.0f);
}

void BloomFDNEngine::processStereo(float* left, float* right, int numSamples)
{
    constexpr float hadamardNorm = 0.353553390593f; // 1/sqrt(8)
    constexpr float outputTapGain = 0.5f;           // 1/sqrt(4): four taps summed per channel

    for (int n = 0; n < numSamples; ++n)
    {
        auto diffusedL = left[n];
        for (auto& stage : allpassL)
            diffusedL = stage.processSample(diffusedL);

        auto diffusedR = right[n];
        for (auto& stage : allpassR)
            diffusedR = stage.processSample(diffusedR);

        // The burst comb bank turns each channel's (already smoothed) near-impulse into a
        // decorrelated train of repeats whose SUM'S windowed RMS genuinely rises for a while
        // before falling - see BurstCombLine's comment for why. This, not the main tank's own
        // (energy-preserving, provably front-loaded) cross-mix, is what actually produces Bloom's
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
            const auto len = delaySamples[(size_t) i];
            const auto readPos = (writePos[(size_t) i] + (int) buf.size() - len) % (int) buf.size();
            lineOut[(size_t) i] = buf[(size_t) readPos];
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
            buf[(size_t) writePos[(size_t) i]] = mixed[(size_t) i] + injection;
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
