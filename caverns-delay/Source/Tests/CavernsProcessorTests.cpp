#include "../PluginProcessor.h"

#include <cmath>

// Caverns' DSP lives directly in PluginProcessor.cpp (there's no separate testable DSP class the
// way Gradient has GradientDelayBuffer/GradientPitchShiftEngine), so these tests construct and
// drive the real CavernsAudioProcessor - the exact class the plugin ships - rather than an
// extracted stand-in. createEditor() was moved out to PluginEditor.cpp specifically so this file
// (and the CavernsTests console app it's built into) never needs to compile the GUI/LookAndFeel/
// font code, keeping this target small and fast like Gradient's.
namespace
{
    void setRaw(CavernsAudioProcessor& p, const juce::String& id, float value)
    {
        p.apvts.getRawParameterValue(id)->store(value);
    }

    int argMaxAbs(const float* data, int from, int to)
    {
        int best = from;
        float bestVal = std::abs(data[from]);
        for (int i = from + 1; i < to; ++i)
        {
            const auto v = std::abs(data[i]);
            if (v > bestVal)
            {
                bestVal = v;
                best = i;
            }
        }
        return best;
    }

    float peakAbs(const float* data, int numSamples)
    {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::abs(data[i]));
        return peak;
    }

    float rms(const float* data, int numSamples)
    {
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i)
            sum += (double) data[i] * (double) data[i];
        return (float) std::sqrt(sum / (double) numSamples);
    }
}

class CavernsProcessorTests : public juce::UnitTest
{
public:
    CavernsProcessorTests() : juce::UnitTest("CavernsAudioProcessor", "Caverns") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest("Bypass leaves the block completely unmodified");
        {
            CavernsAudioProcessor processor;
            setRaw(processor, CavernsAudioProcessor::bypassParamID, 1.0f);
            processor.prepareToPlay(sampleRate, 512);

            juce::AudioBuffer<float> buffer(2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample(ch, i, std::sin((float) i * 0.1f));

            juce::AudioBuffer<float> reference;
            reference.makeCopyOf(buffer);

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expectWithinAbsoluteError(buffer.getSample(ch, i), reference.getSample(ch, i), 1.0e-9f);
        }

        beginTest("An impulse reappears at the delay time set by L Time, not before or after");
        {
            CavernsAudioProcessor processor;
            setRaw(processor, CavernsAudioProcessor::bypassParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::syncParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::linkParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::leftTimeParamID, 100.0f);
            setRaw(processor, CavernsAudioProcessor::rightTimeParamID, 100.0f);
            setRaw(processor, CavernsAudioProcessor::feedbackParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::dryParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::wetParamID, 100.0f);
            setRaw(processor, CavernsAudioProcessor::degradeParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::lowCutParamID, 20.0f);
            setRaw(processor, CavernsAudioProcessor::highCutParamID, 20000.0f);
            // Params are set BEFORE prepareToPlay so the delay-time smoother's initial value
            // already equals the target - no 40ms glide to wait out, an exact delay from sample 0.
            processor.prepareToPlay(sampleRate, 8192);

            const int numSamples = 8192;
            juce::AudioBuffer<float> buffer(2, numSamples);
            buffer.clear();
            buffer.setSample(0, 0, 1.0f);
            buffer.setSample(1, 0, 1.0f);

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            const int expectedDelaySamples = (int) std::round(100.0 * 0.001 * sampleRate); // 4800
            const int peakL = argMaxAbs(buffer.getReadPointer(0), 1, numSamples);
            const int peakR = argMaxAbs(buffer.getReadPointer(1), 1, numSamples);

            expectWithinAbsoluteError(peakL, expectedDelaySamples, 5);
            expectWithinAbsoluteError(peakR, expectedDelaySamples, 5);
            expect(peakAbs(buffer.getReadPointer(0), numSamples) > 0.05f, "Delayed impulse should not be silent");
        }

        beginTest("Tempo Sync falls back to 120 BPM with no host playhead, and each division maps to the correct ms value");
        {
            CavernsAudioProcessor processor;
            setRaw(processor, CavernsAudioProcessor::syncParamID, 1.0f);
            setRaw(processor, CavernsAudioProcessor::linkParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::leftSubdivisionParamID, 5.0f);  // "1/4" -> x1.0
            setRaw(processor, CavernsAudioProcessor::rightSubdivisionParamID, 8.0f); // "1/8" -> x0.5
            processor.prepareToPlay(sampleRate, 64);

            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            // 60000 / 120 bpm = 500ms per quarter note.
            expectWithinAbsoluteError(processor.getCurrentLeftDelayMs(), 500.0f, 0.5f);
            expectWithinAbsoluteError(processor.getCurrentRightDelayMs(), 250.0f, 0.5f);
        }

        beginTest("Link forces the right channel to exactly follow the left, ignoring R's own Time knob");
        {
            CavernsAudioProcessor processor;
            setRaw(processor, CavernsAudioProcessor::syncParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::linkParamID, 1.0f);
            setRaw(processor, CavernsAudioProcessor::leftTimeParamID, 200.0f);
            setRaw(processor, CavernsAudioProcessor::rightTimeParamID, 800.0f);
            processor.prepareToPlay(sampleRate, 64);

            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            expectWithinAbsoluteError(processor.getCurrentLeftDelayMs(), 200.0f, 0.5f);
            expectWithinAbsoluteError(processor.getCurrentRightDelayMs(), 200.0f, 0.5f);
        }

        beginTest("Feedback loop stays bounded (no runaway/NaN) even at the 65% ceiling with hot input and high Wet");
        {
            CavernsAudioProcessor processor;
            setRaw(processor, CavernsAudioProcessor::syncParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::linkParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::leftTimeParamID, 5.0f);
            setRaw(processor, CavernsAudioProcessor::rightTimeParamID, 5.0f);
            setRaw(processor, CavernsAudioProcessor::feedbackParamID, 65.0f);
            setRaw(processor, CavernsAudioProcessor::dryParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::wetParamID, 200.0f);
            setRaw(processor, CavernsAudioProcessor::degradeParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::lowCutParamID, 20.0f);
            setRaw(processor, CavernsAudioProcessor::highCutParamID, 20000.0f);
            processor.prepareToPlay(sampleRate, 512);

            juce::MidiBuffer midi;
            float peak = 0.0f;
            bool sawNonFinite = false;

            // ~0.85s across many blocks, with a loud burst in the very first block.
            for (int block = 0; block < 80; ++block)
            {
                juce::AudioBuffer<float> buffer(2, 512);
                buffer.clear();
                if (block == 0)
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 512; ++i)
                            buffer.setSample(ch, i, 1.0f);

                processor.processBlock(buffer, midi);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                    {
                        const auto v = buffer.getSample(ch, i);
                        if (! std::isfinite(v))
                            sawNonFinite = true;
                        peak = std::max(peak, std::abs(v));
                    }
            }

            expect(! sawNonFinite, "Output must never be NaN/Inf");
            expect(peak < 6.0f, "Feedback loop should stay well short of runaway even at max Feedback and Wet");
        }

