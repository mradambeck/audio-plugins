#include "../PluginProcessor.h"

#include <cmath>

// Corrosion's DSP lives directly in PluginProcessor.cpp (there's no separate testable DSP class
// the way Gradient has GradientDelayBuffer/GradientPitchShiftEngine), so these tests construct
// and drive the real CorrosionAudioProcessor - the exact class the plugin ships - rather than an
// extracted stand-in. createEditor() was moved out to PluginEditor.cpp specifically so this file
// (and the CorrosionTests console app it's built into) never needs to compile the GUI/LookAndFeel/
// font code, keeping this target small and fast like Gradient's.
namespace
{
    void setRaw(CorrosionAudioProcessor& p, const juce::String& id, float value)
    {
        p.apvts.getRawParameterValue(id)->store(value);
    }

    // updateToneFilter() (called from both prepareToPlay() and every processBlock()) reads
    // getSampleRate(), not the sampleRate passed directly into prepareToPlay() - without this,
    // getSampleRate() returns 0, updateToneFilter() bails out early, and the Tone lowpass/every
    // Color-tied band are left at their default (silent) coefficients.
    void prepareProcessor(CorrosionAudioProcessor& p, double sampleRate, int blockSize)
    {
        p.setRateAndBufferSizeDetails(sampleRate, blockSize);
        p.prepareToPlay(sampleRate, blockSize);
    }

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

    float peakAbs(const float* data, int numSamples)
    {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::abs(data[i]));
        return peak;
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

class CorrosionProcessorTests : public juce::UnitTest
{
public:
    CorrosionProcessorTests() : juce::UnitTest("CorrosionAudioProcessor", "Corrosion") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest("Bypass leaves the block completely unmodified");
        {
            CorrosionAudioProcessor processor;
            setRaw(processor, CorrosionAudioProcessor::bypassParamID, 1.0f);
            prepareProcessor(processor, sampleRate, 512);

            juce::AudioBuffer<float> buffer(2, 512);
            fillSine(buffer, 0.5f, 220.0f, sampleRate);

            juce::AudioBuffer<float> reference;
            reference.makeCopyOf(buffer);

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expectWithinAbsoluteError(buffer.getSample(ch, i), reference.getSample(ch, i), 1.0e-9f);
        }

        beginTest("Tone substantially attenuates a high-frequency tone when turned down from near-passthrough");
        {
            auto measureRms = [&](float toneHz)
            {
                CorrosionAudioProcessor processor;
                setRaw(processor, CorrosionAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::driveParamID, 1.0f);
                setRaw(processor, CorrosionAudioProcessor::biasParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::characterParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::toneParamID, toneHz);
                setRaw(processor, CorrosionAudioProcessor::outputParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::dryParamID, -100.0f);
                setRaw(processor, CorrosionAudioProcessor::compParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::rectBlendParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::rectMixParamID, 0.0f);
                prepareProcessor(processor, sampleRate, 8192);

                const int numSamples = 8192;
                juce::AudioBuffer<float> buffer(2, numSamples);
                fillSine(buffer, 0.3f, 8000.0f, sampleRate);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                return rms(buffer.getReadPointer(0) + 1000, numSamples - 1000);
            };

            const auto passthroughRms = measureRms(20000.0f);
            const auto cutRms = measureRms(500.0f);
            expect(cutRms < passthroughRms * 0.3f, "A 500Hz Tone should substantially attenuate an 8kHz tone relative to near-max Tone");
        }

        beginTest("Character=1 (hard knee) saturates a moderately driven tone into a flatter, lower-crest-factor shape than Character=0 (soft)");
        {
            auto measureCrestFactor = [&](float character)
            {
                CorrosionAudioProcessor processor;
                setRaw(processor, CorrosionAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::driveParamID, 3.0f);
                setRaw(processor, CorrosionAudioProcessor::biasParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::characterParamID, character);
                setRaw(processor, CorrosionAudioProcessor::toneParamID, 20000.0f);
                setRaw(processor, CorrosionAudioProcessor::outputParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::dryParamID, -100.0f);
                setRaw(processor, CorrosionAudioProcessor::compParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::rectBlendParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::rectMixParamID, 0.0f);
                prepareProcessor(processor, sampleRate, 8192);

                const int numSamples = 8192;
                juce::AudioBuffer<float> buffer(2, numSamples);
                fillSine(buffer, 0.5f, 300.0f, sampleRate);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                const auto* data = buffer.getReadPointer(0) + 1000;
                const auto n = numSamples - 1000;
                return peakAbs(data, n) / rms(data, n);
            };

            const auto softCrest = measureCrestFactor(0.0f);
            const auto hardCrest = measureCrestFactor(1.0f);
            expect(hardCrest < softCrest * 0.95f, "The hard-kneed Character variant should read as noticeably more squared-off (lower crest factor) than the soft one at the same Drive");
        }

