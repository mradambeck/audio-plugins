#include "IRLibrary.h"

#include <algorithm>
#include <cmath>

namespace wildjag::conv
{

namespace
{
    // Extra zero samples appended to each channel before resampling. LagrangeInterpolator reads a
    // few samples ahead of the position it is producing, and resample() asks it for a few more
    // samples than it keeps (see the latency compensation there), so without this the final output
    // samples would read off the end of the source buffer.
    constexpr int resamplerPadding = 16;

    // Rates closer than this are treated as identical. Hosts report 44100.0 and 44099.999... for
    // the same clock, and a resample pass at a ratio of 1.0000001 is pure loss.
    constexpr double sampleRateEpsilon = 0.01;
}

IRLibrary::IRLibrary(const ConvolutionVariant& variantToUse)
    : variant(variantToUse)
{
    cache.resize(variant.irs.size());
}

void IRLibrary::setTargetSampleRate(double newSampleRate)
{
    if (std::abs(newSampleRate - targetSampleRate) < sampleRateEpsilon)
        return;

    targetSampleRate = newSampleRate;

    // Every cached buffer was resampled to the old rate, so all of them are now wrong.
    std::fill(cache.begin(), cache.end(), nullptr);
}

std::shared_ptr<const DecodedIR> IRLibrary::getDecodedIR(int index)
{
    if (! juce::isPositiveAndBelow(index, (int) variant.irs.size()) || targetSampleRate <= 0.0)
        return nullptr;

    if (auto cached = cache[(size_t) index])
        return cached;

    const auto& asset = variant.irs[(size_t) index];

    juce::AudioBuffer<float> decoded;
    double sourceSampleRate = 0.0;

    if (! decodeBlob(asset.data, asset.dataSize, decoded, sourceSampleRate))
        return nullptr;

    auto atSessionRate = resample(decoded, sourceSampleRate, targetSampleRate);

    if (atSessionRate.getNumSamples() <= 0)
        return nullptr;

    // After resampling, so the normalisation is computed from the samples that will actually be
    // convolved and a 44.1 kHz IR lands at the same level in a 96 kHz session.
    normaliseToUnitEnergy(atSessionRate);

    auto cached = std::make_shared<const DecodedIR>(DecodedIR { std::move(atSessionRate), sourceSampleRate });
    cache[(size_t) index] = cached;
    return cached;
}

void IRLibrary::normaliseToUnitEnergy(juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return;

    double energy = 0.0;
    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* samples = buffer.getReadPointer(channel);
        for (int i = 0; i < numSamples; ++i)
            energy += (double) samples[i] * (double) samples[i];
    }

    // Per-channel RMS energy rather than the raw sum, so a stereo IR is not made 3 dB quieter than
    // the mono version of the same capture.
    const auto perChannelEnergy = energy / (double) numChannels;

    if (perChannelEnergy <= 0.0)
        return;

    const auto gain = 1.0f / (float) std::sqrt(perChannelEnergy);

    // One gain for every channel, so the IR's own stereo balance survives.
    for (int channel = 0; channel < numChannels; ++channel)
        juce::FloatVectorOperations::multiply(buffer.getWritePointer(channel), gain, numSamples);
}

bool IRLibrary::decodeBlob(const void* data, size_t dataSize,
                           juce::AudioBuffer<float>& destination, double& sourceSampleRate)
{
    if (data == nullptr || dataSize == 0)
        return false;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    // Non-owning stream over the BinaryData bytes - they outlive the plugin, so there is nothing to
    // copy and nothing to free.
    auto stream = std::make_unique<juce::MemoryInputStream>(data, dataSize, false);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(stream)));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return false;

    // Convolution supports mono and stereo only (juce::dsp::Convolution asserts above two). A
    // variant shipping a 4-channel true-stereo IR would silently lose channels here, so take the
    // first two and let the caller's IR set be the thing that gets fixed.
    const auto numChannels = (int) std::min<unsigned int>(reader->numChannels, 2u);
    const auto numSamples = (int) std::min<juce::int64>(reader->lengthInSamples, std::numeric_limits<int>::max());

    destination.setSize(numChannels, numSamples);

    if (! reader->read(&destination, 0, numSamples, 0, true, numChannels > 1))
        return false;

    sourceSampleRate = reader->sampleRate;
    return sourceSampleRate > 0.0;
}

juce::AudioBuffer<float> IRLibrary::resample(const juce::AudioBuffer<float>& source,
                                             double sourceSampleRate, double targetSampleRate)
{
    const auto numChannels = source.getNumChannels();
    const auto sourceLength = source.getNumSamples();

    if (numChannels <= 0 || sourceLength <= 0 || sourceSampleRate <= 0.0 || targetSampleRate <= 0.0)
        return {};

    if (std::abs(sourceSampleRate - targetSampleRate) < sampleRateEpsilon)
    {
        juce::AudioBuffer<float> unchanged(numChannels, sourceLength);
        for (int channel = 0; channel < numChannels; ++channel)
            unchanged.copyFrom(channel, 0, source, channel, 0, sourceLength);
        return unchanged;
    }

    const auto ratio = sourceSampleRate / targetSampleRate;
    const auto targetLength = (int) std::floor((double) sourceLength / ratio);

    if (targetLength <= 0)
        return {};

    // LagrangeInterpolator is a 5-point interpolator and so has an inherent 2-sample group delay.
    // juce_Interpolators.cpp's own unit test states the relationship exactly: an event at input
    // position p emerges at output position (p + getBaseLatency()) / ratio. Left uncompensated,
    // every resampled IR would start a few samples later than the IR it was made from, so the same
    // room captured at 44.1 kHz and at 48 kHz would have different onset times in the same session.
    // Discarding the leading samples puts the onset back where it belongs.
    //
    // The compensation is a whole number of samples, so a sub-sample residual remains. At 48 kHz
    // that is under 20 microseconds of an IR's onset - well below anything a fractional-delay
    // filter would be worth introducing here.
    const auto latencyOffset = (int) std::lround((double) juce::LagrangeInterpolator::getBaseLatency() / ratio);

    juce::AudioBuffer<float> resampled(numChannels, targetLength);

    std::vector<float> padded((size_t) sourceLength + resamplerPadding, 0.0f);
    std::vector<float> produced((size_t) targetLength + (size_t) latencyOffset, 0.0f);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        std::copy_n(source.getReadPointer(channel), sourceLength, padded.begin());
        std::fill(padded.begin() + sourceLength, padded.end(), 0.0f);

        juce::LagrangeInterpolator interpolator;
        interpolator.reset();
        interpolator.process(ratio, padded.data(), produced.data(), (int) produced.size());

        std::copy_n(produced.begin() + latencyOffset, targetLength, resampled.getWritePointer(channel));
    }

    return resampled;
}

} // namespace wildjag::conv
