#include "../PluginProcessor.h"

#include <cmath>
#include <vector>

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

    // Runs the arp for numSteps free-running (Sync off) steps of stepLengthSamples each and
    // returns the measured fundamental of each step, sampled from the middle third of its window
    // (safely past the near-instant attack settling and before the gate closes for the release
    // tail at the step's end). midi is delivered only in the FIRST block (step 0) - later steps
    // pass an empty MidiBuffer, matching how a host would only deliver the note-on/off events once.
    std::vector<float> measureArpStepFrequencies(AlloyAudioProcessor& processor, juce::MidiBuffer firstStepMidi,
                                                   int stepLengthSamples, int numSteps, double sampleRate)
    {
        std::vector<float> frequencies;
        for (int step = 0; step < numSteps; ++step)
        {
            juce::AudioBuffer<float> buffer(2, stepLengthSamples);
            buffer.clear();
            auto midi = (step == 0) ? firstStepMidi : juce::MidiBuffer();
            processor.processBlock(buffer, midi);

            const auto windowStart = stepLengthSamples / 2;
            const auto windowLength = (stepLengthSamples * 35) / 100;
            frequencies.push_back(estimateFrequencyByZeroCrossings(
                buffer.getReadPointer(0) + windowStart, windowLength, sampleRate));
        }
        return frequencies;
    }

    void setupArpTestPatch(AlloyAudioProcessor& p, int pattern, int octaveRangeChoiceIndex)
    {
        setupIsolatedAnalogPatch(p);
        setRaw(p, AlloyAudioProcessor::arpEnabledParamID, 1.0f);
        setRaw(p, AlloyAudioProcessor::arpSyncParamID, 0.0f); // free-running, deterministic step timing
        setRaw(p, AlloyAudioProcessor::arpRateParamID, 10.0f); // 10 Hz - 100ms/step
        setRaw(p, AlloyAudioProcessor::arpPatternParamID, (float) pattern);
        setRaw(p, AlloyAudioProcessor::arpOctaveRangeParamID, (float) octaveRangeChoiceIndex);
        setRaw(p, AlloyAudioProcessor::arpGateParamID, 90.0f); // mostly open, so the release tail doesn't reach the measurement window
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

        beginTest("isBusesLayoutSupported accepts mono and stereo, rejects other channel counts");
        {
            AlloyAudioProcessor processor;

            juce::AudioProcessor::BusesLayout monoLayout;
            monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
            expect(processor.isBusesLayoutSupported(monoLayout));

            juce::AudioProcessor::BusesLayout stereoLayout;
            stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());
            expect(processor.isBusesLayoutSupported(stereoLayout));

            juce::AudioProcessor::BusesLayout lcrLayout;
            lcrLayout.outputBuses.add(juce::AudioChannelSet::createLCR());
            expect(! processor.isBusesLayoutSupported(lcrLayout));
        }

        beginTest("Note On through a mono output bus produces the same tone as stereo (isolated analog VCO)");
        {
            const int numSamples = 16384;
            auto midi = noteOnBuffer(45, 100); // MIDI 45 = A2 = 110Hz

            AlloyAudioProcessor monoProcessor;
            setupIsolatedAnalogPatch(monoProcessor);
            monoProcessor.prepareToPlay(sampleRate, numSamples);
            juce::AudioBuffer<float> monoBuffer(1, numSamples);
            monoBuffer.clear();
            monoProcessor.processBlock(monoBuffer, midi);

            const auto measuredHz = estimateFrequencyByZeroCrossings(
                monoBuffer.getReadPointer(0) + numSamples / 2, numSamples / 2, sampleRate);
            expectWithinAbsoluteError(measuredHz, 110.0f, 5.0f);

            // Age is off in the isolated patch, so ageCentsOffset is exactly 0.0f regardless of the
            // random drift/warble noise generator's state (see the comment at its use site in
            // PluginProcessor.cpp) - output is otherwise deterministic, so a separately-constructed
            // stereo instance given the same patch and note should render an identical signal on
            // channel 0. This pins the fix down to "copy mono into N channels", not a signal change.
            AlloyAudioProcessor stereoProcessor;
            setupIsolatedAnalogPatch(stereoProcessor);
            stereoProcessor.prepareToPlay(sampleRate, numSamples);
            juce::AudioBuffer<float> stereoBuffer(2, numSamples);
            stereoBuffer.clear();
            stereoProcessor.processBlock(stereoBuffer, midi);

            const auto diff = rmsOfDifference(monoBuffer.getReadPointer(0), stereoBuffer.getReadPointer(0), numSamples);
            expectWithinAbsoluteError(diff, 0.0f, 1.0e-9f);
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

        // Regression test for the analogFilter.setCutoffFrequency() cache guard (skips the pow()/
        // tan() when neither filterEnvValue nor currentVelocity have changed since the last
        // sample) - a velocity change landing deep into sustain, after many redundant cache-hit
        // samples, must still move the cutoff. Isolates velocity's effect from the envelope's by
        // zeroing Filter Env Amount, so any brightness change can only come from the velocity term.
        beginTest("Filter cutoff responds to a velocity change mid-note, even deep into sustain");
        {
            AlloyAudioProcessor processor;
            setupIsolatedAnalogPatch(processor);
            setRaw(processor, AlloyAudioProcessor::analogWaveformParamID, 0.0f); // saw - rich in harmonics
            setRaw(processor, AlloyAudioProcessor::analogFilterCutoffParamID, 300.0f);
            setRaw(processor, AlloyAudioProcessor::analogFilterResonanceParamID, 30.0f);
            setRaw(processor, AlloyAudioProcessor::analogVelocityToFilterParamID, 100.0f);
            setRaw(processor, AlloyAudioProcessor::analogFilterEnvAmountParamID, 0.0f);
            processor.prepareToPlay(sampleRate, 96000);

            // Low-velocity note-on, held deep into sustain - many redundant cache-hit samples.
            const int sustainSamples = 48000; // 1s
            juce::AudioBuffer<float> sustainBuffer(2, sustainSamples);
            sustainBuffer.clear();
            auto noteOnLow = noteOnBuffer(45, 10);
            processor.processBlock(sustainBuffer, noteOnLow);
            const auto darkRms = rms(sustainBuffer.getReadPointer(0) + sustainSamples - 4800, 4800);

            // Legato note-on (gate still open, same note - no pitch/envelope confound) at a much
            // higher velocity.
            const int afterChangeSamples = 4800; // 100ms - comfortably past the filter settling
            juce::AudioBuffer<float> afterBuffer(2, afterChangeSamples);
            afterBuffer.clear();
            auto noteOnHigh = noteOnBuffer(45, 120);
            processor.processBlock(afterBuffer, noteOnHigh);
            const auto brightRms = rms(afterBuffer.getReadPointer(0) + afterChangeSamples - 480, 480);

            expect(brightRms > darkRms * 1.3f,
                   "A velocity increase mid-note (legato, still sustaining) should open the filter and "
                   "measurably brighten the output - if the cutoff cache incorrectly stuck at the old "
                   "velocity, this wouldn't change");
        }

        // Regression tests for the persistent-buffer buildArpNotePool() refactor - no prior test
        // coverage touched the arpeggiator at all, so this is new ground, not just a regression
        // guard. Pattern ordering/octave expansion is verified observably (measured pitch per arp
        // step), matching this file's approach elsewhere rather than peeking at private state.
        beginTest("Arp Up pattern plays held notes in ascending order, expanded across the octave range");
        {
            AlloyAudioProcessor processor;
            setupArpTestPatch(processor, /*pattern*/ 0 /*Up*/, /*octaveRangeChoiceIndex*/ 1 /*-> 2 octaves*/);
            processor.prepareToPlay(sampleRate, 4800);

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 100), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 52, (juce::uint8) 100), 0);

            // 2 notes x 2 octaves -> a 4-step pool: 48, 52, 60, 64 - one full cycle. (Capacity-reuse
            // across many repeated calls is covered separately below, without per-step pitch
            // checking - a retrigger landing exactly on a cycle-wrap boundary is measurably
            // fragile for zero-crossing pitch estimation specifically, independent of this test's
            // actual concern, which is pool CONTENT/ORDER, not the modulo-index wraparound itself.)
            const auto freqs = measureArpStepFrequencies(processor, midi, /*stepLengthSamples*/ 4800, /*numSteps*/ 4, sampleRate);

            const float expectedSemitones[] = { 48.0f, 52.0f, 60.0f, 64.0f };
            for (size_t i = 0; i < freqs.size(); ++i)
            {
                const auto expectedHz = 440.0f * std::pow(2.0f, (expectedSemitones[i] - 69.0f) / 12.0f);
                expectWithinAbsoluteError(freqs[i], expectedHz, expectedHz * 0.05f,
                                           "Step " + juce::String((int) i) + " should play the expected note in the Up sequence");
            }
        }

        beginTest("Arp Down pattern is the exact reverse of Up's sequence");
        {
            AlloyAudioProcessor processor;
            setupArpTestPatch(processor, /*pattern*/ 1 /*Down*/, /*octaveRangeChoiceIndex*/ 1 /*-> 2 octaves*/);
            processor.prepareToPlay(sampleRate, 4800);

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 100), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 52, (juce::uint8) 100), 0);

            const auto freqs = measureArpStepFrequencies(processor, midi, 4800, 4, sampleRate);

            const float expectedSemitones[] = { 64.0f, 60.0f, 52.0f, 48.0f };
            for (size_t i = 0; i < freqs.size(); ++i)
            {
                const auto expectedHz = 440.0f * std::pow(2.0f, (expectedSemitones[i] - 69.0f) / 12.0f);
                expectWithinAbsoluteError(freqs[i], expectedHz, expectedHz * 0.05f,
                                           "Step " + juce::String((int) i) + " should play the expected note in the Down sequence");
            }
        }

        beginTest("Arp stays stable (no NaN/Inf, no unexpected silence) across many repeated steps");
        {
            // Capacity-reuse correctness for the persistent scratch buffers - buildArpNotePool()
            // is called fresh every step, so any corruption from reusing (not reallocating)
            // arpScratchBaseNotes/arpScratchUpPool/arpScratchResultPool across many calls would
            // show up as instability well before this many steps.
            AlloyAudioProcessor processor;
            setupArpTestPatch(processor, /*pattern*/ 2 /*Up-Down*/, /*octaveRangeChoiceIndex*/ 2 /*3 octaves*/);
            processor.prepareToPlay(sampleRate, 4800);

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 100), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 52, (juce::uint8) 100), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 55, (juce::uint8) 100), 0);

            constexpr int numSteps = 50;
            juce::AudioBuffer<float> lastStep(2, 4800);
            for (int step = 0; step < numSteps; ++step)
            {
                juce::AudioBuffer<float> buffer(2, 4800);
                buffer.clear();
                auto stepMidi = (step == 0) ? midi : juce::MidiBuffer();
                processor.processBlock(buffer, stepMidi);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 4800; ++i)
                        expect(std::isfinite(buffer.getSample(ch, i)), "Output must stay finite across many repeated arp steps");

                if (step == numSteps - 1)
                    lastStep = buffer;
            }

            expect(rms(lastStep.getReadPointer(0), 4800) > 0.01f,
                   "The arp should still be audibly playing after many repeated steps, not have gone silent");
        }

        beginTest("Hold keeps the arp playing the latched chord after all physical keys release, unlike Hold off");
        {
            auto measureRmsAfterReleasingAllKeys = [&](bool holdOn)
            {
                AlloyAudioProcessor processor;
                setupArpTestPatch(processor, /*pattern*/ 0 /*Up*/, /*octaveRangeChoiceIndex*/ 0 /*1 octave*/);
                setRaw(processor, AlloyAudioProcessor::arpHoldParamID, holdOn ? 1.0f : 0.0f);
                processor.prepareToPlay(sampleRate, 4800);

                juce::MidiBuffer noteOns;
                noteOns.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 100), 0);
                noteOns.addEvent(juce::MidiMessage::noteOn(1, 52, (juce::uint8) 100), 0);
                juce::AudioBuffer<float> firstStep(2, 4800);
                firstStep.clear();
                processor.processBlock(firstStep, noteOns);

                // Release both physical keys - heldNotes empties either way; latchedNotes (Hold's
                // chord memory) is untouched by note-off, per handleMidiMessage's own comment.
                juce::MidiBuffer noteOffs;
                noteOffs.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
                noteOffs.addEvent(juce::MidiMessage::noteOff(1, 52), 0);
                juce::AudioBuffer<float> releaseStep(2, 4800);
                releaseStep.clear();
                processor.processBlock(releaseStep, noteOffs);

                // A few more steps with no MIDI at all - the arp must keep sourcing from
                // latchedNotes (Hold on) or fall silent once heldNotes is empty (Hold off).
                juce::AudioBuffer<float> laterStep(2, 4800);
                laterStep.clear();
                juce::MidiBuffer noMidi;
                processor.processBlock(laterStep, noMidi);
                processor.processBlock(laterStep, noMidi);

                return rms(laterStep.getReadPointer(0) + 1600, 1600);
            };

            const auto holdOnRms = measureRmsAfterReleasingAllKeys(true);
            const auto holdOffRms = measureRmsAfterReleasingAllKeys(false);

            expect(holdOnRms > 0.05f, "With Hold on, the arp should keep playing the latched chord after all physical keys release");
            expect(holdOffRms < 0.01f, "With Hold off, the arp should fall silent once all physical keys release (empty pool)");
        }
    }
};

static AlloyProcessorTests alloyProcessorTests;
