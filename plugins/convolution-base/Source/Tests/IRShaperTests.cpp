#include "IRShaper.h"

#include <cmath>

// IRShaper is a pure function, so these are exact-value tests rather than tolerance tests wherever
// the contract allows it. Two of them - the identity checks - are the reason the plugin can claim
// "Length and Attack default to playing the IR as recorded" as a fact rather than an intention.
namespace
{
    juce::AudioBuffer<float> makeTestIR(int numChannels, int numSamples)
    {
        juce::AudioBuffer<float> buffer(numChannels, numSamples);

        // Deterministic, non-trivial, and never zero - so a test that accidentally compares silence
        // against silence cannot pass by coincidence.
        for (int channel = 0; channel < numChannels; ++channel)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample(channel, i,
                                 std::sin(0.01f * (float) i + (float) channel) * std::exp(-0.0005f * (float) i) + 0.1f);

        return buffer;
    }
}

class IRShaperTests : public juce::UnitTest
{
public:
    IRShaperTests() : juce::UnitTest("IRShaper", "Convolution") {}

    void runTest() override
    {
        using namespace wildjag::conv;

        constexpr double sampleRate = 48000.0;
        const auto source = makeTestIR(2, 24000);

        beginTest("defaults leave the IR bit-identical");
        {
            const IRShaper::Params defaults;
            expect(IRShaper::isIdentity(defaults));

            const auto shaped = IRShaper::shape(source, sampleRate, defaults);

            expectEquals(shaped.getNumChannels(), source.getNumChannels());
            expectEquals(shaped.getNumSamples(), source.getNumSamples());

            // Bit-identical, not approximately equal. Anything else means the default path is
            // applying an envelope it claims not to.
            for (int channel = 0; channel < source.getNumChannels(); ++channel)
                for (int i = 0; i < source.getNumSamples(); ++i)
                    if (! juce::exactlyEqual(shaped.getSample(channel, i), source.getSample(channel, i)))
                    {
                        expect(false, "sample " + juce::String(i) + " differs from the source");
                        return;
                    }

            expect(true);
        }

        beginTest("full length with no attack does not fade the ending");
        {
            // Guards the specific mistake of always applying the truncation fade: at 100% there is
            // nothing to fade, and doing it anyway would quietly shorten every IR in every variant.
            const auto shaped = IRShaper::shape(source, sampleRate, { 100.0f, 0.0f });
            const auto last = source.getNumSamples() - 1;
            expect(juce::exactlyEqual(shaped.getSample(0, last), source.getSample(0, last)));
        }

        beginTest("length truncates to the requested sample count");
        {
            const auto shaped = IRShaper::shape(source, sampleRate, { 50.0f, 0.0f });
            expectEquals(shaped.getNumSamples(), source.getNumSamples() / 2);

            const auto quarter = IRShaper::shape(source, sampleRate, { 25.0f, 0.0f });
            expectEquals(quarter.getNumSamples(), source.getNumSamples() / 4);
        }

        beginTest("truncation fades out to silence, monotonically");
        {
            const auto shaped = IRShaper::shape(source, sampleRate, { 50.0f, 0.0f });
            const auto n = shaped.getNumSamples();

            expect(std::abs(shaped.getSample(0, n - 1)) < 1.0e-6f, "truncated IR must end at silence");

            // The fade region's envelope must never rise. Compared against the source so the IR's
            // own wiggle is divided out and only the applied gain is under test.
            const auto fadeLength = (int) std::lround(IRShaper::maxTruncationFadeMs * 0.001 * sampleRate);
            auto previousGain = 2.0f;

            for (int i = n - fadeLength; i < n; ++i)
            {
                const auto reference = source.getSample(0, i);
                if (std::abs(reference) < 1.0e-4f)
                    continue;

                const auto gain = shaped.getSample(0, i) / reference;
                expect(gain <= previousGain + 1.0e-4f, "fade-out gain rose at sample " + juce::String(i));
                previousGain = gain;
            }
        }

        beginTest("attack starts at silence and reaches unity on time");
        {
            constexpr float attackMs = 50.0f;
            const auto shaped = IRShaper::shape(source, sampleRate, { 100.0f, attackMs });
            const auto attackSamples = (int) std::lround(attackMs * 0.001 * sampleRate);

            expect(std::abs(shaped.getSample(0, 0)) < 1.0e-6f, "attack must start from silence");

            // Past the fade-in the IR is untouched again.
            expect(juce::exactlyEqual(shaped.getSample(0, attackSamples), source.getSample(0, attackSamples)));
            expect(juce::exactlyEqual(shaped.getSample(0, attackSamples + 100), source.getSample(0, attackSamples + 100)));

            // And inside it, the gain is strictly below unity.
            const auto midway = attackSamples / 2;
            expect(std::abs(shaped.getSample(0, midway)) < std::abs(source.getSample(0, midway)));
        }

        beginTest("a long attack on a short IR cannot erase it");
        {
            const auto shortIR = makeTestIR(1, 480); // 10 ms
            const auto shaped = IRShaper::shape(shortIR, sampleRate, { 100.0f, 500.0f });

            expect(shaped.getNumSamples() > 0);
            expect(shaped.getMagnitude(0, shaped.getNumSamples()) > 0.0f,
                   "a 500 ms attack on a 10 ms IR must not leave silence");
        }

        beginTest("length never drops below the minimum");
        {
            const auto shaped = IRShaper::shape(source, sampleRate, { 0.0f, 0.0f });
            expect(shaped.getNumSamples() >= std::min(IRShaper::minimumShapedSamples, source.getNumSamples()));
        }

        beginTest("peak envelope tracks the loudest sample in each span");
        {
            juce::AudioBuffer<float> spike(1, 1000);
            spike.clear();
            spike.setSample(0, 500, 0.75f);

            const auto envelope = IRShaper::computePeakEnvelope(spike, 10);
            expectEquals((int) envelope.size(), 10);

            // Sample 500 of 1000 falls in the sixth of ten spans; every other span is silent.
            expectWithinAbsoluteError(envelope[5], 0.75f, 1.0e-6f);
            expectWithinAbsoluteError(envelope[0], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError(envelope[9], 0.0f, 1.0e-6f);
        }
    }
};

static IRShaperTests irShaperTests;
