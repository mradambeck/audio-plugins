#include "../PluginProcessor.h"

#include <cmath>

// Drives the real AuraAudioProcessor - specifically isBusesLayoutSupported()/processBlock()'s bus
// handling, which AuraFDNEngineTests/AuraParameterMapTests (the engine-only suite this target
// otherwise runs) can't reach at all, since that logic lives entirely in PluginProcessor.cpp.
// TestCreateEditorStub.cpp keeps this out of the GUI/LookAndFeel/font code, same as every other
// plugin's *ProcessorTests target in this repo.
namespace
{
    void fillSine(juce::AudioBuffer<float>& buffer, float amplitude, float freqHz, double sampleRate)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(ch, i, amplitude * std::sin(juce::MathConstants<float>::twoPi * freqHz * (float) i / (float) sampleRate));
    }

    float rms(const float* data, int numSamples)
    {
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i)
            sum += (double) data[i] * (double) data[i];
        return (float) std::sqrt(sum / (double) numSamples);
    }

    float rmsOfDifference(const float* a, const float* b, int numSamples)
    {
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto d = (double) a[i] - (double) b[i];
            sum += d * d;
        }
        return (float) std::sqrt(sum / (double) numSamples);
    }
}

class AuraProcessorTests : public juce::UnitTest
{
public:
    AuraProcessorTests() : juce::UnitTest("AuraAudioProcessor", "Aura") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest("isBusesLayoutSupported accepts mono-in/stereo-out and stereo/stereo, rejects mono-out and other channel counts");
        {
            AuraAudioProcessor processor;

            juce::AudioProcessor::BusesLayout monoInStereoOut;
            monoInStereoOut.inputBuses.add(juce::AudioChannelSet::mono());
            monoInStereoOut.outputBuses.add(juce::AudioChannelSet::stereo());
            expect(processor.isBusesLayoutSupported(monoInStereoOut));

            juce::AudioProcessor::BusesLayout stereoLayout;
            stereoLayout.inputBuses.add(juce::AudioChannelSet::stereo());
            stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());
            expect(processor.isBusesLayoutSupported(stereoLayout));

            juce::AudioProcessor::BusesLayout monoOutLayout;
            monoOutLayout.inputBuses.add(juce::AudioChannelSet::mono());
            monoOutLayout.outputBuses.add(juce::AudioChannelSet::mono());
            expect(! processor.isBusesLayoutSupported(monoOutLayout));

            juce::AudioProcessor::BusesLayout lcrLayout;
            lcrLayout.inputBuses.add(juce::AudioChannelSet::createLCR());
            lcrLayout.outputBuses.add(juce::AudioChannelSet::createLCR());
            expect(! processor.isBusesLayoutSupported(lcrLayout));
        }

        beginTest("A mono input bus renders identically to a stereo input with the same signal on both channels");
        {
            constexpr int numSamples = 8192;

            AuraAudioProcessor monoProcessor;
            juce::AudioProcessor::BusesLayout monoInStereoOut;
            monoInStereoOut.inputBuses.add(juce::AudioChannelSet::mono());
            monoInStereoOut.outputBuses.add(juce::AudioChannelSet::stereo());
            expect(monoProcessor.setBusesLayout(monoInStereoOut));
            monoProcessor.prepareToPlay(sampleRate, numSamples);

            // A mono input bus still gets a 2-channel buffer from the host (max(in,out) channels) -
            // only channel 0 carries real input. Channel 1 is left at silence (rather than a copy of
            // channel 0) specifically so this test actually exercises processBlock()'s duplication,
            // not just coincidentally matches it.
            juce::AudioBuffer<float> monoBuffer(2, numSamples);
            monoBuffer.clear();
            fillSine(monoBuffer, 0.5f, 220.0f, sampleRate);
            for (int i = 0; i < numSamples; ++i)
                monoBuffer.setSample(1, i, 0.0f);
            juce::MidiBuffer midi;
            monoProcessor.processBlock(monoBuffer, midi);

            AuraAudioProcessor stereoProcessor;
            stereoProcessor.prepareToPlay(sampleRate, numSamples);
            juce::AudioBuffer<float> stereoBuffer(2, numSamples);
            fillSine(stereoBuffer, 0.5f, 220.0f, sampleRate);
            stereoProcessor.processBlock(stereoBuffer, midi);

            // getTotalNumInputChannels() < 2 makes processBlock() duplicate channel 0 into channel
            // 1 before the engine ever runs, so a real mono input and a stereo input carrying the
            // same signal on both channels must render bit-for-bit identically.
            const auto diffL = rmsOfDifference(monoBuffer.getReadPointer(0), stereoBuffer.getReadPointer(0), numSamples);
            const auto diffR = rmsOfDifference(monoBuffer.getReadPointer(1), stereoBuffer.getReadPointer(1), numSamples);
            expectWithinAbsoluteError(diffL, 0.0f, 1.0e-9f);
            expectWithinAbsoluteError(diffR, 0.0f, 1.0e-9f);

            // And it's not a silent no-op - the reverb is actually audible.
            const auto wetRms = rms(monoBuffer.getReadPointer(0), numSamples);
            expect(wetRms > 0.001f);

            // And the two output channels genuinely differ - the tank's even/odd line split
            // builds a real stereo image from the mono source on its own (no width control on
            // this engine to disable it), not just mono duplicated to both channels.
            const auto widthRms = rmsOfDifference(monoBuffer.getReadPointer(0), monoBuffer.getReadPointer(1), numSamples);
            expect(widthRms > 0.0005f, "L and R should differ - the reverb should have real stereo width from a mono source");
        }
    }
};

static AuraProcessorTests auraProcessorTests;
