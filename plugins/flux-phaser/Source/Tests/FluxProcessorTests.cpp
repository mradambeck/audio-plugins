#include "../PluginProcessor.h"

#include <cmath>

// Flux's DSP lives directly in PluginProcessor.cpp (there's no separate testable DSP class the
// way Gradient has GradientDelayBuffer/GradientPitchShiftEngine), so these tests construct and
// drive the real FluxAudioProcessor - the exact class the plugin ships - rather than an extracted
// stand-in. createEditor() was moved out to PluginEditor.cpp specifically so this file (and the
// FluxTests console app it's built into) never needs to compile the GUI/LookAndFeel/font code,
// keeping this target small and fast like Gradient's.
namespace
{
    void setRaw(FluxAudioProcessor& p, const juce::String& id, float value)
    {
        p.apvts.getRawParameterValue(id)->store(value);
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

class FluxProcessorTests : public juce::UnitTest
{
public:
    FluxProcessorTests() : juce::UnitTest("FluxAudioProcessor", "Flux") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest("Bypass leaves the block completely unmodified");
        {
            FluxAudioProcessor processor;
            setRaw(processor, FluxAudioProcessor::bypassParamID, 1.0f);
            processor.prepareToPlay(sampleRate, 512);

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

        beginTest("isBusesLayoutSupported accepts matched mono or stereo, rejects mismatches and other channel counts");
        {
            FluxAudioProcessor processor;

            juce::AudioProcessor::BusesLayout monoLayout;
            monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
            monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
            expect(processor.isBusesLayoutSupported(monoLayout));

            juce::AudioProcessor::BusesLayout stereoLayout;
            stereoLayout.inputBuses.add(juce::AudioChannelSet::stereo());
            stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());
            expect(processor.isBusesLayoutSupported(stereoLayout));

            juce::AudioProcessor::BusesLayout mismatchedLayout;
            mismatchedLayout.inputBuses.add(juce::AudioChannelSet::mono());
            mismatchedLayout.outputBuses.add(juce::AudioChannelSet::stereo());
            expect(! processor.isBusesLayoutSupported(mismatchedLayout));

            juce::AudioProcessor::BusesLayout lcrLayout;
            lcrLayout.inputBuses.add(juce::AudioChannelSet::createLCR());
            lcrLayout.outputBuses.add(juce::AudioChannelSet::createLCR());
            expect(! processor.isBusesLayoutSupported(lcrLayout));
        }

        beginTest("Mono output bus renders identically to stereo channel 0, feedback/grit/brightness/depth all engaged");
        {
            constexpr int numSamples = 8192;

            auto configure = [](FluxAudioProcessor& p)
            {
                setRaw(p, FluxAudioProcessor::depthParamID, 70.0f);
                setRaw(p, FluxAudioProcessor::feedbackParamID, 50.0f);
                setRaw(p, FluxAudioProcessor::gritParamID, 60.0f);
                setRaw(p, FluxAudioProcessor::brightnessParamID, 40.0f);
                setRaw(p, FluxAudioProcessor::blendParamID, 65.0f);
            };

            FluxAudioProcessor monoProcessor;
            configure(monoProcessor);
            monoProcessor.prepareToPlay(sampleRate, numSamples);
            juce::AudioBuffer<float> monoBuffer(1, numSamples);
            fillSine(monoBuffer, 0.5f, 220.0f, sampleRate);
            juce::MidiBuffer midi;
            monoProcessor.processBlock(monoBuffer, midi);

            FluxAudioProcessor stereoProcessor;
            configure(stereoProcessor);
            stereoProcessor.prepareToPlay(sampleRate, numSamples);
            juce::AudioBuffer<float> stereoBuffer(2, numSamples);
            fillSine(stereoBuffer, 0.5f, 220.0f, sampleRate);
            stereoProcessor.processBlock(stereoBuffer, midi);

            // Channel 0's whole computation (allpassL, feedbackStateL, the Grit/Brightness filters,
            // even the stereo-widening freqHzL offset) never reads anything from the R side - see
            // processBlock()'s per-sample loop - so mono and stereo must render bit-for-bit
            // identically on channel 0 regardless of what these knobs are set to.
            const auto diff = rmsOfDifference(monoBuffer.getReadPointer(0), stereoBuffer.getReadPointer(0), numSamples);
            expectWithinAbsoluteError(diff, 0.0f, 1.0e-9f);

            // And it's not a silent no-op passthrough - the phaser is actually doing something.
            const auto wetRms = rms(monoBuffer.getReadPointer(0), numSamples);
            expect(wetRms > 0.01f);
        }

        beginTest("Blend=0% is an exact dry passthrough - the equal-power crossfade's wet gain is exactly zero there");
        {
            FluxAudioProcessor processor;
            setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::blendParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::feedbackParamID, 80.0f); // should still have zero effect on output
            setRaw(processor, FluxAudioProcessor::gritParamID, 50.0f);    // ditto
            processor.prepareToPlay(sampleRate, 512);

            juce::AudioBuffer<float> buffer(2, 512);
            fillSine(buffer, 0.5f, 300.0f, sampleRate);

            juce::AudioBuffer<float> reference;
            reference.makeCopyOf(buffer);

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expectWithinAbsoluteError(buffer.getSample(ch, i), reference.getSample(ch, i), 1.0e-6f);
        }

        beginTest("Tempo Sync falls back to 120 BPM with no host playhead, and each division maps to the correct LFO rate");
        {
            FluxAudioProcessor processor;
            setRaw(processor, FluxAudioProcessor::syncParamID, 1.0f);
            setRaw(processor, FluxAudioProcessor::divisionParamID, 2.0f); // "1/2" -> x2.0
            processor.prepareToPlay(sampleRate, 64);

            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            // 120 BPM -> 0.5s quarter note -> 1/2 note period = 1.0s -> 1.0Hz.
            expectWithinAbsoluteError(processor.getCurrentLfoRateHz(), 1.0f, 0.01f);

            setRaw(processor, FluxAudioProcessor::divisionParamID, 8.0f); // "1/8" -> x0.5
            processor.processBlock(buffer, midi);

            // 1/8 note period = 0.25s -> 4.0Hz.
            expectWithinAbsoluteError(processor.getCurrentLfoRateHz(), 4.0f, 0.04f);
        }

        beginTest("Brightness substantially attenuates a high-frequency tone when turned down from near-passthrough");
        {
            auto measureRms = [&](float brightness)
            {
                FluxAudioProcessor processor;
                setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::blendParamID, 100.0f);
                setRaw(processor, FluxAudioProcessor::depthParamID, 0.0f); // freeze the sweep
                setRaw(processor, FluxAudioProcessor::feedbackParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::gritParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::brightnessParamID, brightness);
                processor.prepareToPlay(sampleRate, 8192);

                const int numSamples = 8192;
                juce::AudioBuffer<float> buffer(2, numSamples);
                fillSine(buffer, 0.5f, 8000.0f, sampleRate);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);

                return rms(buffer.getReadPointer(0) + 1000, numSamples - 1000);
            };

