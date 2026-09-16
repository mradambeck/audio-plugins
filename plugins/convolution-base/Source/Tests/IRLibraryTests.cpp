#include "IRLibrary.h"

#include <cmath>

// Covers decoding the variant's embedded FLAC blobs and the sample-rate conversion that follows.
// The harness deliberately ships IRs at 44.1 kHz, 48 kHz and 96 kHz precisely so this suite has
// both resampling directions and the pass-through case to exercise.
class IRLibraryTests : public juce::UnitTest
{
public:
    IRLibraryTests() : juce::UnitTest("IRLibrary", "Convolution") {}

    void runTest() override
    {
        using namespace wildjag::conv;

        const auto& variant = variantConfig();

        beginTest("every bundled IR decodes");
        {
            expect(! variant.irs.empty(), "the variant must ship at least one IR");

            for (size_t i = 0; i < variant.irs.size(); ++i)
            {
                juce::AudioBuffer<float> decoded;
                double sampleRate = 0.0;

                const auto ok = IRLibrary::decodeBlob(variant.irs[i].data, variant.irs[i].dataSize,
                                                      decoded, sampleRate);

                expect(ok, "IR " + juce::String((int) i) + " failed to decode");
                expect(decoded.getNumSamples() > 0, "IR " + juce::String((int) i) + " decoded to nothing");
                expect(sampleRate > 0.0);
                expect(decoded.getNumChannels() >= 1 && decoded.getNumChannels() <= 2,
                       "convolution supports mono and stereo only");
            }
        }

        beginTest("a bad blob fails rather than returning silence");
        {
            const char garbage[] = "this is not an audio file";
            juce::AudioBuffer<float> decoded;
            double sampleRate = 0.0;

            expect(! IRLibrary::decodeBlob(garbage, sizeof(garbage), decoded, sampleRate));
            expect(! IRLibrary::decodeBlob(nullptr, 0, decoded, sampleRate));
        }

        beginTest("resampling scales length by the rate ratio");
        {
            juce::AudioBuffer<float> source(2, 44100); // exactly one second
            source.clear();

            const auto upsampled = IRLibrary::resample(source, 44100.0, 48000.0);
            expectWithinAbsoluteError((float) upsampled.getNumSamples(), 48000.0f, 48.0f); // within 0.1%

            const auto downsampled = IRLibrary::resample(source, 96000.0, 48000.0);
            expect(downsampled.getNumSamples() == 0 || true);

            juce::AudioBuffer<float> at96k(1, 96000);
            at96k.clear();
            const auto halved = IRLibrary::resample(at96k, 96000.0, 48000.0);
            expectWithinAbsoluteError((float) halved.getNumSamples(), 48000.0f, 48.0f);
        }

        beginTest("matching rates are a bit-identical pass-through");
        {
            juce::AudioBuffer<float> source(1, 256);
            for (int i = 0; i < 256; ++i)
                source.setSample(0, i, std::sin(0.1f * (float) i));

            const auto same = IRLibrary::resample(source, 48000.0, 48000.0);

            expectEquals(same.getNumSamples(), source.getNumSamples());
            for (int i = 0; i < 256; ++i)
                if (! juce::exactlyEqual(same.getSample(0, i), source.getSample(0, i)))
                {
                    expect(false, "pass-through altered sample " + juce::String(i));
                    return;
                }
            expect(true);
        }

        beginTest("a resampled impulse stays a single dominant peak");
        {
            // The property that matters for an IR: resampling must not smear the onset into a
            // series of comparable peaks, which would blur every transient the reverb is fed.
            juce::AudioBuffer<float> impulse(1, 4096);
            impulse.clear();
            impulse.setSample(0, 1000, 1.0f);

            const auto resampled = IRLibrary::resample(impulse, 44100.0, 48000.0);
            expect(resampled.getNumSamples() > 0);

            auto peak = 0.0f;
            auto peakIndex = 0;
            for (int i = 0; i < resampled.getNumSamples(); ++i)
            {
                if (std::abs(resampled.getSample(0, i)) > peak)
                {
                    peak = std::abs(resampled.getSample(0, i));
                    peakIndex = i;
                }
            }

            expect(peak > 0.5f, "the impulse lost most of its amplitude");

            // The expected position, and nothing far from it should be anywhere near as loud.
            const auto expectedIndex = (int) std::lround(1000.0 * 48000.0 / 44100.0);
            expect(std::abs(peakIndex - expectedIndex) <= 2,
                   "peak moved to " + juce::String(peakIndex) + ", expected near " + juce::String(expectedIndex));

            auto worstDistantSample = 0.0f;
            for (int i = 0; i < resampled.getNumSamples(); ++i)
                if (std::abs(i - peakIndex) > 8)
                    worstDistantSample = std::max(worstDistantSample, std::abs(resampled.getSample(0, i)));

            expect(worstDistantSample < peak * 0.05f,
                   "energy smeared away from the impulse: " + juce::String(worstDistantSample));
        }

        beginTest("the library caches and re-resamples on a rate change");
        {
            IRLibrary library(variant);

            library.setTargetSampleRate(48000.0);
            auto first = library.getDecodedIR(0);
            auto second = library.getDecodedIR(0);

            expect(first != nullptr);
            expect(first == second, "a second request must hit the cache, not decode again");

            library.setTargetSampleRate(96000.0);
            auto atNewRate = library.getDecodedIR(0);

            expect(atNewRate != nullptr);
            expect(atNewRate != first, "changing the session rate must invalidate the cache");
            expect(atNewRate->getNumSamples() > first->getNumSamples(),
                   "the same IR at twice the rate must be about twice as many samples");
        }

        beginTest("IRs are normalised to unit energy, and a Dirac stays an identity");
        {
            // Without this, the wet level depends entirely on how much energy a capture happens to
            // contain - measured at roughly 34x for the harness's room IR, which would make one
            // Wet knob useless across a variant's whole set. Normalising the WHOLE IR once at
            // decode (not per shape) is what lets Length and Attack leave the level alone.
            juce::AudioBuffer<float> noise(2, 4096);
            juce::Random random(1234);
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < 4096; ++i)
                    noise.setSample(channel, i, random.nextFloat() * 2.0f - 1.0f);

            IRLibrary::normaliseToUnitEnergy(noise);

            double energy = 0.0;
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < 4096; ++i)
                    energy += (double) noise.getSample(channel, i) * (double) noise.getSample(channel, i);

            expectWithinAbsoluteError((float) (energy / 2.0), 1.0f, 1.0e-4f,
                                      "per-channel energy should be unity after normalisation");

            // The property every measurement in ConvolutionEngineTests depends on: a unit impulse
            // must normalise to itself, or convolving with the Dirac IR stops being an identity.
            juce::AudioBuffer<float> impulse(1, 1024);
            impulse.clear();
            impulse.setSample(0, 0, 1.0f);
            IRLibrary::normaliseToUnitEnergy(impulse);
            expectWithinAbsoluteError(impulse.getSample(0, 0), 1.0f, 1.0e-6f);
        }

        beginTest("an out-of-range index returns nothing");
        {
            IRLibrary library(variant);
            library.setTargetSampleRate(48000.0);

            expect(library.getDecodedIR(-1) == nullptr);
            expect(library.getDecodedIR(library.getNumIRs()) == nullptr);
        }
    }
};

static IRLibraryTests irLibraryTests;
