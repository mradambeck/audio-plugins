#include "AuraFDNEngine.h"

#include <algorithm>
#include <cmath>

// H16 = H2 (kron) H8, Sylvester construction, +-1 entries (normalised by 1/sqrt(16) at use time in
// processStereo()) - orthogonal/energy-preserving, so stability stays governed purely by the
// feedback gain and damping rather than by the matrix. Same matrix IntruderFDNEngine uses.
const std::array<std::array<float, AuraFDNEngine::numLines>, AuraFDNEngine::numLines>
    AuraFDNEngine::hadamard { {
        { 1, 1, 1, 1, 1, 1, 1, 1 },
        { 1, -1, 1, -1, 1, -1, 1, -1 },
        { 1, 1, -1, -1, 1, 1, -1, -1 },
        { 1, -1, -1, 1, 1, -1, -1, 1 },
        { 1, 1, 1, 1, -1, -1, -1, -1 },
        { 1, -1, 1, -1, -1, 1, -1, 1 },
        { 1, 1, -1, -1, -1, -1, 1, 1 },
        { 1, -1, -1, 1, -1, 1, 1, -1 },
    } };

void AuraFDNEngine::prepare(double sampleRate)
{
    sampleRateHz = sampleRate;

    for (int i = 0; i < numLines; ++i)
    {
        const auto maxDelaySamples = (int) std::ceil(baseLineLengthsMs[(size_t) i] * 0.001 * sampleRate) + 4;
        lineBuffers[(size_t) i].setSize(maxDelaySamples);
        feedbackShelf[(size_t) i].setPivotHz(shelfPivotHz, sampleRate);
        subBassShelf[(size_t) i].setPivotHz(subBassShelfPivotHz, sampleRate);
    }

    maxPreDelaySamples = (int) std::ceil(0.2 * sampleRate) + 4; // 200ms max pre-delay headroom
    preDelayBufferL.setSize(maxPreDelaySamples);
    preDelayBufferR.setSize(maxPreDelaySamples);

    inputTiltL.setPivotHz(inputTiltPivotHz, sampleRate);
    inputTiltR.setPivotHz(inputTiltPivotHz, sampleRate);

    setInputHighPassHz(defaultInputHighPassHz);

    prepared = true;

    updateLineLengths();
    reset();
}

void AuraFDNEngine::reset()
{
    for (auto& buf : lineBuffers) buf.reset();
    for (auto& f : dampingFilter) f.reset();
    for (auto& s : feedbackShelf) s.reset();
    for (auto& s : subBassShelf) s.reset();
    lowCutL.reset();
    lowCutR.reset();
    preDelayBufferL.reset();
    preDelayBufferR.reset();
    inputTiltL.reset();
    inputTiltR.reset();
    inputHighPassL.reset();
    inputHighPassR.reset();
}

void AuraFDNEngine::setPreDelayMs(float ms)
{
    const auto clampedMs = std::max(0.0f, ms);
    preDelaySamples = std::min((int) std::round(clampedMs * 0.001f * (float) sampleRateHz), maxPreDelaySamples - 1);
}

void AuraFDNEngine::setBandGains(float highBandGain, float lowBandGain)
{
    const auto clampedHigh = std::clamp(highBandGain, 0.0f, maxFeedbackGain);
    const auto clampedLow = std::clamp(lowBandGain, 0.0f, maxFeedbackGain);
    for (auto& s : feedbackShelf)
    {
        s.highGain = clampedHigh;
        s.lowGain = clampedLow;
    }
}

void AuraFDNEngine::setSubBassGain(float gain)
{
    const auto clamped = std::clamp(gain, 0.0f, 1.0f);
    for (auto& s : subBassShelf)
    {
        s.lowGain = clamped;
        s.highGain = 1.0f; // only the sub-bass side ever gets attenuated - see the header comment
    }
}

void AuraFDNEngine::setDampingWeight(float weight)
{
    const auto clamped = std::clamp(weight, 0.0f, maxDampingWeight);
    for (auto& f : dampingFilter)
        f.setWeight(clamped);
}

void AuraFDNEngine::setInputTilt(float tiltDb)
{
    inputTiltL.setTiltDb(tiltDb);
    inputTiltR.setTiltDb(tiltDb);
}

void AuraFDNEngine::setInputHighPassHz(float hz)
{
    inputHighPassL.setCutoffHz(std::max(1.0f, hz), sampleRateHz);
    inputHighPassR.setCutoffHz(std::max(1.0f, hz), sampleRateHz);
}

void AuraFDNEngine::setLowCutHz(float hz)
{
    const auto clamped = std::clamp(hz, 0.0f, 300.0f);
    lowCutActive = clamped > 0.0f; // see the header's lowCutActive comment on why this matters
    if (lowCutActive)
    {
        lowCutL.setCutoffHz(clamped, sampleRateHz);
        lowCutR.setCutoffHz(clamped, sampleRateHz);
    }
}

