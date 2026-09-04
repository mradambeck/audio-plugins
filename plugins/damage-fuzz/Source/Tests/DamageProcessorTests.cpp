#include "../PluginProcessor.h"

#include <cmath>

// Damage's DSP lives directly in PluginProcessor.cpp (there's no separate testable DSP class the
// way Gradient has GradientDelayBuffer/GradientPitchShiftEngine), so these tests construct and
// drive the real DamageAudioProcessor - the exact class the plugin ships - rather than an
// extracted stand-in. createEditor() was moved out to PluginEditor.cpp specifically so this file
// (and the DamageTests console app it's built into) never needs to compile the GUI/LookAndFeel/
// font code, keeping this target small and fast like Gradient's.
namespace
{
    void setRaw(DamageAudioProcessor& p, const juce::String& id, float value)
    {
        p.apvts.getRawParameterValue(id)->store(value);
    }

    // updateFilters() (called from both prepareToPlay() and every processBlock()) reads
    // getSampleRate(), not the sampleRate passed directly into prepareToPlay() - without this,
    // getSampleRate() returns 0, updateFilters() bails out early, and Hi Pass/Lo Pass/the Hi
    // Pass EQ dip are left at their default (silent) coefficients.
    void prepareProcessor(DamageAudioProcessor& p, double sampleRate, int blockSize)
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

    // Standard deviation of RMS measured across consecutive chunks - near zero for a constant-
    // level signal, clearly nonzero for one whose amplitude is being chopped/modulated over time.
    float chunkRmsStdDev(const float* data, int numSamples, int chunkSize)
    {
        std::vector<float> chunkRms;
        for (int start = 0; start + chunkSize <= numSamples; start += chunkSize)
            chunkRms.push_back(rms(data + start, chunkSize));

        if (chunkRms.empty())
            return 0.0f;

        double mean = 0.0;
        for (auto v : chunkRms)
            mean += v;
        mean /= (double) chunkRms.size();

        double variance = 0.0;
        for (auto v : chunkRms)
            variance += ((double) v - mean) * ((double) v - mean);
        variance /= (double) chunkRms.size();

        return (float) std::sqrt(variance);
    }
}