            const auto passthroughRms = measureRms(100.0f);
            const auto cutRms = measureRms(0.0f);
            expect(cutRms < passthroughRms * 0.3f, "Brightness at 0% (500Hz lowpass) should substantially attenuate an 8kHz tone versus 100% (18kHz, near-passthrough)");
        }

        beginTest("Grit measurably changes the output relative to Grit off");
        {
            auto renderOutput = [&](float grit, juce::AudioBuffer<float>& outBuffer)
            {
                FluxAudioProcessor processor;
                setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::blendParamID, 100.0f);
                setRaw(processor, FluxAudioProcessor::depthParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::feedbackParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::brightnessParamID, 100.0f);
                setRaw(processor, FluxAudioProcessor::gritParamID, grit);
                processor.prepareToPlay(sampleRate, 8192);

                fillSine(outBuffer, 0.6f, 300.0f, sampleRate);
                juce::MidiBuffer midi;
                processor.processBlock(outBuffer, midi);
            };

            const int numSamples = 8192;
            juce::AudioBuffer<float> withoutGrit(2, numSamples);
            juce::AudioBuffer<float> withGrit(2, numSamples);
            renderOutput(0.0f, withoutGrit);
            renderOutput(80.0f, withGrit);

            const auto diff = rmsOfDifference(withoutGrit.getReadPointer(0) + 1000, withGrit.getReadPointer(0) + 1000, numSamples - 1000);
            const auto baseline = rms(withoutGrit.getReadPointer(0) + 1000, numSamples - 1000);
            expect(diff > baseline * 0.2f, "Engaging Grit should measurably change the output waveform, not leave it near-identical");
        }

        beginTest("Grit's cache invalidates on a genuine change mid-session, not just at prepareToPlay");
        {
            FluxAudioProcessor processor;
            setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::blendParamID, 100.0f);
            setRaw(processor, FluxAudioProcessor::depthParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::feedbackParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::brightnessParamID, 100.0f);
            setRaw(processor, FluxAudioProcessor::gritParamID, 0.0f);
            processor.prepareToPlay(sampleRate, 8192);

            const int numSamples = 8192;
            juce::AudioBuffer<float> firstBlock(2, numSamples);
            fillSine(firstBlock, 0.3f, 110.0f, sampleRate); // probe right at gritEqHz, where the cached peaking cut bites hardest
            juce::MidiBuffer midi;
            processor.processBlock(firstBlock, midi);
            const auto firstRms = rms(firstBlock.getReadPointer(0) + 1000, numSamples - 1000);

            setRaw(processor, FluxAudioProcessor::gritParamID, 90.0f); // same processor instance, no reprepare
            juce::AudioBuffer<float> secondBlock(2, numSamples);
            fillSine(secondBlock, 0.3f, 110.0f, sampleRate);
            processor.processBlock(secondBlock, midi);
            const auto secondRms = rms(secondBlock.getReadPointer(0) + 1000, numSamples - 1000);

            expect(std::abs(secondRms - firstRms) > firstRms * 0.1f,
                "Changing Grit mid-session should measurably change the output - if the coefficient "
                "cache were stuck at the first value, this block would look the same as the first");
        }

        beginTest("Brightness's cache invalidates on a genuine change mid-session, not just at prepareToPlay");
        {
            FluxAudioProcessor processor;
            setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::blendParamID, 100.0f);
            setRaw(processor, FluxAudioProcessor::depthParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::feedbackParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::gritParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::brightnessParamID, 100.0f);
            processor.prepareToPlay(sampleRate, 8192);

            const int numSamples = 8192;
            juce::AudioBuffer<float> firstBlock(2, numSamples);
            fillSine(firstBlock, 0.5f, 8000.0f, sampleRate);
            juce::MidiBuffer midi;
            processor.processBlock(firstBlock, midi);
            const auto firstRms = rms(firstBlock.getReadPointer(0) + 1000, numSamples - 1000);

            setRaw(processor, FluxAudioProcessor::brightnessParamID, 0.0f); // same processor instance, no reprepare
            juce::AudioBuffer<float> secondBlock(2, numSamples);
            fillSine(secondBlock, 0.5f, 8000.0f, sampleRate);
            processor.processBlock(secondBlock, midi);
            const auto secondRms = rms(secondBlock.getReadPointer(0) + 1000, numSamples - 1000);

            expect(secondRms < firstRms * 0.3f,
                "Dropping Brightness to 0% mid-session should substantially attenuate an 8kHz tone - "
                "if the coefficient cache were stuck at the first value, this block would look the same as the first");
        }

        beginTest("Grit's EQ cache doesn't survive a session sample-rate change with unmoved knobs");
        {
            constexpr float gritValue = 70.0f;
            constexpr int numSamples = 8192;

            auto measureGritCutAt110Hz = [&](double rate)
            {
                FluxAudioProcessor processor;
                setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::blendParamID, 100.0f);
                setRaw(processor, FluxAudioProcessor::depthParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::feedbackParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::brightnessParamID, 100.0f);
                setRaw(processor, FluxAudioProcessor::gritParamID, gritValue);
                processor.prepareToPlay(rate, numSamples);

                juce::AudioBuffer<float> buffer(2, numSamples);
                fillSine(buffer, 0.3f, 110.0f, rate);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);
                return rms(buffer.getReadPointer(0) + 1000, numSamples - 1000);
            };

            // Correct rate-B behaviour: a processor that only ever knew 11025Hz.
            const auto directAt11025 = measureGritCutAt110Hz(11025.0);

            // Same processor: establish the Grit cache at 44100Hz, THEN switch to 11025Hz with
            // Grit left untouched - exactly the scenario lastGritAmount's reset in prepareToPlay()
            // exists to handle. Without that reset, processBlock() at 11025Hz would incorrectly
            // skip recompute (lastGritAmount already equals gritAmount from the first block) and
            // keep using 44100Hz-derived coefficients.
            FluxAudioProcessor processor;
            setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::blendParamID, 100.0f);
            setRaw(processor, FluxAudioProcessor::depthParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::feedbackParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::brightnessParamID, 100.0f);
            setRaw(processor, FluxAudioProcessor::gritParamID, gritValue);
            processor.prepareToPlay(44100.0, numSamples);
            {
                juce::AudioBuffer<float> warmup(2, numSamples);
                fillSine(warmup, 0.3f, 110.0f, 44100.0);
                juce::MidiBuffer midi;
                processor.processBlock(warmup, midi);
            }
            processor.prepareToPlay(11025.0, numSamples);
            juce::AudioBuffer<float> afterRateChange(2, numSamples);
            fillSine(afterRateChange, 0.3f, 110.0f, 11025.0);
            juce::MidiBuffer midi2;
            processor.processBlock(afterRateChange, midi2);
            const auto afterRateChangeRms = rms(afterRateChange.getReadPointer(0) + 1000, numSamples - 1000);

            expectWithinAbsoluteError(afterRateChangeRms, directAt11025, directAt11025 * 0.1f + 1.0e-6f);
        }

        beginTest("Brightness's filter cache doesn't survive a session sample-rate change with unmoved knobs");
        {
            constexpr float brightnessValue = 20.0f; // ~1024Hz cutoff, well under either rate's Nyquist
            constexpr int numSamples = 8192;

            auto measureBrightnessAt2kHz = [&](double rate)
            {
                FluxAudioProcessor processor;
                setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::blendParamID, 100.0f);
                setRaw(processor, FluxAudioProcessor::depthParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::feedbackParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::gritParamID, 0.0f);
                setRaw(processor, FluxAudioProcessor::brightnessParamID, brightnessValue);
                processor.prepareToPlay(rate, numSamples);

                juce::AudioBuffer<float> buffer(2, numSamples);
                fillSine(buffer, 0.5f, 2000.0f, rate);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);
                return rms(buffer.getReadPointer(0) + 1000, numSamples - 1000);
            };

            const auto directAt11025 = measureBrightnessAt2kHz(11025.0);

            FluxAudioProcessor processor;
            setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::blendParamID, 100.0f);
            setRaw(processor, FluxAudioProcessor::depthParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::feedbackParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::gritParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::brightnessParamID, brightnessValue);
            processor.prepareToPlay(44100.0, numSamples);
            {
                juce::AudioBuffer<float> warmup(2, numSamples);
                fillSine(warmup, 0.5f, 2000.0f, 44100.0);
                juce::MidiBuffer midi;
                processor.processBlock(warmup, midi);
            }
            processor.prepareToPlay(11025.0, numSamples);
            juce::AudioBuffer<float> afterRateChange(2, numSamples);
            fillSine(afterRateChange, 0.5f, 2000.0f, 11025.0);
            juce::MidiBuffer midi2;
            processor.processBlock(afterRateChange, midi2);
            const auto afterRateChangeRms = rms(afterRateChange.getReadPointer(0) + 1000, numSamples - 1000);

            expectWithinAbsoluteError(afterRateChangeRms, directAt11025, directAt11025 * 0.1f + 1.0e-6f);
        }

        beginTest("Feedback stays bounded (no runaway/NaN) even at 100% with a loud sustained input");
        {
            FluxAudioProcessor processor;
            setRaw(processor, FluxAudioProcessor::bypassParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::blendParamID, 100.0f);
            setRaw(processor, FluxAudioProcessor::depthParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::feedbackParamID, 100.0f);
            setRaw(processor, FluxAudioProcessor::gritParamID, 0.0f);
            setRaw(processor, FluxAudioProcessor::brightnessParamID, 100.0f);
            processor.prepareToPlay(sampleRate, 512);

            juce::MidiBuffer midi;
            float peak = 0.0f;
            bool sawNonFinite = false;

            for (int block = 0; block < 90; ++block) // ~0.96s
            {
                juce::AudioBuffer<float> buffer(2, 512);
                fillSine(buffer, 0.8f, 300.0f, sampleRate);
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
            expect(peak < 5.0f, "Feedback loop should stay well short of runaway even at 100% Feedback with a loud sustained input");
        }
    }
};

static FluxProcessorTests fluxProcessorTests;
