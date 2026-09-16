#include "ConvolutionEngine.h"

#include <algorithm>
#include <cmath>

namespace wildjag::conv
{

namespace
{
    // Head size for the non-uniform partitioned algorithm. juce_Convolution.h recommends 256 or
    // greater for reverberation IRs; the partitioning is what keeps a multi-second IR affordable
    // without buying that back as reported latency.
    constexpr int convolutionHeadSize = 256;

    // Ramp times. Pre-delay is the slowest because moving a read head is audible as pitch drift, so
    // a longer ramp trades a brief flutter for a smaller one. Bypass is the fastest that is still
    // click-free, since a bypass that audibly lags the button feels broken.
    constexpr double preDelayRampSeconds = 0.05;
    constexpr double gainRampSeconds = 0.02;
    constexpr double bypassRampSeconds = 0.02;

    // Spare samples on each delay line beyond the maximum pre-delay: CircularDelayBuffer's
    // interpolated read needs the delay to stay within [0, capacity - 2], and the smoothed value can
    // sit fractionally above the maximum mid-ramp.
    constexpr int preDelayGuardSamples = 4;
}

ConvolutionEngine::ConvolutionEngine()
    : convolution(juce::dsp::Convolution::NonUniform { convolutionHeadSize })
{
}

void ConvolutionEngine::prepare(double sampleRate, int maxBlockSizeToUse, int numChannelsToUse,
                                juce::AudioBuffer<float>&& initialIR)
{
    jassert(numChannelsToUse == 1 || numChannelsToUse == 2); // juce::dsp::Convolution supports no more

    // Set before the loadIR() below: loadIR() stamps this as the buffer's own rate, and a stale
    // value there would have the convolution resample an IR that needs no resampling.
    currentSampleRate = sampleRate;
    maxBlockSize = std::max(maxBlockSizeToUse, 1);
    numChannels = juce::jlimit(1, 2, numChannelsToUse);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) numChannels };

    if (initialIR.getNumSamples() > 0)
        loadIR(std::move(initialIR));

    convolution.prepare(spec);

    lowCutFilter.prepare(spec);
    lowCutFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highCutFilter.prepare(spec);
    highCutFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    const auto maxPreDelaySamples = (int) std::ceil(maxPreDelayMs * 0.001 * sampleRate);
    preDelayLines.resize((size_t) numChannels);
    for (auto& line : preDelayLines)
        line.setSize(maxPreDelaySamples + preDelayGuardSamples);

    preDelaySamples.reset(sampleRate, preDelayRampSeconds);
    dryGain.reset(sampleRate, gainRampSeconds);
    wetGain.reset(sampleRate, gainRampSeconds);
    bypassAmount.reset(sampleRate, bypassRampSeconds);

    wetBuffer.setSize(numChannels, maxBlockSize);
    wetBuffer.clear();

    prepared = true;
}

void ConvolutionEngine::reset()
{
    convolution.reset();
    lowCutFilter.reset();
    highCutFilter.reset();

    for (auto& line : preDelayLines)
        line.reset();

    preDelaySamples.setCurrentAndTargetValue(preDelaySamples.getTargetValue());
    dryGain.setCurrentAndTargetValue(dryGain.getTargetValue());
    wetGain.setCurrentAndTargetValue(wetGain.getTargetValue());
    bypassAmount.setCurrentAndTargetValue(bypassAmount.getTargetValue());

    wetBuffer.clear();
}

void ConvolutionEngine::loadIR(juce::AudioBuffer<float>&& shapedAtSessionRate)
{
    if (shapedAtSessionRate.getNumSamples() <= 0 || shapedAtSessionRate.getNumChannels() <= 0)
        return;

    // Read before the move: a moved-from buffer reports nothing useful.
    const auto isStereo = shapedAtSessionRate.getNumChannels() > 1;

    // Normalise::no and Trim::no are both deliberate. Normalising would make Length and Attack
    // change the output level, which is the opposite of what those controls are for; trimming would
    // silently discard the leading silence that is part of some IRs' character (and that Pre-Delay
    // is there to add to, not to replace).
    convolution.loadImpulseResponse(std::move(shapedAtSessionRate),
                                    currentSampleRate,
                                    isStereo ? juce::dsp::Convolution::Stereo::yes
                                             : juce::dsp::Convolution::Stereo::no,
                                    juce::dsp::Convolution::Trim::no,
                                    juce::dsp::Convolution::Normalise::no);
}