        beginTest("Low Cut substantially attenuates a low-frequency tone relative to a near-passthrough setting");
        {
            auto measureRms = [&](float lowCutHz)
            {
                CavernsAudioProcessor processor;
                setRaw(processor, CavernsAudioProcessor::syncParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::linkParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::leftTimeParamID, 1.0f);
                setRaw(processor, CavernsAudioProcessor::rightTimeParamID, 1.0f);
                setRaw(processor, CavernsAudioProcessor::feedbackParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::dryParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::wetParamID, 100.0f);
                setRaw(processor, CavernsAudioProcessor::degradeParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::lowCutParamID, lowCutHz);
                setRaw(processor, CavernsAudioProcessor::highCutParamID, 20000.0f);
                processor.prepareToPlay(sampleRate, 4096);

                const int numSamples = 4096;
                juce::AudioBuffer<float> buffer(2, numSamples);
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto s = std::sin(juce::MathConstants<float>::twoPi * 50.0f * (float) i / (float) sampleRate);
                    buffer.setSample(0, i, s);
                    buffer.setSample(1, i, s);
                }
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                // Skip the first ~4ms so the 1ms delay tap and the filter's own settling don't
                // dominate a fairly short measurement window.
                return rms(buffer.getReadPointer(0) + 200, numSamples - 200);
            };

