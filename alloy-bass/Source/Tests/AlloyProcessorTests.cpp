#include "../PluginProcessor.h"

#include <cmath>

// Alloy's DSP lives directly in PluginProcessor.cpp (there's no separate testable DSP class the
// way Gradient has GradientDelayBuffer/GradientPitchShiftEngine), so these tests construct and
// drive the real AlloyAudioProcessor - the exact class the plugin ships - rather than an extracted
// stand-in. createEditor() was moved out to PluginEditor.cpp specifically so this file (and the
// AlloyTests console app it's built into) never needs to compile the GUI/LookAndFeel/font code,
// keeping this target small and fast like Gradient's.
namespace
{
    void setRaw(AlloyAudioProcessor& p, const juce::String& id, float value)
    {
        p.apvts.getRawParameterValue(id)->store(value);
    }

    // A fully explicit, isolated patch: analog VCO only (sub and FM layers silenced), no drive/
    // filter coloration, envelopes essentially instant so a note reaches full level almost right
    // away. Individual tests override just the handful of parameters they care about on top of
    // this, rather than each test having to set all ~50 parameters by hand.
    void setupIsolatedAnalogPatch(AlloyAudioProcessor& p)
    {
        setRaw(p, AlloyAudioProcessor::analogWaveformParamID, 2.0f); // triangle
        setRaw(p, AlloyAudioProcessor::analogOctaveParamID, 2.0f);   // choice index 2 -> 0 octave shift
        setRaw(p, AlloyAudioProcessor::analogUnisonParamID, 0.0f);   // 1 voice
        setRaw(p, AlloyAudioProcessor::analogDetuneParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::analogFilterCutoffParamID, 18000.0f);
        setRaw(p, AlloyAudioProcessor::analogFilterResonanceParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::analogFilterEnvAmountParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::analogVelocityToFilterParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::analogFilterAttackParamID, 0.001f);
        setRaw(p, AlloyAudioProcessor::analogFilterDecayParamID, 0.005f);
        setRaw(p, AlloyAudioProcessor::analogFilterSustainParamID, 100.0f);
        setRaw(p, AlloyAudioProcessor::analogFilterReleaseParamID, 0.05f);
        setRaw(p, AlloyAudioProcessor::analogAmpAttackParamID, 0.001f);
        setRaw(p, AlloyAudioProcessor::analogAmpDecayParamID, 0.005f);
        setRaw(p, AlloyAudioProcessor::analogAmpSustainParamID, 100.0f);
        setRaw(p, AlloyAudioProcessor::analogAmpReleaseParamID, 0.05f);
        setRaw(p, AlloyAudioProcessor::analogGlideTimeParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::analogVolumeParamID, 100.0f);

        setRaw(p, AlloyAudioProcessor::subEnabledParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::subWaveformParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::subOctaveParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::subVolumeParamID, 0.0f);

        setRaw(p, AlloyAudioProcessor::fmCarrierWaveformParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::fmCarrierOctaveParamID, 2.0f);
        setRaw(p, AlloyAudioProcessor::fmCarrierVolumeParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::fmVelocityToCarrierParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::fmCarrierAttackParamID, 0.001f);
        setRaw(p, AlloyAudioProcessor::fmCarrierDecayParamID, 0.005f);
        setRaw(p, AlloyAudioProcessor::fmCarrierSustainParamID, 100.0f);
        setRaw(p, AlloyAudioProcessor::fmCarrierReleaseParamID, 0.05f);

        setRaw(p, AlloyAudioProcessor::fmModulatorWaveformParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::fmModulatorOctaveParamID, 2.0f);
        setRaw(p, AlloyAudioProcessor::fmModulatorVolumeParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::fmVelocityToBrightnessParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::fmModulatorBrightnessParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::fmModulatorAttackParamID, 0.001f);
        setRaw(p, AlloyAudioProcessor::fmModulatorDecayParamID, 0.005f);
        setRaw(p, AlloyAudioProcessor::fmModulatorSustainParamID, 100.0f);
        setRaw(p, AlloyAudioProcessor::fmModulatorReleaseParamID, 0.05f);

        setRaw(p, AlloyAudioProcessor::arpEnabledParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::arpSyncParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::arpDivisionParamID, 5.0f);
        setRaw(p, AlloyAudioProcessor::arpRateParamID, 4.0f);
        setRaw(p, AlloyAudioProcessor::arpPatternParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::arpOctaveRangeParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::arpGateParamID, 80.0f);
        setRaw(p, AlloyAudioProcessor::arpHoldParamID, 0.0f);

        setRaw(p, AlloyAudioProcessor::mixDriveParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::mixToneParamID, 100.0f);
        setRaw(p, AlloyAudioProcessor::mixOutputParamID, 0.0f);
        setRaw(p, AlloyAudioProcessor::mixAgeParamID, 0.0f);
    }