void ConvolutionEngine::setPreDelayMs(float ms) noexcept
{
    const auto clamped = juce::jlimit(0.0f, maxPreDelayMs, ms);
    preDelaySamples.setTargetValue((float) (clamped * 0.001 * currentSampleRate));
}

void ConvolutionEngine::setLowCutHz(float hz) noexcept { lowCutTargetHz = hz; }
void ConvolutionEngine::setHighCutHz(float hz) noexcept { highCutTargetHz = hz; }
void ConvolutionEngine::setDryGain(float linearGain) noexcept { dryGain.setTargetValue(linearGain); }
void ConvolutionEngine::setWetGain(float linearGain) noexcept { wetGain.setTargetValue(linearGain); }
void ConvolutionEngine::setBypassed(bool shouldBeBypassed) noexcept
{
    bypassAmount.setTargetValue(shouldBeBypassed ? 1.0f : 0.0f);
}

void ConvolutionEngine::process(juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (! prepared)
        return;

    const auto channelsToProcess = std::min(buffer.getNumChannels(), numChannels);
    const auto n = std::min(numSamples, maxBlockSize);

    if (channelsToProcess <= 0 || n <= 0)
        return;

    // 1. Pre-delay, into the wet scratch. Done before the convolution because pre-delay is the gap
    //    between the dry sound and the onset of the reverb, not a delay applied to the reverb's
    //    output - the difference shows up as soon as the IR has any pre-ringing.
    //    The smoothed delay advances once per sample and is shared across channels, so the stereo
    //    image cannot shear while the knob moves.
    for (int i = 0; i < n; ++i)
    {
        const auto delay = preDelaySamples.getNextValue();

        for (int channel = 0; channel < channelsToProcess; ++channel)
        {
            auto& line = preDelayLines[(size_t) channel];
            line.write(buffer.getSample(channel, i));
            wetBuffer.setSample(channel, i, line.readInterpolated(delay));
        }
    }

    // 2. Convolution, in place on the wet scratch.
    {
        juce::dsp::AudioBlock<float> block(wetBuffer.getArrayOfWritePointers(),
                                           (size_t) channelsToProcess, 0, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> context(block);
        convolution.process(context);

        // 3. High/low cut on the wet path only. Each filter is skipped outright at its neutral
        //    extreme; entering from that state resets it first, so it starts from silence rather
        //    than from however long-ago state it last held.
        const auto wantLowCut = lowCutTargetHz > lowCutNeutralHz + 0.5f;
        if (wantLowCut)
        {
            if (! lowCutActive)
            {
                lowCutFilter.reset();
                lowCutActive = true;
            }

            lowCutFilter.setCutoffFrequency(lowCutTargetHz);
            lowCutFilter.process(context);
        }
        else
        {
            lowCutActive = false;
        }

        const auto wantHighCut = highCutTargetHz < highCutNeutralHz - 0.5f;
        if (wantHighCut)
        {
            if (! highCutActive)
            {
                highCutFilter.reset();
                highCutActive = true;
            }

            highCutFilter.setCutoffFrequency(highCutTargetHz);
            highCutFilter.process(context);
        }
        else
        {
            highCutActive = false;
        }
    }

    // 4. Mix. The bypass ramp interpolates the dry coefficient towards unity and the wet coefficient
    //    towards zero rather than early-returning, which is what keeps a multi-second tail decaying
    //    through the transition instead of being cut off mid-flight. The convolution above keeps
    //    running while bypassed for the same reason: un-bypassing then resumes a tail that is
    //    already where it should be.
    for (int i = 0; i < n; ++i)
    {
        const auto bypass = bypassAmount.getNextValue();
        const auto dry = dryGain.getNextValue();
        const auto wet = wetGain.getNextValue();

        const auto dryCoefficient = dry + (1.0f - dry) * bypass;
        const auto wetCoefficient = wet * (1.0f - bypass);

        for (int channel = 0; channel < channelsToProcess; ++channel)
        {
            const auto drySample = buffer.getSample(channel, i);
            const auto wetSample = wetBuffer.getSample(channel, i);
            buffer.setSample(channel, i, drySample * dryCoefficient + wetSample * wetCoefficient);
        }
    }
}

} // namespace wildjag::conv
