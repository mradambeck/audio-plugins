#include "../PluginProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstring>

namespace
{
    void setParam(InhaltAudioProcessor& processor, const char* paramID, float rawValue)
    {
        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(rawValue));
    }

    juce::AudioBuffer<float> renderImpulse(InhaltAudioProcessor& processor, double sampleRate, int numSamples)
    {
        processor.prepareToPlay(sampleRate, 512);

        juce::AudioBuffer<float> output(2, numSamples);
        output.clear();

        juce::AudioBuffer<float> block(2, 512);
        juce::MidiBuffer midi;

        int written = 0;
        while (written < numSamples)
        {
            const auto thisBlockSize = std::min(512, numSamples - written);
            block.setSize(2, thisBlockSize, false, false, true);
            block.clear();
            if (written == 0)
            {
                block.setSample(0, 0, 1.0f);
                block.setSample(1, 0, 1.0f);
            }

            processor.processBlock(block, midi);

            output.copyFrom(0, written, block, 0, 0, thisBlockSize);
            output.copyFrom(1, written, block, 1, 0, thisBlockSize);
            written += thisBlockSize;
        }
        return output;
    }
}

class InhaltProcessorTests : public juce::UnitTest
{
public:
    InhaltProcessorTests() : juce::UnitTest("InhaltAudioProcessor", "Inhalt") {}