    juce::MidiBuffer noteOnBuffer(int note, juce::uint8 velocity)
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, velocity), 0);
        return midi;
    }

    juce::MidiBuffer noteOffBuffer(int note)
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        return midi;
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

    // Estimates fundamental frequency from zero-crossing rate - robust enough for a triangle/saw
    // wave whose sign changes track the fundamental even with some harmonic content layered on.
    float estimateFrequencyByZeroCrossings(const float* data, int numSamples, double sampleRate)
    {
        int crossings = 0;
        for (int i = 1; i < numSamples; ++i)
            if ((data[i - 1] < 0.0f) != (data[i] < 0.0f))
                ++crossings;

        const auto windowSeconds = (double) numSamples / sampleRate;
        return (float) ((crossings / 2.0) / windowSeconds);
    }
}

class AlloyProcessorTests : public juce::UnitTest
{
public:
    AlloyProcessorTests() : juce::UnitTest("AlloyAudioProcessor", "Alloy") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest("No MIDI ever received - output stays exactly silent");
        {
            AlloyAudioProcessor processor;
            setupIsolatedAnalogPatch(processor);
            processor.prepareToPlay(sampleRate, 512);

            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expectWithinAbsoluteError(buffer.getSample(ch, i), 0.0f, 1.0e-9f);
        }

        beginTest("Note On produces a tone whose fundamental matches the played MIDI note (isolated analog VCO)");
        {
            AlloyAudioProcessor processor;
            setupIsolatedAnalogPatch(processor);
            processor.prepareToPlay(sampleRate, 16384);

            const int numSamples = 16384;
            juce::AudioBuffer<float> buffer(2, numSamples);
            buffer.clear();
            auto midi = noteOnBuffer(45, 100); // MIDI 45 = A2 = 110Hz
            processor.processBlock(buffer, midi);

            // Skip the first half (envelope attack/decay settling) and measure the back half.
            const auto measuredHz = estimateFrequencyByZeroCrossings(
                buffer.getReadPointer(0) + numSamples / 2, numSamples / 2, sampleRate);

            expectWithinAbsoluteError(measuredHz, 110.0f, 5.0f);
        }

        beginTest("Note Off releases the voice toward silence");
        {
            AlloyAudioProcessor processor;
            setupIsolatedAnalogPatch(processor);
            setRaw(processor, AlloyAudioProcessor::analogAmpReleaseParamID, 0.05f);
            processor.prepareToPlay(sampleRate, 96000);

            juce::MidiBuffer midi;

            const int openSamples = 4800; // 100ms - long enough to reach sustain
            juce::AudioBuffer<float> openBuffer(2, openSamples);
            openBuffer.clear();
            auto noteOn = noteOnBuffer(45, 100);
            processor.processBlock(openBuffer, noteOn);
            const auto openRms = rms(openBuffer.getReadPointer(0) + openSamples - 480, 480);

            const int releaseSamples = 24000; // 500ms - 10x the 50ms release time constant
            juce::AudioBuffer<float> releaseBuffer(2, releaseSamples);
            releaseBuffer.clear();
            auto noteOff = noteOffBuffer(45);
            processor.processBlock(releaseBuffer, noteOff);
            const auto releasedRms = rms(releaseBuffer.getReadPointer(0) + releaseSamples - 4800, 4800);

            expect(openRms > 0.05f, "The sustained, gate-open note should be clearly audible");
            expect(releasedRms < openRms * 0.05f, "500ms after Note Off (10x the 50ms release), the voice should have decayed to near-silence");
        }

