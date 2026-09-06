#include "ConcreteCapturePass.h"

#include <array>
#include <cmath>

namespace
{
    // Same 4-point Catmull-Rom cubic interpolation as ConcretePitchEngine's own reference path
    // (duplicated rather than shared - see that file's own comment on being "moved here verbatim"
    // from an earlier version; this catalog's convention is a small, local copy over a shared
    // utility until a second use genuinely needs one). Indices clamp to [0, dataLength) at the
    // edges.
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
}

juce::AudioBuffer<float> ConcreteCapturePass::resample(const juce::AudioBuffer<float>& input, double ratio)
{
    const auto inLength = input.getNumSamples();
    const auto numChannels = input.getNumChannels();
    const auto outLength = juce::jmax(1, (int) std::llround((double) inLength / ratio));

    juce::AudioBuffer<float> output(numChannels, outLength);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* in = input.getReadPointer(ch);
        auto* out = output.getWritePointer(ch);
        for (int i = 0; i < outLength; ++i)
            out[i] = cubicInterpolate(in, inLength, (double) i * ratio);
    }
    return output;
}

void ConcreteCapturePass::applyDrive(juce::AudioBuffer<float>& buffer, double sourceSampleRate, double driveDb)
{
    if (driveDb <= 0.0)
        return; // exactly transparent at the default - no tanh, no filter, matching every other
                 // Phase 2/3 control's "no artifacts until asked for" convention.

    const auto gain = (float) std::pow(10.0, driveDb / 20.0);
    const auto normalization = 1.0f / std::tanh(gain); // keeps full-scale input mapped near full scale

    // Cutoff falls from ~20kHz (a barely-audible effect right above 0dB) toward ~800Hz as drive
    // increases toward +24dB - a simple, generic stand-in for "sampling hot rolled off the high
    // end on playback" (see the plan's Phase 4 notes on the MPC60). Not yet machine-specific -
    // Phase 7 gives each machine its own tuned curve. Two cascaded one-pole stages, not one - a
    // single stage's ~6dB/octave rolloff is too gentle to net out ahead of the tanh saturation's
    // OWN newly-generated high-frequency harmonic content on broadband material (confirmed
    // empirically: one stage at a gentler ~2.5kHz floor left driven bright material with MORE
    // energy above 5kHz than undriven, the opposite of the intended character - the same failure
    // mode, and the same fix, as ConcretePitchEngine's Mode C decimation filter).
    const auto cutoffHz = juce::jmap(juce::jlimit(0.0, 24.0, driveDb), 0.0, 24.0, 20000.0, 800.0);
    const auto alpha = (float) (1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * cutoffHz / sourceSampleRate));

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        std::array<float, 2> lowpassState { 0.0f, 0.0f };
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            auto x = std::tanh(data[i] * gain) * normalization;
            for (auto& stage : lowpassState)
            {
                stage += alpha * (x - stage);
                x = stage;
            }
            data[i] = x;
        }
    }
}

void ConcreteCapturePass::applyDoubleSmear(juce::AudioBuffer<float>& buffer, double sourceSampleRate,
                                             ConcreteFilterModel::Mode model, double cutoffHz, double resonance01)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        ConcreteFilterModel filter;
        filter.prepare(sourceSampleRate);
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = filter.processSample(model, data[i], (float) cutoffHz, (float) resonance01);
    }
}

ConcreteCapturePass::Result ConcreteCapturePass::apply(const juce::AudioBuffer<float>& sourceBuffer,
                                                          double sourceSampleRate, const Settings& settings)
{
    Result result;

    if (settings.bypass || sourceBuffer.getNumSamples() <= 0)
    {
        result.buffer = std::make_shared<juce::AudioBuffer<float>>(sourceBuffer);
        return result; // captureTransposeSemitones stays 0.0 - see the class comment on bypass
    }

    const auto iterations = juce::jlimit(1, 4, settings.iterations);
    const auto ratioPerIteration = std::pow(2.0, settings.transposeSemitones / 12.0);

    auto workingBuffer = std::make_shared<juce::AudioBuffer<float>>(sourceBuffer);
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        *workingBuffer = resample(*workingBuffer, ratioPerIteration);
        applyDrive(*workingBuffer, sourceSampleRate, settings.inputDriveDb);

        for (int ch = 0; ch < workingBuffer->getNumChannels(); ++ch)
        {
            auto* data = workingBuffer->getWritePointer(ch);
            for (int i = 0; i < workingBuffer->getNumSamples(); ++i)
                data[i] = ConcreteQuantizer::process(data[i], settings.quantizerMode, settings.bitDepthBits);
        }
    }

    // The one deliberate exception to "the capture pass doesn't touch filters" (Architecture #3) -
    // sits at the very END of the whole chain, once, after all iterations - see the class comment.
    if (settings.doubleSmear)
        applyDoubleSmear(*workingBuffer, sourceSampleRate, settings.doubleSmearFilterModel,
                          settings.doubleSmearCutoffHz, settings.doubleSmearResonance01);

    result.buffer = workingBuffer; // shared_ptr<AudioBuffer<float>> -> shared_ptr<const ...>, implicit
    result.captureTransposeSemitones = settings.transposeSemitones * (double) iterations;
    return result;
}