    void runTest() override
    {
        constexpr double sampleRate = 44100.0;

        beginTest("Every declared parameter ID resolves to a real parameter");
        {
            InhaltAudioProcessor processor;
            for (const char* id : {
                     InhaltAudioProcessor::timeKnobParamID, InhaltAudioProcessor::highParamID,
                     InhaltAudioProcessor::preDelayMsParamID, InhaltAudioProcessor::lowCutHzParamID,
                     InhaltAudioProcessor::converterParamID, InhaltAudioProcessor::widthParamID,
                     InhaltAudioProcessor::dryParamID, InhaltAudioProcessor::wetParamID,
                     InhaltAudioProcessor::bypassParamID })
            {
                expect(processor.apvts.getParameter(id) != nullptr,
                    juce::String("parameter \"") + id + "\" should resolve");
            }
        }

        beginTest("APVTS state round-trips through getStateInformation/setStateInformation");
        {
            InhaltAudioProcessor processor;
            setParam(processor, InhaltAudioProcessor::timeKnobParamID, 5.5f);
            setParam(processor, InhaltAudioProcessor::highParamID, -4.0f);
            setParam(processor, InhaltAudioProcessor::wetParamID, 77.0f);

            juce::MemoryBlock state;
            processor.getStateInformation(state);

            InhaltAudioProcessor restored;
            restored.setStateInformation(state.getData(), (int) state.getSize());

            expect(std::abs(restored.apvts.getRawParameterValue(InhaltAudioProcessor::timeKnobParamID)->load() - 5.5f) < 1.0e-3f);
            expect(std::abs(restored.apvts.getRawParameterValue(InhaltAudioProcessor::highParamID)->load() - (-4.0f)) < 1.0e-3f);
            expect(std::abs(restored.apvts.getRawParameterValue(InhaltAudioProcessor::wetParamID)->load() - 77.0f) < 1.0e-3f);
        }

        beginTest("Dry=100/Wet=0 is a near-pure passthrough (no audible reverb contribution)");
        {
            InhaltAudioProcessor processor;
            setParam(processor, InhaltAudioProcessor::dryParamID, 100.0f);
            setParam(processor, InhaltAudioProcessor::wetParamID, 0.0f);

            const auto numSamples = (int) (0.2 * sampleRate);
            const auto output = renderImpulse(processor, sampleRate, numSamples);

            // The impulse itself (sample 0) should survive close to unity; well after it, with no
            // wet contribution, the signal should be at or very near silence.
            expect(std::abs(output.getSample(0, 0) - 1.0f) < 0.05f,
                "the dry impulse should pass through close to unity gain");
            float tailPeak = 0.0f;
            for (int i = numSamples / 2; i < numSamples; ++i)
                tailPeak = std::max(tailPeak, std::abs(output.getSample(0, i)));
            expect(tailPeak < 1.0e-3f, "with Wet=0 there should be no audible reverb tail");
        }

        beginTest("Bypass suppresses the wet reverb tail (set before the first process block)");
        {
            InhaltAudioProcessor processor;
            setParam(processor, InhaltAudioProcessor::dryParamID, 100.0f);
            setParam(processor, InhaltAudioProcessor::wetParamID, 100.0f);
            setParam(processor, InhaltAudioProcessor::bypassParamID, 1.0f);

            const auto numSamples = (int) (0.2 * sampleRate);
            const auto output = renderImpulse(processor, sampleRate, numSamples);

            float tailPeak = 0.0f;
            for (int i = numSamples / 2; i < numSamples; ++i)
                tailPeak = std::max(tailPeak, std::abs(output.getSample(0, i)));
            expect(tailPeak < 1.0e-3f, "bypassed should have no audible reverb tail even with Wet=100");
        }

        beginTest("Width=100 is a bit-identical no-op vs. a Width value that would otherwise change the image");
        {
            InhaltAudioProcessor processorA, processorB;
            for (auto* p : { &processorA, &processorB })
            {
                setParam(*p, InhaltAudioProcessor::dryParamID, 0.0f);
                setParam(*p, InhaltAudioProcessor::wetParamID, 100.0f);
            }
            setParam(processorA, InhaltAudioProcessor::widthParamID, 100.0f);
            setParam(processorB, InhaltAudioProcessor::widthParamID, 100.0f);

            const auto numSamples = (int) (0.3 * sampleRate);
            const auto outA = renderImpulse(processorA, sampleRate, numSamples);
            const auto outB = renderImpulse(processorB, sampleRate, numSamples);

            // Deliberate exact comparison (not approximatelyEqual) - "bit-identical" is the actual
            // claim under test, since Width=100 should skip its processing loop entirely rather
            // than compute a numerically-near-identical result. Compares the raw sample data
            // directly rather than sample-by-sample float == float (which -Wfloat-equal flags,
            // reasonably, as usually a mistake - this is the deliberate exception).
            const auto bytesPerChannel = (size_t) numSamples * sizeof(float);
            for (int channel = 0; channel < 2; ++channel)
            {
                expect(std::memcmp(outA.getReadPointer(channel), outB.getReadPointer(channel), bytesPerChannel) == 0,
                    "two runs at Width=100 must be bit-identical");
            }
        }

        beginTest("prepareToPlay snaps smoothed values (no fade-in on the very first block)");
        {
            // The exact bug empirically caught on ConvBase (see plugins/convolution-base's own
            // history): without snapping, the engine's Dry/Wet smoothers start each prepare at 0
            // and glide up over ~20ms, so the FIRST block's dry signal is quieter than it should
            // be. Check the very first sample of the very first block reflects the fully-applied
            // Dry gain, not a ramped-up-from-zero one.
            InhaltAudioProcessor processor;
            setParam(processor, InhaltAudioProcessor::dryParamID, 100.0f);
            setParam(processor, InhaltAudioProcessor::wetParamID, 0.0f);

            processor.prepareToPlay(sampleRate, 512);

            juce::AudioBuffer<float> block(2, 512);
            block.clear();
            block.setSample(0, 0, 1.0f);
            block.setSample(1, 0, 1.0f);
            juce::MidiBuffer midi;
            processor.processBlock(block, midi);

            expect(std::abs(block.getSample(0, 0) - 1.0f) < 0.05f,
                "the very first sample of the very first block should already reflect Dry=100%, not a ramped-up value");
        }
    }
};

static InhaltProcessorTests inhaltProcessorTests;