            const auto passthroughRms = measureRms(20.0f);
            const auto cutRms = measureRms(1000.0f);
            expect(cutRms < passthroughRms * 0.3f, "1kHz Low Cut should substantially attenuate a 50Hz tone");
        }

        beginTest("High Cut substantially attenuates a high-frequency tone relative to a near-passthrough setting");
        {
            auto measureRms = [&](float highCutHz)
            {
                CavernsAudioProcessor processor;
                setRaw(processor, CavernsAudioProcessor::syncParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::linkParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::leftTimeParamID, 1.0f);
                setRaw(processor, CavernsAudioProcessor::rightTimeParamID, 1.0f);
                setRaw(processor, CavernsAudioProcessor::feedbackParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::dryParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::wetParamID, 100.0f);
                setRaw(processor, CavernsAudioProcessor::degradeParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::lowCutParamID, 20.0f);
                setRaw(processor, CavernsAudioProcessor::highCutParamID, highCutHz);
                processor.prepareToPlay(sampleRate, 4096);

                const int numSamples = 4096;
                juce::AudioBuffer<float> buffer(2, numSamples);
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto s = std::sin(juce::MathConstants<float>::twoPi * 15000.0f * (float) i / (float) sampleRate);
                    buffer.setSample(0, i, s);
                    buffer.setSample(1, i, s);
                }
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                return rms(buffer.getReadPointer(0) + 200, numSamples - 200);
            };

            const auto passthroughRms = measureRms(20000.0f);
            const auto cutRms = measureRms(1200.0f);
            expect(cutRms < passthroughRms * 0.3f, "1.2kHz High Cut should substantially attenuate a 15kHz tone");
        }

        // Regression tests for the filter-coefficient cache guard added to
        // feedbackDarkenerL/R.coefficients, lowCutFilterL/R.coefficients, and
        // highCutFilterL/R.coefficients (skips the std::tan()+heap-allocating make*() call when
        // the Hz value hasn't changed since the last block).
        beginTest("Low Cut cache invalidates when the parameter changes mid-session, not just at prepareToPlay");
        {
            CavernsAudioProcessor processor;
            setRaw(processor, CavernsAudioProcessor::syncParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::linkParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::leftTimeParamID, 1.0f);
            setRaw(processor, CavernsAudioProcessor::rightTimeParamID, 1.0f);
            setRaw(processor, CavernsAudioProcessor::feedbackParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::dryParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::wetParamID, 100.0f);
            setRaw(processor, CavernsAudioProcessor::degradeParamID, 0.0f);
            setRaw(processor, CavernsAudioProcessor::lowCutParamID, 20.0f); // near-passthrough
            setRaw(processor, CavernsAudioProcessor::highCutParamID, 20000.0f);
            processor.prepareToPlay(sampleRate, 4096);

            auto renderFiftyHzToneRms = [&]
            {
                const int numSamples = 4096;
                juce::AudioBuffer<float> buffer(2, numSamples);
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto s = std::sin(juce::MathConstants<float>::twoPi * 50.0f * (float) i / (float) sampleRate);
                    buffer.setSample(0, i, s);
                    buffer.setSample(1, i, s);
                }
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);
                return rms(buffer.getReadPointer(0) + 200, numSamples - 200);
            };

            const auto passthroughRms = renderFiftyHzToneRms();

            // A genuine change AFTER the cache has already computed once (previous block already
            // established lastLowCutHz) - the very next block must still pick it up.
            setRaw(processor, CavernsAudioProcessor::lowCutParamID, 1000.0f);
            const auto cutRms = renderFiftyHzToneRms();

            expect(cutRms < passthroughRms * 0.3f,
                   "Changing Low Cut mid-session should still take effect on the very next block, "
                   "not stay stuck at the previously-cached coefficient");
        }

        beginTest("Filter coefficients don't survive a session sample-rate change with unmoved knobs");
        {
            // Two ways to reach the SAME final sample rate with the SAME Low Cut value: a direct
            // prepare at that rate (unambiguously correct), and a prepare at a very different rate
            // FIRST (establishing cached coefficient state) followed by a second prepareToPlay() at
            // the target rate with Low Cut left untouched (the actual bug scenario - see
            // lastLowCutHz's own member comment). Both must measure the same, since final behavior
            // should only depend on the CURRENT sample rate and parameter value, never on history.
            auto measureLowCutRmsAt2kHz = [&](double firstPrepareRate, double finalPrepareRate)
            {
                CavernsAudioProcessor processor;
                setRaw(processor, CavernsAudioProcessor::syncParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::linkParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::leftTimeParamID, 1.0f);
                setRaw(processor, CavernsAudioProcessor::rightTimeParamID, 1.0f);
                setRaw(processor, CavernsAudioProcessor::feedbackParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::dryParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::wetParamID, 100.0f);
                setRaw(processor, CavernsAudioProcessor::degradeParamID, 0.0f);
                setRaw(processor, CavernsAudioProcessor::lowCutParamID, 2000.0f);
                setRaw(processor, CavernsAudioProcessor::highCutParamID, 20000.0f);

                processor.prepareToPlay(firstPrepareRate, 512);
                juce::AudioBuffer<float> warmup(2, 512);
                warmup.clear();
                juce::MidiBuffer warmupMidi;
                processor.processBlock(warmup, warmupMidi); // establishes cached coefficient state

                processor.prepareToPlay(finalPrepareRate, 4096); // Low Cut untouched across this call

                const int numSamples = 4096;
                juce::AudioBuffer<float> buffer(2, numSamples);
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto s = std::sin(juce::MathConstants<float>::twoPi * 2000.0f * (float) i / (float) finalPrepareRate);
                    buffer.setSample(0, i, s);
                    buffer.setSample(1, i, s);
                }
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);
                return rms(buffer.getReadPointer(0) + 200, numSamples - 200);
            };

            const auto directRms = measureLowCutRmsAt2kHz(48000.0, 48000.0);
            const auto afterRateChangeRms = measureLowCutRmsAt2kHz(11025.0, 48000.0);

            expectWithinAbsoluteError(afterRateChangeRms, directRms, directRms * 0.1f,
                                       "Filter response after a sample-rate change should match a direct prepare "
                                       "at the same final rate, not a coefficient stale from the old rate");
        }
    }
};

static CavernsProcessorTests cavernsProcessorTests;