        beginTest("Color's slapback echo appears near 120ms when Tone is dark, and is far weaker when Tone is bright");
        {
            auto measureEchoWindowRms = [&](float toneHz)
            {
                CorrosionAudioProcessor processor;
                setRaw(processor, CorrosionAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::driveParamID, 1.0f);
                setRaw(processor, CorrosionAudioProcessor::biasParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::characterParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::toneParamID, toneHz);
                setRaw(processor, CorrosionAudioProcessor::outputParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::dryParamID, -100.0f);
                setRaw(processor, CorrosionAudioProcessor::compParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::rectBlendParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::rectMixParamID, 0.0f);
                prepareProcessor(processor, sampleRate, 16384);

                const int numSamples = 16384;
                juce::AudioBuffer<float> buffer(2, numSamples);
                buffer.clear();
                // A short burst rather than a single-sample impulse, so it survives even an
                // aggressive Tone cutoff enough to leave a detectable echo.
                fillSine(buffer, 0.8f, 400.0f, sampleRate);
                for (int i = 200; i < numSamples; ++i)
                {
                    buffer.setSample(0, i, 0.0f);
                    buffer.setSample(1, i, 0.0f);
                }

                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                // Slapback is a fixed 120ms delay - look in a window around there, well after the
                // burst itself (200 samples ~= 4ms) has decayed.
                const int echoStart = (int) (0.115 * sampleRate);
                const int echoLength = (int) (0.02 * sampleRate);
                return rms(buffer.getReadPointer(0) + echoStart, echoLength);
            };

            const auto darkEchoRms = measureEchoWindowRms(1000.0f);
            const auto brightEchoRms = measureEchoWindowRms(20000.0f);
            expect(darkEchoRms > brightEchoRms * 3.0f, "A dark Tone setting's slapback echo should be clearly audible around 120ms, unlike a bright Tone's near-silent one");
        }

        beginTest("Rect Mix measurably changes the output relative to Rect Mix off");
        {
            auto renderOutput = [&](float rectMix, juce::AudioBuffer<float>& outBuffer)
            {
                CorrosionAudioProcessor processor;
                setRaw(processor, CorrosionAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::driveParamID, 1.0f);
                setRaw(processor, CorrosionAudioProcessor::biasParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::characterParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::toneParamID, 20000.0f);
                setRaw(processor, CorrosionAudioProcessor::outputParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::dryParamID, -100.0f);
                setRaw(processor, CorrosionAudioProcessor::compParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::rectBlendParamID, 1.0f);
                setRaw(processor, CorrosionAudioProcessor::rectMixParamID, rectMix);
                prepareProcessor(processor, sampleRate, 8192);

                fillSine(outBuffer, 0.6f, 300.0f, sampleRate);
                juce::MidiBuffer midi;
                processor.processBlock(outBuffer, midi);
            };

            const int numSamples = 8192;
            juce::AudioBuffer<float> withoutRect(2, numSamples);
            juce::AudioBuffer<float> withRect(2, numSamples);
            renderOutput(0.0f, withoutRect);
            renderOutput(1.0f, withRect);

            const auto diff = rmsOfDifference(withoutRect.getReadPointer(0) + 1000, withRect.getReadPointer(0) + 1000, numSamples - 1000);
            const auto baseline = rms(withoutRect.getReadPointer(0) + 1000, numSamples - 1000);
            expect(diff > baseline * 0.2f, "Engaging full-wave Rect Mix should measurably change the output waveform, not leave it near-identical");
        }

        beginTest("Comp reduces the dry path's dynamic range between a loud burst and a quiet tail");
        {
            auto measureLoudToQuietRatio = [&](float comp)
            {
                CorrosionAudioProcessor processor;
                setRaw(processor, CorrosionAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::driveParamID, 1.0f);
                setRaw(processor, CorrosionAudioProcessor::biasParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::characterParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::toneParamID, 20000.0f);
                setRaw(processor, CorrosionAudioProcessor::outputParamID, -100.0f); // silence the wet path
                setRaw(processor, CorrosionAudioProcessor::dryParamID, 0.0f);       // fully dry
                setRaw(processor, CorrosionAudioProcessor::compParamID, comp);
                setRaw(processor, CorrosionAudioProcessor::rectBlendParamID, 0.0f);
                setRaw(processor, CorrosionAudioProcessor::rectMixParamID, 0.0f);
                prepareProcessor(processor, sampleRate, 24000);

                const int numSamples = 24000; // 500ms
                juce::AudioBuffer<float> buffer(2, numSamples);
                // Loud for the first 200ms, quiet for the remaining 300ms.
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto amp = i < (int) (0.2 * sampleRate) ? 0.7f : 0.05f;
                    const auto s = amp * std::sin(juce::MathConstants<float>::twoPi * 300.0f * (float) i / (float) sampleRate);
                    buffer.setSample(0, i, s);
                    buffer.setSample(1, i, s);
                }

                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                const auto loudRms = rms(buffer.getReadPointer(0) + 1000, (int) (0.15 * sampleRate));
                const auto quietStart = (int) (0.35 * sampleRate); // well after the compressor's release has settled
                const auto quietRms = rms(buffer.getReadPointer(0) + quietStart, (int) (0.1 * sampleRate));
                return loudRms / quietRms;
            };

            const auto uncompressedRatio = measureLoudToQuietRatio(0.0f);
            const auto compressedRatio = measureLoudToQuietRatio(1.0f);
            expect(compressedRatio < uncompressedRatio * 0.7f, "Fully engaging Comp should noticeably shrink the loud/quiet ratio versus the uncompressed dry signal");
        }
    }
};

static CorrosionProcessorTests corrosionProcessorTests;