class DamageProcessorTests : public juce::UnitTest
{
public:
    DamageProcessorTests() : juce::UnitTest("DamageAudioProcessor", "Damage") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest("Bypass leaves the block completely unmodified");
        {
            DamageAudioProcessor processor;
            setRaw(processor, DamageAudioProcessor::bypassParamID, 1.0f);
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

        // Measures getGateLevelDb() (the envelope follower's own tracked level, exposed for the
        // editor's level meter) directly rather than inferring gate state from the fuzzed/filtered
        // audio output - the comparator-based fuzz pins to a constant rail for any input smaller
        // than its duty-cycle threshold offset, and that constant then gets removed by the (gate-
        // independent) DC blocker regardless of the actual gate state, which would otherwise make
        // an audio-output-based measurement pass for the wrong reason.
        beginTest("Gate: getGateLevelDb() reads as open for a loud tone and decays well below threshold once a quiet tone begins");
        {
            DamageAudioProcessor processor;
            setRaw(processor, DamageAudioProcessor::bypassParamID, 0.0f);
            setRaw(processor, DamageAudioProcessor::gateParamID, -40.0f);
            setRaw(processor, DamageAudioProcessor::driveParamID, 1.0f);
            setRaw(processor, DamageAudioProcessor::widthParamID, 50.0f);
            setRaw(processor, DamageAudioProcessor::squareParamID, 0.0f);
            setRaw(processor, DamageAudioProcessor::oscillateParamID, 0.0f);
            setRaw(processor, DamageAudioProcessor::highPassParamID, 20.0f);
            setRaw(processor, DamageAudioProcessor::lowPassParamID, 20000.0f);
            setRaw(processor, DamageAudioProcessor::dryParamID, 0.0f);
            setRaw(processor, DamageAudioProcessor::wetParamID, 100.0f);
            setRaw(processor, DamageAudioProcessor::slowReleaseParamID, 0.0f);
            const int chunkSize = 480; // 10ms
            prepareProcessor(processor, sampleRate, chunkSize);

            juce::MidiBuffer midi;
            juce::AudioBuffer<float> chunk(2, chunkSize);

            // 300ms of a loud tone, well above the -40dB gate threshold, to fully open and settle it.
            for (int i = 0; i < 30; ++i)
            {
                fillSine(chunk, 0.5f, 200.0f, sampleRate);
                processor.processBlock(chunk, midi);
            }
            const auto openDb = processor.getGateLevelDb();

            // Then a quiet tone, far below the gate's close threshold - track the decay over 1s.
            float levelAt100ms = 0.0f, levelAt800ms = 0.0f;
            for (int i = 0; i < 100; ++i)
            {
                fillSine(chunk, 0.0001f, 200.0f, sampleRate);
                processor.processBlock(chunk, midi);
                if (i == 9)
                    levelAt100ms = processor.getGateLevelDb();
                if (i == 79)
                    levelAt800ms = processor.getGateLevelDb();
            }

            expect(openDb > -20.0f, "A loud tone well above the gate threshold should read as clearly open");
            expect(levelAt800ms < -40.0f, "800ms after the input drops, the tracked level should have decayed well below the gate's close threshold");
            expect(levelAt800ms < levelAt100ms, "The tracked level should keep decaying over time, not jump straight to its floor");
        }

        beginTest("Hi Pass substantially attenuates a low-frequency fuzzed tone relative to a near-passthrough setting");
        {
            auto measureRms = [&](float highPassHz)
            {
                DamageAudioProcessor processor;
                setRaw(processor, DamageAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::gateParamID, -80.0f); // always open
                setRaw(processor, DamageAudioProcessor::driveParamID, 1.0f);
                setRaw(processor, DamageAudioProcessor::widthParamID, 50.0f);
                setRaw(processor, DamageAudioProcessor::squareParamID, 1.0f);
                setRaw(processor, DamageAudioProcessor::oscillateParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::highPassParamID, highPassHz);
                setRaw(processor, DamageAudioProcessor::lowPassParamID, 20000.0f);
                setRaw(processor, DamageAudioProcessor::dryParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::wetParamID, 100.0f);
                setRaw(processor, DamageAudioProcessor::slowReleaseParamID, 0.0f);
                prepareProcessor(processor, sampleRate, 8192);

                const int numSamples = 8192;
                juce::AudioBuffer<float> buffer(2, numSamples);
                fillSine(buffer, 0.5f, 50.0f, sampleRate);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                return rms(buffer.getReadPointer(0) + 1000, numSamples - 1000);
            };

            const auto passthroughRms = measureRms(20.0f);
            const auto cutRms = measureRms(800.0f);
            expect(cutRms < passthroughRms * 0.4f, "An 800Hz Hi Pass should substantially attenuate a fuzzed 50Hz fundamental");
        }

        beginTest("Lo Pass substantially attenuates a high-frequency fuzzed tone relative to a near-passthrough setting");
        {
            auto measureRms = [&](float lowPassHz)
            {
                DamageAudioProcessor processor;
                setRaw(processor, DamageAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::gateParamID, -80.0f); // always open
                setRaw(processor, DamageAudioProcessor::driveParamID, 1.0f);
                setRaw(processor, DamageAudioProcessor::widthParamID, 50.0f);
                setRaw(processor, DamageAudioProcessor::squareParamID, 1.0f);
                setRaw(processor, DamageAudioProcessor::oscillateParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::highPassParamID, 20.0f);
                setRaw(processor, DamageAudioProcessor::lowPassParamID, lowPassHz);
                setRaw(processor, DamageAudioProcessor::dryParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::wetParamID, 100.0f);
                setRaw(processor, DamageAudioProcessor::slowReleaseParamID, 0.0f);
                prepareProcessor(processor, sampleRate, 8192);

                const int numSamples = 8192;
                juce::AudioBuffer<float> buffer(2, numSamples);
                fillSine(buffer, 0.5f, 3000.0f, sampleRate);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                return rms(buffer.getReadPointer(0) + 1000, numSamples - 1000);
            };

            const auto passthroughRms = measureRms(20000.0f);
            const auto cutRms = measureRms(1000.0f);
            expect(cutRms < passthroughRms * 0.4f, "A 1kHz Lo Pass should substantially attenuate a fuzzed 3kHz fundamental");
        }

        beginTest("FM/oscillate audibly chops a sustained tone's level over time; without it the level stays essentially constant");
        {
            auto measureChunkStdDev = [&](bool oscillateOn)
            {
                DamageAudioProcessor processor;
                setRaw(processor, DamageAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::gateParamID, -80.0f); // always open
                setRaw(processor, DamageAudioProcessor::driveParamID, 5.0f);
                setRaw(processor, DamageAudioProcessor::widthParamID, 50.0f);
                setRaw(processor, DamageAudioProcessor::squareParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::oscillateParamID, oscillateOn ? 1.0f : 0.0f);
                setRaw(processor, DamageAudioProcessor::oscFreqParamID, 5.0f);
                setRaw(processor, DamageAudioProcessor::highPassParamID, 20.0f);
                setRaw(processor, DamageAudioProcessor::lowPassParamID, 20000.0f);
                setRaw(processor, DamageAudioProcessor::dryParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::wetParamID, 100.0f);
                setRaw(processor, DamageAudioProcessor::slowReleaseParamID, 0.0f);
                prepareProcessor(processor, sampleRate, 65536);

                const int numSamples = 48000;
                juce::AudioBuffer<float> buffer(2, numSamples);
                fillSine(buffer, 0.4f, 300.0f, sampleRate);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                // 20ms chunks - short enough to resolve the ~20-40Hz chop rate this FM setup lands on.
                return chunkRmsStdDev(buffer.getReadPointer(0), numSamples, (int) (0.02 * sampleRate));
            };

            const auto stdDevOff = measureChunkStdDev(false);
            const auto stdDevOn = measureChunkStdDev(true);
            expect(stdDevOn > stdDevOff * 3.0f, "Engaging FM/oscillate should make the level vary far more across time than a steady fuzzed tone");
        }

        beginTest("Slow Release keeps getGateLevelDb() elevated noticeably longer after the input drops than the default release");
        {
            auto measureDbAfterSilence = [&](bool slowRelease)
            {
                DamageAudioProcessor processor;
                setRaw(processor, DamageAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::gateParamID, -40.0f);
                setRaw(processor, DamageAudioProcessor::driveParamID, 1.0f);
                setRaw(processor, DamageAudioProcessor::widthParamID, 50.0f);
                setRaw(processor, DamageAudioProcessor::squareParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::oscillateParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::highPassParamID, 20.0f);
                setRaw(processor, DamageAudioProcessor::lowPassParamID, 20000.0f);
                setRaw(processor, DamageAudioProcessor::dryParamID, 0.0f);
                setRaw(processor, DamageAudioProcessor::wetParamID, 100.0f);
                setRaw(processor, DamageAudioProcessor::slowReleaseParamID, slowRelease ? 1.0f : 0.0f);
                const int chunkSize = 480; // 10ms
                prepareProcessor(processor, sampleRate, chunkSize);

                juce::MidiBuffer midi;
                juce::AudioBuffer<float> chunk(2, chunkSize);

                // 300ms of a loud tone to fully open and settle the gate.
                for (int i = 0; i < 30; ++i)
                {
                    fillSine(chunk, 0.4f, 200.0f, sampleRate);
                    processor.processBlock(chunk, midi);
                }

                // Then a quiet tone - track the level 500ms into the silence.
                float levelAt500ms = 0.0f;
                for (int i = 0; i < 60; ++i)
                {
                    fillSine(chunk, 0.0001f, 200.0f, sampleRate);
                    processor.processBlock(chunk, midi);
                    if (i == 49)
                        levelAt500ms = processor.getGateLevelDb();
                }
                return levelAt500ms;
            };

            const auto defaultDb = measureDbAfterSilence(false);
            const auto slowDb = measureDbAfterSilence(true);
            expect(slowDb > defaultDb + 10.0f, "500ms after the input drops, Slow Release's tracked level should still read markedly higher than the default release's");
        }
    }
};

static DamageProcessorTests damageProcessorTests;
