#include "../PluginProcessor.h"

#include <cmath>

// Drives the real ShieldsAudioProcessor - specifically isBusesLayoutSupported()/processBlock()'s
// bus handling, which ShieldsFDNEngineTests (the engine-only suite this target otherwise runs)
// can't reach at all, since that logic lives entirely in PluginProcessor.cpp.
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

class ShieldsProcessorTests : public juce::UnitTest
{
public:
    ShieldsProcessorTests() : juce::UnitTest("ShieldsAudioProcessor", "Shields") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest("isBusesLayoutSupported accepts mono-in/stereo-out and stereo/stereo, rejects mono-out and other channel counts");
        {
            ShieldsAudioProcessor processor;

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

            ShieldsAudioProcessor monoProcessor;
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

            ShieldsAudioProcessor stereoProcessor;
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

        beginTest("a block larger than the one it was prepared for is fully processed, not truncated");
        {
            // The engine is sized with headroom (4x samplesPerBlock - see blockSizeHeadroom) rather
            // than reallocating on the audio thread; the contract is that an oversized block is
            // chunked through the engine, not that everything past the first chunk is left
            // dry-unmixed. 8x the prepared block size genuinely exceeds a single chunk (2x would
            // still fit inside the 4x headroom and never exercise this path at all).
            ShieldsAudioProcessor processor;
            processor.apvts.getParameter(ShieldsAudioProcessor::dryParamID)->setValueNotifyingHost(0.0f);
            processor.apvts.getParameter(ShieldsAudioProcessor::wetParamID)->setValueNotifyingHost(
                processor.apvts.getParameter(ShieldsAudioProcessor::wetParamID)->convertTo0to1(100.0f));
            processor.prepareToPlay(sampleRate, 256);

            juce::AudioBuffer<float> buffer(2, 256 * 8);
            fillSine(buffer, 0.5f, 220.0f, sampleRate);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expect(std::isfinite(buffer.getSample(0, i)));

            // With Dry at 0%, any sample the engine never reached stays exactly 0 - so a non-silent
            // second half is direct evidence the engine ran on it, not just that reading it didn't
            // crash.
            const auto secondHalfRms = rms(buffer.getReadPointer(0) + 256 * 4, 256 * 4);
            expect(secondHalfRms > 0.0f, "samples past the first chunk were left unprocessed");
        }

        beginTest("a fully bypassed mono-routed instance still passes the mono signal through both output channels");
        {
            // Bypass used to return before the mono-in duplication ran at all, so a mono input bus
            // whose channel 1 the host hadn't populated (not guaranteed to be anything in
            // particular) passed straight through untouched on that channel.
            ShieldsAudioProcessor processor;
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add(juce::AudioChannelSet::mono());
            layout.outputBuses.add(juce::AudioChannelSet::stereo());
            expect(processor.setBusesLayout(layout));
            processor.prepareToPlay(sampleRate, 256);
            processor.apvts.getParameter(ShieldsAudioProcessor::bypassParamID)->setValueNotifyingHost(1.0f);

            constexpr int numSamples = 2048;
            juce::AudioBuffer<float> buffer(2, numSamples);
            fillSine(buffer, 0.5f, 220.0f, sampleRate); // channel 0 = the real mono input
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample(1, i, 12345.0f); // poison channel 1, as an uninitialized host buffer might arrive

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            const auto diff = rmsOfDifference(buffer.getReadPointer(0), buffer.getReadPointer(1), numSamples);
            expectWithinAbsoluteError(diff, 0.0f, 1.0e-9f,
                                      "bypassed mono input should duplicate to both output channels, not leave channel 1 untouched");
        }
    }
};

static ShieldsProcessorTests shieldsProcessorTests;