        beginTest("Panic silences the voice even though the note was never explicitly released");
        {
            AlloyAudioProcessor processor;
            setupIsolatedAnalogPatch(processor);
            setRaw(processor, AlloyAudioProcessor::analogAmpReleaseParamID, 0.05f);
            processor.prepareToPlay(sampleRate, 96000);

            const int openSamples = 4800;
            juce::AudioBuffer<float> openBuffer(2, openSamples);
            openBuffer.clear();
            auto noteOn = noteOnBuffer(45, 100);
            processor.processBlock(openBuffer, noteOn);
            const auto openRms = rms(openBuffer.getReadPointer(0) + openSamples - 480, 480);

            processor.requestPanic();

            // No note-off ever sent - the note is still "held" from the MIDI state's perspective.
            const int afterPanicSamples = 24000;
            juce::AudioBuffer<float> afterPanicBuffer(2, afterPanicSamples);
            afterPanicBuffer.clear();
            juce::MidiBuffer noMidi;
            processor.processBlock(afterPanicBuffer, noMidi);
            const auto afterPanicRms = rms(afterPanicBuffer.getReadPointer(0) + afterPanicSamples - 4800, 4800);

            expect(openRms > 0.05f, "The sustained, gate-open note should be clearly audible before Panic");
            expect(afterPanicRms < openRms * 0.05f, "500ms after Panic, the voice should have decayed to near-silence despite never receiving a Note Off");
        }

        beginTest("Sub oscillator measurably adds to the output relative to Sub off");
        {
            auto renderOutput = [&](bool subOn, juce::AudioBuffer<float>& outBuffer)
            {
                AlloyAudioProcessor processor;
                setupIsolatedAnalogPatch(processor);
                setRaw(processor, AlloyAudioProcessor::subEnabledParamID, subOn ? 1.0f : 0.0f);
                setRaw(processor, AlloyAudioProcessor::subVolumeParamID, 60.0f);
                processor.prepareToPlay(sampleRate, (int) outBuffer.getNumSamples());

                outBuffer.clear();
                auto midi = noteOnBuffer(45, 100);
                processor.processBlock(outBuffer, midi);
            };

            const int numSamples = 8192;
            juce::AudioBuffer<float> withoutSub(2, numSamples);
            juce::AudioBuffer<float> withSub(2, numSamples);
            renderOutput(false, withoutSub);
            renderOutput(true, withSub);

            const auto diff = rmsOfDifference(withoutSub.getReadPointer(0) + numSamples / 2, withSub.getReadPointer(0) + numSamples / 2, numSamples / 2);
            expect(diff > 0.02f, "Enabling the Sub oscillator should measurably change the output versus Sub off");
        }

        beginTest("Mix Tone substantially attenuates a harmonically rich tone when turned down from bright");
        {
            auto measureRms = [&](float tone)
            {
                AlloyAudioProcessor processor;
                setupIsolatedAnalogPatch(processor);
                setRaw(processor, AlloyAudioProcessor::analogWaveformParamID, 0.0f); // saw - rich in harmonics
                setRaw(processor, AlloyAudioProcessor::mixToneParamID, tone);
                processor.prepareToPlay(sampleRate, 16384);

                const int numSamples = 16384;
                juce::AudioBuffer<float> buffer(2, numSamples);
                buffer.clear();
                auto midi = noteOnBuffer(45, 100); // 110Hz fundamental, harmonics well above 200Hz
                processor.processBlock(buffer, midi);

                return rms(buffer.getReadPointer(0) + numSamples / 2, numSamples / 2);
            };

            const auto brightRms = measureRms(100.0f);
            const auto darkRms = measureRms(0.0f);
            // A modest reduction, not a dramatic one - the 110Hz fundamental itself sits below
            // even Tone=0%'s 200Hz cutoff, so only the harmonics above it get cut; most of the
            // saw's RMS energy is concentrated in that uncuttable fundamental.
            expect(darkRms < brightRms * 0.85f, "Mix Tone at 0% (200Hz lowpass) should measurably darken a harmonically rich 110Hz saw versus 100% (near-passthrough)");
        }
    }
};

static AlloyProcessorTests alloyProcessorTests;
