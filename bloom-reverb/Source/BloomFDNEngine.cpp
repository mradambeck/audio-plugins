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

    updateLineLengths();
    setBandwidthHz(15000.0f);
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

    bandwidthStateL = 0.0f;
    bandwidthStateR = 0.0f;
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
            const auto injection = (i % 2 == 0) ? diffusedL : diffusedR;
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

        bandwidthStateL += (1.0f - bandwidthCoefficient) * (wetL - bandwidthStateL);
        bandwidthStateR += (1.0f - bandwidthCoefficient) * (wetR - bandwidthStateR);

        const auto quantize = [levels = bitDepthLevels](float x)
        {
            return std::round(x * levels) / levels;
        };

        left[n] = quantize(bandwidthStateL);
        right[n] = quantize(bandwidthStateR);
    }
}
