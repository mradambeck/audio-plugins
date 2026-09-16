#include "ConvolutionProcessor.h"

#include <cmath>

// Drives the real shared AudioProcessor: bus negotiation, the mono fan-out, state round-tripping,
// and the bypass-parameter override. None of this is reachable from the engine-only suite.
// TestCreateEditorStub.cpp keeps the editor, the LookAndFeel and the fonts out of this target.
namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    void fillSine(juce::AudioBuffer<float>& buffer, float amplitude, float frequencyHz)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(channel, i, amplitude * std::sin(juce::MathConstants<float>::twoPi
                                                                  * frequencyHz * (float) i / (float) sampleRate));
    }

    float rms(const juce::AudioBuffer<float>& buffer, int channel)
    {
        double sum = 0.0;
        const auto* samples = buffer.getReadPointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sum += (double) samples[i] * (double) samples[i];
        return (float) std::sqrt(sum / (double) buffer.getNumSamples());
    }
}

class ConvolutionProcessorTests : public juce::UnitTest
{
public:
    ConvolutionProcessorTests() : juce::UnitTest("ConvolutionProcessor", "Convolution") {}

    void runTest() override
    {
        using namespace wildjag::conv;

        beginTest("accepts mono->mono, mono->stereo and stereo->stereo");
        {
            ConvolutionProcessor processor(variantConfig());

            const auto mono = juce::AudioChannelSet::mono();
            const auto stereo = juce::AudioChannelSet::stereo();

            auto layout = [](const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
            {
                juce::AudioProcessor::BusesLayout l;
                l.inputBuses.add(in);
                l.outputBuses.add(out);
                return l;
            };

            expect(processor.isBusesLayoutSupported(layout(mono, mono)));
            expect(processor.isBusesLayoutSupported(layout(mono, stereo)));
            expect(processor.isBusesLayoutSupported(layout(stereo, stereo)));

            // Widening output beyond stereo is not supported - juce::dsp::Convolution handles mono
            // and stereo only, and silently dropping channels would be worse than refusing.
            expect(! processor.isBusesLayoutSupported(layout(stereo, mono)));
            expect(! processor.isBusesLayoutSupported(layout(stereo, juce::AudioChannelSet::create5point1())));
        }

        beginTest("exposes a real bypass parameter to the host");
        {
            ConvolutionProcessor processor(variantConfig());

            // Deliberately different from the rest of this catalog, which uses a plain parameter and
            // an early return. A convolution tail cannot survive that idiom.
            auto* bypass = processor.getBypassParameter();
            expect(bypass != nullptr, "the host must be given a bypass parameter to drive");
            expect(bypass == processor.apvts.getParameter(ConvolutionProcessor::bypassParamID));
        }

        beginTest("reports zero latency to the host");
        {
            ConvolutionProcessor processor(variantConfig());
            processor.setPlayConfigDetails(2, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            expectEquals(processor.getLatencySamples(), 0);
        }

        beginTest("produces stereo output from a mono input");
        {
            ConvolutionProcessor processor(variantConfig());
            processor.setPlayConfigDetails(1, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            juce::AudioBuffer<float> buffer(2, blockSize);
            buffer.clear();
            fillSine(buffer, 0.5f, 440.0f);
            buffer.clear(1, 0, blockSize); // only channel 0 carries the mono input

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            expect(rms(buffer, 1) > 0.0f, "the right channel was left silent by a mono input");
        }

        beginTest("a full run at defaults passes signal and stays finite");
        {
            ConvolutionProcessor processor(variantConfig());
            processor.setPlayConfigDetails(2, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            juce::MidiBuffer midi;
            auto sawAnyOutput = false;

            for (int block = 0; block < 40; ++block)
            {
                juce::AudioBuffer<float> buffer(2, blockSize);
                fillSine(buffer, 0.5f, 220.0f);
                processor.processBlock(buffer, midi);

                for (int channel = 0; channel < 2; ++channel)
                {
                    const auto* samples = buffer.getReadPointer(channel);
                    for (int i = 0; i < blockSize; ++i)
                    {
                        expect(std::isfinite(samples[i]), "non-finite sample in block " + juce::String(block));
                        if (std::abs(samples[i]) > 1.0e-6f)
                            sawAnyOutput = true;
                    }
                }
            }

            expect(sawAnyOutput, "the processor produced silence at its defaults");
        }

        beginTest("tolerates a block larger than the one it was prepared for");
        {
            // Logic's offline bounce does exactly this. The engine is sized with headroom rather
            // than reallocating on the audio thread; the contract is that an oversized block is
            // truncated, not that it crashes.
            ConvolutionProcessor processor(variantConfig());
            processor.setPlayConfigDetails(2, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            juce::AudioBuffer<float> buffer(2, blockSize * 2);
            fillSine(buffer, 0.5f, 220.0f);

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expect(std::isfinite(buffer.getSample(0, i)));
        }

        beginTest("state round-trips through the host's save/restore");
        {
            ConvolutionProcessor saved(variantConfig());

            saved.apvts.getParameter(ConvolutionProcessor::preDelayMsParamID)->setValueNotifyingHost(0.4f);
            saved.apvts.getParameter(ConvolutionProcessor::lengthPercentParamID)->setValueNotifyingHost(0.55f);
            saved.apvts.getParameter(ConvolutionProcessor::wetParamID)->setValueNotifyingHost(0.7f);

            juce::MemoryBlock state;
            saved.getStateInformation(state);

            ConvolutionProcessor restored(variantConfig());
            restored.setStateInformation(state.getData(), (int) state.getSize());

            for (auto* id : { ConvolutionProcessor::preDelayMsParamID,
                              ConvolutionProcessor::lengthPercentParamID,
                              ConvolutionProcessor::wetParamID })
            {
                expectWithinAbsoluteError(restored.apvts.getRawParameterValue(id)->load(),
                                          saved.apvts.getRawParameterValue(id)->load(), 1.0e-4f);
            }
        }

        beginTest("the IR dropdown offers exactly the variant's IRs");
        {
            ConvolutionProcessor processor(variantConfig());

            auto* irParam = dynamic_cast<juce::AudioParameterChoice*>(
                processor.apvts.getParameter(ConvolutionProcessor::irIndexParamID));

            expect(irParam != nullptr);
            expectEquals(irParam->choices.size(), (int) variantConfig().irs.size());

            for (int i = 0; i < irParam->choices.size(); ++i)
                expectEquals(irParam->choices[i], juce::String(variantConfig().irs[(size_t) i].displayName));
        }
    }
};

static ConvolutionProcessorTests convolutionProcessorTests;