void AuraFDNEngine::setBitDepth(float bits)
{
    const auto clamped = std::clamp(bits, 8.0f, 24.0f);
    bitDepthActive = clamped < 24.0f; // see the header's bitDepthActive comment on why this matters
    bitDepthLevels = std::pow(2.0f, clamped - 1.0f);
}

void AuraFDNEngine::updateLineLengths()
{
    for (int i = 0; i < numLines; ++i)
        lineDelaySamples[(size_t) i] = std::max(1, (int) std::round(baseLineLengthsMs[(size_t) i] * 0.001 * sampleRateHz));
}

void AuraFDNEngine::processStereo(float* left, float* right, int numSamples)
{
    if (!prepared)
        return;

    for (int n = 0; n < numSamples; ++n)
    {
        auto dryL = left[n];
        auto dryR = right[n];

        // --- Player Low Cut (see setLowCutHz()'s comment) - first, before pre-delay and
        // everything else, so it trims content before it ever reaches the effect. Skipped
        // entirely when inactive (0Hz, the default) rather than run with a near-zero coefficient,
        // guaranteeing a bit-identical bypass.
        if (lowCutActive)
        {
            dryL = dryL - lowCutL.processSample(dryL);
            dryR = dryR - lowCutR.processSample(dryR);
        }

        // --- Pre-delay
        preDelayBufferL.write(dryL);
        preDelayBufferR.write(dryR);
        const auto preL = preDelayBufferL.read(preDelaySamples);
        const auto preR = preDelayBufferR.read(preDelaySamples);

        // --- Input tilt (High's onset-tone effect), applied here so it colors EVERYTHING
        // downstream including the very first tank arrivals - see this file's header comment on
        // why feedbackShelf alone can't do this.
        // First-order input high-pass (x - lowpass(x)) modelling the real unit's own I/O rolloff -
        // see inputHighPassL's comment in the header for the measurement this reproduces.
        const auto hpL = preL - inputHighPassL.processSample(preL);
        const auto hpR = preR - inputHighPassR.processSample(preR);

        const auto tiltedL = inputTiltL.processSample(hpL);
        const auto tiltedR = inputTiltR.processSample(hpR);

        // --- FDN tank: read each line, apply per-line HF damping, Hadamard-mix, apply the
        // per-line low/high-band feedback shelf, inject new input, write back. Matches
        // AmbienceFDN.forward()'s signal flow exactly - see this file's header comment. Even
        // lines seeded from L, odd from R (same convention as Shields/Intruder).
        std::array<float, numLines> lineOut {};
        for (int i = 0; i < numLines; ++i)
            lineOut[(size_t) i] = lineBuffers[(size_t) i].read(lineDelaySamples[(size_t) i] - 1);

        std::array<float, numLines> damped {};
        for (int i = 0; i < numLines; ++i)
            damped[(size_t) i] = dampingFilter[(size_t) i].processSample(lineOut[(size_t) i]);

        std::array<float, numLines> mixed {};
        constexpr float hadamardNorm = 0.353553390593f; // 1/sqrt(8)
        for (int i = 0; i < numLines; ++i)
        {
            float sum = 0.0f;
            for (int j = 0; j < numLines; ++j)
                sum += hadamard[(size_t) i][(size_t) j] * damped[(size_t) j];
            mixed[(size_t) i] = sum * hadamardNorm;
        }

        for (int i = 0; i < numLines; ++i)
        {
            const auto shelved = feedbackShelf[(size_t) i].processSample(mixed[(size_t) i]);
            // Extra per-lap sub-bass brake, on top of feedbackShelf's own broader tilt - see
            // setSubBassGain()'s comment. Applied here (in-loop, after the main shelf) so it
            // actually shortens ring time, not just level, matching this file's own KNOWN LIMIT
            // comment on why inputHighPassL (outside the loop) structurally cannot.
            const auto subBassShaped = subBassShelf[(size_t) i].processSample(shelved);
            const auto injected = (i % 2 == 0) ? tiltedL : tiltedR;
            const auto lineIn = subBassShaped + injected;
            // NaN/Inf guard at the recirculation point - same convention as ShieldsFDNEngine/
            // IntruderFDNEngine: a poisoned sample must not be allowed to circulate forever.
            lineBuffers[(size_t) i].write(std::isfinite(lineIn) ? lineIn : 0.0f);
        }

        float tankL = 0.0f, tankR = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            if (i % 2 == 0) tankL += lineOut[(size_t) i];
            else tankR += lineOut[(size_t) i];
        }
        constexpr float tankNorm = 0.5f; // 1/sqrt(4) - 4 lines summed per channel
        auto outL = tankL * tankNorm;
        auto outR = tankR * tankNorm;

        // --- Output-stage quantization (Bit Depth) - see setBitDepth()'s and bitDepthActive's
        // comments for why this is a genuine bypass at 24, not just a near-transparent setting.
        if (bitDepthActive)
        {
            outL = std::round(outL * bitDepthLevels) / bitDepthLevels;
            outR = std::round(outR * bitDepthLevels) / bitDepthLevels;
        }

        left[n] = outL;
        right[n] = outR;
    }
}
