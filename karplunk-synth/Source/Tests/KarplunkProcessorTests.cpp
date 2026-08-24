#include "../PluginProcessor.h"

#include <cmath>

// Every other Karplunk test (KarplunkTests) drives the isolated KarplunkVoice DSP class
// directly - setStructure()/setPosition() called by hand, never through the real APVTS parameter
// object, MIDI dispatch, or per-sample smoothing that the actual shipped plugin uses. These tests
// instead construct and drive the real KarplunkAudioProcessor - the exact class Logic loads -
// through apvts.getRawParameterValue()->store() and processBlock(), the same path a user's knob
// turns and MIDI notes actually take. createEditor() was moved out to PluginEditor.cpp specifically
// so this file (and the KarplunkProcessorTests console app it's built into) never needs to compile
// the GUI/LookAndFeel/font code, matching alloy-bass's AlloyTests split.
namespace
{
    void setRaw(KarplunkAudioProcessor& p, const juce::String& id, float value)
    {
        p.apvts.getRawParameterValue(id)->store(value);
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

    // Autocorrelation-based period estimate over a short window, same technique
    // KarplunkVoiceTests.cpp uses on the isolated Voice class - here applied to the real
    // processor's rendered output to identify WHICH note is actually sounding at a given moment.
    float estimateFrequencyHz(const float* data, int windowSize, double sampleRate, float expectedHz, int searchRadius = 15)
    {
        const auto expectedDelay = (float) (sampleRate / expectedHz);
        const int centerLag = (int) std::lround(expectedDelay);
        const int lagLo = std::max(1, centerLag - searchRadius);
        const int lagHi = centerLag + searchRadius;
        int bestLag = centerLag;
        double bestCorr = -1e18;
        for (int lag = lagLo; lag <= lagHi; ++lag)
        {
            double corr = 0.0;
            for (int i = 0; i + lag < windowSize; ++i)
                corr += (double) data[i] * (double) data[i + lag];
            if (corr > bestCorr) { bestCorr = corr; bestLag = lag; }
        }
        return (float) (sampleRate / (double) bestLag);
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
            const auto diff = (double) a[i] - (double) b[i];
            sum += diff * diff;
        }
        return (float) std::sqrt(sum / (double) numSamples);
    }

    // Single-frequency magnitude via direct Goertzel summation, same technique KarplunkVoiceTests
    // uses on the isolated Voice class - here applied to the real processor's rendered output.
    float goertzelMagnitude(const float* data, int numSamples, double freqHz, double sampleRate)
    {
        const auto omega = 2.0 * juce::MathConstants<double>::pi * freqHz / sampleRate;
        double real = 0.0;
        double imag = 0.0;
        for (int n = 0; n < numSamples; ++n)
        {
            real += (double) data[n] * std::cos(omega * (double) n);
            imag -= (double) data[n] * std::sin(omega * (double) n);
        }
        return (float) (2.0 * std::sqrt(real * real + imag * imag) / (double) numSamples);
    }

    // Renders a single bowed (continuous, steady-state) note through the real processor, from a
    // freshly constructed instance so NoiseExcitation's deterministic xorshift32 (fixed seed = 1,
    // never time-seeded) starts identically every call - the only thing that can differ between
    // two renders is whatever parameters this function is asked to set differently.
    juce::AudioBuffer<float> renderBowedNote(int note, float structure, float position, double sampleRate, int numSamples)
    {
        KarplunkAudioProcessor processor;
        processor.prepareToPlay(sampleRate, 512);

        setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f); // continuous bow - steady state, easiest to analyze
        setRaw(processor, KarplunkAudioProcessor::structureParamID, structure);
        setRaw(processor, KarplunkAudioProcessor::positionParamID, position);

        juce::AudioBuffer<float> buffer(2, numSamples);
        auto midi = noteOnBuffer(note, 100);
        processor.processBlock(buffer, midi);
        return buffer;
    }
}

class KarplunkProcessorTests : public juce::UnitTest
{
public:
    KarplunkProcessorTests() : juce::UnitTest("KarplunkAudioProcessor", "Karplunk") {}

    void runTest() override
    {
        const double sampleRate = 44100.0;

        beginTest("No MIDI ever received - output stays exactly silent");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);

            juce::AudioBuffer<float> buffer(2, 4096);
            buffer.clear();
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            expectEquals(rms(buffer.getReadPointer(0), buffer.getNumSamples()), 0.0f);
        }

        // Control: proves the render helper itself is deterministic (fixed noise seed, no hidden
        // time/random dependency) before trusting any A/B difference measured below as meaningful.
        beginTest("Two fresh processors with identical parameters render bit-identical output");
        {
            const int numSamples = 44100;
            auto a = renderBowedNote(60, 0.0f, 0.5f, sampleRate, numSamples);
            auto b = renderBowedNote(60, 0.0f, 0.5f, sampleRate, numSamples);

            const auto diff = rmsOfDifference(a.getReadPointer(0), b.getReadPointer(0), numSamples);
            expectEquals(diff, 0.0f);
        }

        // The direct answer to "does Structure do anything in the real, APVTS/MIDI/processBlock-
        // driven plugin, not just the isolated Voice class": render the same bowed note through the
        // real KarplunkAudioProcessor with Structure at 0% vs 100% (everything else, including
        // Position, left at its real default) and confirm the actual rendered audio differs
        // substantially - not a rounding-error-sized difference.
        beginTest("Structure = 100% substantially changes the real plugin's rendered output vs Structure = 0%");
        {
            const int numSamples = (int) (2.0 * sampleRate); // 2s: ~150ms attack + ~300ms decay-to-sustain + long settled tail
            auto withoutStructure = renderBowedNote(60, 0.0f, 0.5f, sampleRate, numSamples);
            auto withStructure = renderBowedNote(60, 1.0f, 0.5f, sampleRate, numSamples);

            // Skip the attack/decay-to-sustain transient and the 20ms parameter-smoothing ramp -
            // measure only the settled steady-state tail.
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto baseline = rms(withoutStructure.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                withoutStructure.getReadPointer(0) + skipSamples,
                withStructure.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Structure=0 tail RMS: " + juce::String(baseline, 6)
                       + ", diff RMS: " + juce::String(diff, 6)
                       + ", ratio: " + juce::String(diff / baseline, 4));

            // A clearly audible effect, not just numerical noise - matches the same bar used for
            // Alloy's own "does this control measurably change the output" processor tests.
            expect(diff > baseline * 0.05f);
        }

        // Where the difference above actually comes from: Structure is supposed to detune upper
        // harmonics away from exact integer multiples of the fundamental (inharmonicity), so a
        // harmonic's Goertzel magnitude measured AT its exact integer-multiple frequency should
        // drop once Structure pulls that harmonic's real energy away from that exact bin.
        beginTest("Structure = 100% measurably detunes the 3rd harmonic away from its exact frequency (real processor)");
        {
            const int numSamples = (int) (2.0 * sampleRate);
            auto withoutStructure = renderBowedNote(60, 0.0f, 0.5f, sampleRate, numSamples);
            auto withStructure = renderBowedNote(60, 1.0f, 0.5f, sampleRate, numSamples);

            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto fundamentalHz = 440.0 * std::pow(2.0, (60.0 - 69.0) / 12.0); // MIDI note 60 = C4
            const auto thirdHarmonicHz = fundamentalHz * 3.0;

            const auto magnitudeWithout = goertzelMagnitude(
                withoutStructure.getReadPointer(0) + skipSamples, tailSamples, thirdHarmonicHz, sampleRate);
            const auto magnitudeWith = goertzelMagnitude(
                withStructure.getReadPointer(0) + skipSamples, tailSamples, thirdHarmonicHz, sampleRate);

            logMessage("3rd harmonic magnitude at exact frequency - Structure=0: " + juce::String(magnitudeWithout, 6)
                       + ", Structure=100%: " + juce::String(magnitudeWith, 6));

            // Structure changes the loop's total delay, which shifts where each partial's true
            // resonant peak actually falls relative to the exact integer-multiple frequency this
            // measures at - that shift can land the peak closer to or further from this fixed
            // measurement point depending on the note/harmonic, so only the MAGNITUDE of change is
            // asserted here, not a specific direction.
            expect(std::abs(magnitudeWith - magnitudeWithout) > magnitudeWithout * 0.1f);
        }

        // The core Mono behavior end-to-end, through the real APVTS/MIDI-driven processor, not
        // just KarplunkMonoNoteStack in isolation: hold A, hold B (both physically held, no
        // note-off yet) - only B should be audible (true mono, not a quiet second voice), proven
        // by frequency estimation on the real rendered output.
        beginTest("Mono mode: only the most recently pressed note sounds while multiple are held");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::monoParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::dampingParamID, 0.9f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f); // continuous tone - easiest to measure

            const auto noteAHz = 440.0 * std::pow(2.0, (60.0 - 69.0) / 12.0); // C4
            const auto noteBHz = 440.0 * std::pow(2.0, (67.0 - 69.0) / 12.0); // G4

            const int settleSamples = (int) (0.2 * sampleRate);

            juce::AudioBuffer<float> bufferA(2, settleSamples);
            auto midiA = noteOnBuffer(60, 100);
            processor.processBlock(bufferA, midiA); // A held

            juce::AudioBuffer<float> bufferB(2, settleSamples);
            auto midiB = noteOnBuffer(67, 100);
            processor.processBlock(bufferB, midiB); // B pressed too, A still physically held

            const int windowSize = 2048;
            const auto measuredHz = estimateFrequencyHz(
                bufferB.getReadPointer(0) + settleSamples - windowSize, windowSize, sampleRate, (float) noteBHz);

            logMessage("Measured Hz after B pressed (A still held): " + juce::String(measuredHz, 1)
                       + " (expected B=" + juce::String(noteBHz, 1) + ", not A=" + juce::String(noteAHz, 1) + ")");

            expect(std::abs(measuredHz - noteBHz) < std::abs(measuredHz - noteAHz),
                   "should be sounding B (most recently pressed), not A");
        }

        // The behavior this whole feature exists for: releasing the currently-sounding note while
        // an earlier one is still held RETRIGGERS that earlier note, rather than leaving it silent
        // or just cutting to nothing. Since Mono only ever drives ONE shared voice, A's frequency
        // reappearing after releasing B is unambiguous proof of a real retrigger - that voice was
        // fully repurposed to B's pitch while B was held (see the test above), so there is no
        // other way A's pitch could reappear.
        beginTest("Mono mode: releasing the top note retriggers the still-held note beneath it");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::monoParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::dampingParamID, 0.9f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);

            const auto noteAHz = 440.0 * std::pow(2.0, (60.0 - 69.0) / 12.0); // C4

            const int settleSamples = (int) (0.2 * sampleRate);

            juce::AudioBuffer<float> bufferA(2, settleSamples);
            auto midiA = noteOnBuffer(60, 100);
            processor.processBlock(bufferA, midiA); // hold A

            juce::AudioBuffer<float> bufferB(2, settleSamples);
            auto midiB = noteOnBuffer(67, 100);
            processor.processBlock(bufferB, midiB); // hold B too (A still physically held)

            juce::AudioBuffer<float> bufferRelease(2, settleSamples);
            auto midiOffB = noteOffBuffer(67);
            processor.processBlock(bufferRelease, midiOffB); // release B - A should retrigger

            const int windowSize = 2048;
            const auto measuredHz = estimateFrequencyHz(
                bufferRelease.getReadPointer(0) + settleSamples - windowSize, windowSize, sampleRate, (float) noteAHz);

            logMessage("Measured Hz after releasing B (A still held): " + juce::String(measuredHz, 1)
                       + " (expected A=" + juce::String(noteAHz, 1) + ")");

            expect(std::abs(measuredHz - noteAHz) < 5.0f, "A should have retriggered and be sounding again");
        }

        // Poly mode (the default) must stay exactly as it was before this feature existed -
        // toggling Mono off (or never touching it) shouldn't change anything about note handling.
        beginTest("Mono defaults to off - Poly behavior is unaffected by the new parameter existing");
        {
            const int numSamples = 44100;
            auto withoutTouchingMono = renderBowedNote(60, 0.0f, 0.5f, sampleRate, numSamples);

            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::monoParamID, 0.0f); // explicit off
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::structureParamID, 0.0f);
            setRaw(processor, KarplunkAudioProcessor::positionParamID, 0.5f);
            juce::AudioBuffer<float> buffer(2, numSamples);
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(buffer, midi);

            const auto diff = rmsOfDifference(withoutTouchingMono.getReadPointer(0), buffer.getReadPointer(0), numSamples);
            expectEquals(diff, 0.0f, "explicitly setting Mono=off should render bit-identical to never touching it at all");
        }

        // Glide's whole point: a legato retrigger should audibly SLIDE through intermediate
        // pitches over Glide Time, not jump instantly - measured at three points within a single
        // continuous render (right at the retrigger, partway through the glide, and well past it)
        // to directly prove a real, gradual pitch sweep is happening, not just "eventually arrives
        // at the right note" (which an instant jump would also satisfy).
        beginTest("Mono + Glide: pitch actually slides between notes over time, not jumping instantly");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::monoParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::glideTimeParamID, 300.0f); // 300ms
            setRaw(processor, KarplunkAudioProcessor::dampingParamID, 0.9f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f); // continuous tone

            const auto noteAHz = 440.0 * std::pow(2.0, (60.0 - 69.0) / 12.0); // C4
            const auto noteBHz = 440.0 * std::pow(2.0, (67.0 - 69.0) / 12.0); // G4

            const int settleSamples = (int) (0.2 * sampleRate);
            juce::AudioBuffer<float> bufferA(2, settleSamples);
            auto midiA = noteOnBuffer(60, 100);
            processor.processBlock(bufferA, midiA); // hold A, let it settle

            // One continuous render spanning the whole glide (and beyond), so all three
            // measurements come from the same unbroken buffer - retriggering B at sample 0.
            const int glideRenderSamples = (int) (1.6 * sampleRate);
            juce::AudioBuffer<float> bufferGlide(2, glideRenderSamples);
            auto midiB = noteOnBuffer(67, 100);
            processor.processBlock(bufferGlide, midiB);

            const int windowSize = 1024; // short - needs to resolve pitch AT a moment in time, not
                                          // average across the whole glide

            // The search must be centered wide enough to find EITHER endpoint's period, not just
            // A's - a search centered/radius-limited around A's own period (as a plain "is this
            // note being played" check would use) can lock onto the wrong autocorrelation peak
            // once the true period has moved well outside that window, which looks exactly like
            // "broken glide" (implausible, non-monotonic frequency readings) but is actually just
            // a search-window bug in the measurement, not the DSP - caught by cross-checking
            // against the closed-form expected trajectory before trusting the numbers.
            const auto periodA = sampleRate / noteAHz;
            const auto periodB = sampleRate / noteBHz;
            const auto searchCenterHz = sampleRate / ((periodA + periodB) / 2.0);
            const auto searchRadius = (int) std::ceil(std::abs(periodA - periodB) / 2.0) + 10;

            auto measureAt = [&](double seconds) {
                const int start = (int) (seconds * sampleRate);
                return estimateFrequencyHz(bufferGlide.getReadPointer(0) + start, windowSize, sampleRate,
                                            (float) searchCenterHz, searchRadius);
            };

            const auto earlyHz = measureAt(0.01);  // ~10ms in - glide has barely started
            const auto midHz = measureAt(0.15);    // ~150ms in - 0.5 time constants, ~39% of the way
            const auto lateHz = measureAt(1.5);    // ~1.5s in - 5 time constants (300ms glide), ~99.3% arrived

            logMessage("Glide sweep - early: " + juce::String(earlyHz, 1) + "Hz, mid: " + juce::String(midHz, 1)
                       + "Hz, late: " + juce::String(lateHz, 1) + "Hz (A=" + juce::String(noteAHz, 1)
                       + "Hz, B=" + juce::String(noteBHz, 1) + "Hz)");

            expect(std::abs(earlyHz - noteAHz) < 10.0, "glide should start at A's pitch, not jump to B immediately");
            expect(std::abs(lateHz - noteBHz) < 10.0, "glide should have reached B's pitch well after the glide time");
            expect(midHz > earlyHz + 5.0 && midHz < lateHz - 5.0,
                   "midway through the glide, pitch should be clearly BETWEEN A and B, not already at either endpoint");
        }

        // Control: with Glide Time at its default (0ms, off), a legato retrigger still jumps
        // instantly - Glide must be an opt-in behavior change, not something that alters Mono's
        // existing retrigger character unless explicitly dialed in.
        beginTest("Mono + Glide=0 (default): legato retrigger still jumps instantly, exactly as before Glide existed");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::monoParamID, 1.0f);
            // glideTimeParamID left untouched - confirms the default (0ms) preserves old behavior.
            setRaw(processor, KarplunkAudioProcessor::dampingParamID, 0.9f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);

            const auto noteBHz = 440.0 * std::pow(2.0, (67.0 - 69.0) / 12.0);

            const int settleSamples = (int) (0.2 * sampleRate);
            juce::AudioBuffer<float> bufferA(2, settleSamples);
            auto midiA = noteOnBuffer(60, 100);
            processor.processBlock(bufferA, midiA);

            juce::AudioBuffer<float> bufferB(2, settleSamples);
            auto midiB = noteOnBuffer(67, 100);
            processor.processBlock(bufferB, midiB);

            const int windowSize = 1024;
            const auto earlyHz = estimateFrequencyHz(bufferB.getReadPointer(0), windowSize, sampleRate, (float) noteBHz);

            logMessage("Glide=0 early measurement: " + juce::String(earlyHz, 1) + "Hz (expected B=" + juce::String(noteBHz, 1) + "Hz immediately)");

            // A generous tolerance (not 5Hz) - the estimator itself only searches over INTEGER
            // sample lags with no sub-sample refinement, which alone is worth a few Hz of
            // quantization error at this frequency (measured ~386.8Hz vs 392.0Hz expected before
            // this was widened) - still comfortably distinguishes "reached B" from "still at A"
            // (261.6Hz, 130Hz away).
            expect(std::abs(earlyHz - noteBHz) < 10.0, "with no Glide time set, the retrigger should reach B's pitch immediately, not glide");
        }

        // Waveshape's direct answer to "does this control do anything in the real, APVTS/MIDI-
        // driven plugin" - same pattern as Structure's own equivalent test.
        beginTest("Waveshape = 100% substantially changes the real plugin's rendered output vs Waveshape = 0%");
        {
            auto renderWithWaveshape = [&](float waveshape) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, waveshape);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto withoutWaveshape = renderWithWaveshape(0.0f);
            auto withWaveshape = renderWithWaveshape(1.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto baseline = rms(withoutWaveshape.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                withoutWaveshape.getReadPointer(0) + skipSamples,
                withWaveshape.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Waveshape=0 tail RMS: " + juce::String(baseline, 6)
                       + ", diff RMS: " + juce::String(diff, 6)
                       + ", ratio: " + juce::String(diff / baseline, 4));

            expect(diff > baseline * 0.05f);
        }

        // The worst case for a nonlinearity living inside the feedback loop, through the REAL
        // processor (not just the isolated Voice class): max Decay, full Bow, max Waveshape, held
        // for several seconds. Also checks the real output doesn't become dramatically LOUDER
        // than the equivalent unshaped render - KarplunkWaveFolder's own per-sample bound doesn't
        // by itself guarantee the loop's overall energy/RMS can't grow when the fold interacts
        // with loop gain across many passes, so this is checked empirically, not assumed.
        beginTest("Waveshape = 100% stays finite, bounded, and not dramatically louder through the real processor");
        {
            auto render = [&](float waveshape) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, waveshape);
                juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto withWaveshape = render(1.0f);
            auto withoutWaveshape = render(0.0f);

            const auto* data = withWaveshape.getReadPointer(0);
            const int numSamples = withWaveshape.getNumSamples();
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                expect(std::isfinite(data[i]), "output must stay finite through the real processor at Waveshape=100%");
                peak = std::max(peak, std::abs(data[i]));
            }

            const int tailSamples = (int) (1.0 * sampleRate);
            const auto shapedTailRms = rms(withWaveshape.getReadPointer(0) + numSamples - tailSamples, tailSamples);
            const auto plainTailRms = rms(withoutWaveshape.getReadPointer(0) + numSamples - tailSamples, tailSamples);

            logMessage("Waveshape=100% peak: " + juce::String(peak, 4)
                       + ", settled tail RMS shaped=" + juce::String(shapedTailRms, 6)
                       + " vs plain=" + juce::String(plainTailRms, 6));

            expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");

            // Not a tight loudness-parity bound - see KarplunkVoice.h's renderNextSample() comment
            // on the two separate waveshaper calls: the OUTPUT-only path deliberately uses little
            // drive compensation (measured: full compensation crushed the fold almost to silence
            // at high drive, which is what originally prompted this whole investigation), so some
            // real loudness increase at extreme settings is the intended, measured tradeoff, not
            // a regression - this bound exists only to catch genuine runaway (an order of
            // magnitude or more).
            //
            // Widened from 6x to 16x once the friction bow model gained its own bow-noise term
            // (see KarplunkExcitation.h) - at Damping=100% (this test's condition) the UNSHAPED
            // reference is now much quieter than at lower Damping (a real, measured property of the
            // noise-driven resonant buildup, not a bug: plain settled here at ~0.0094 - see
            // KarplunkVoiceTests.cpp's own isolated version of this same test for the equivalent
            // finding), which inflates this ratio metric without the SHAPED value itself running
            // away (0.100 here, well inside the 2.5 peak bound and in line with other conditions).
            expect(shapedTailRms < plainTailRms * 16.0f,
                   "Waveshape shouldn't make the sustained loop dramatically (order-of-magnitude) louder than the unshaped equivalent");
        }

        // Waveshaper Type defaults to Fold (index 0) - preserves every existing Waveshape test's
        // behavior exactly, matching every other new-control convention in this project (Mono,
        // Glide Time).
        beginTest("Waveshaper Type defaults to Fold - explicitly setting it to Fold is bit-identical to never touching it");
        {
            const int numSamples = (int) (2.0 * sampleRate);
            auto withoutTouchingType = renderBowedNote(60, 0.0f, 0.5f, sampleRate, numSamples);

            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, 0.0f); // explicit Fold
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::structureParamID, 0.0f);
            setRaw(processor, KarplunkAudioProcessor::positionParamID, 0.5f);
            juce::AudioBuffer<float> buffer(2, numSamples);
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(buffer, midi);

            const auto diff = rmsOfDifference(withoutTouchingType.getReadPointer(0), buffer.getReadPointer(0), numSamples);
            expectEquals(diff, 0.0f, "explicitly selecting Fold should render bit-identical to never touching Waveshaper Type at all");
        }

        beginTest("Waveshaper Type = Fuzz substantially changes the real plugin's rendered output vs Waveshape = 0%");
        {
            auto renderWithFuzz = [&](float waveshape) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, 1.0f); // Fuzz
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, waveshape);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto withoutFuzz = renderWithFuzz(0.0f);
            auto withFuzz = renderWithFuzz(1.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto baseline = rms(withoutFuzz.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                withoutFuzz.getReadPointer(0) + skipSamples,
                withFuzz.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Fuzz=0 tail RMS: " + juce::String(baseline, 6)
                       + ", diff RMS: " + juce::String(diff, 6)
                       + ", ratio: " + juce::String(diff / baseline, 4));

            expect(diff > baseline * 0.05f);
        }

        beginTest("Fold and Fuzz produce measurably DIFFERENT output at the same Waveshape amount - the selector actually changes character");
        {
            auto renderWithType = [&](float type) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, type);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto foldOutput = renderWithType(0.0f);
            auto fuzzOutput = renderWithType(1.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto foldRms = rms(foldOutput.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                foldOutput.getReadPointer(0) + skipSamples,
                fuzzOutput.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Fold vs Fuzz at Waveshape=100% - Fold tail RMS: " + juce::String(foldRms, 6)
                       + ", diff RMS: " + juce::String(diff, 6));

            expect(diff > foldRms * 0.1f, "Fold and Fuzz should sound clearly different at the same Waveshape amount, not coincidentally similar");
        }

        beginTest("Waveshaper Type = Fuzz stays finite and bounded at the worst-case combination (max Decay, full Bow)");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, 1.0f); // Fuzz
            setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, 1.0f);
            juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(buffer, midi);

            const auto* data = buffer.getReadPointer(0);
            float peak = 0.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                expect(std::isfinite(data[i]), "output must stay finite through the real processor at Fuzz=100%");
                peak = std::max(peak, std::abs(data[i]));
            }

            logMessage("Fuzz=100% worst-case peak: " + juce::String(peak, 4));
            expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
        }

        beginTest("Waveshaper Type = Saturate substantially changes the real plugin's rendered output vs Waveshape = 0%");
        {
            auto renderWithSaturate = [&](float waveshape) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, 2.0f); // Saturate
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, waveshape);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto withoutSaturate = renderWithSaturate(0.0f);
            auto withSaturate = renderWithSaturate(1.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto baseline = rms(withoutSaturate.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                withoutSaturate.getReadPointer(0) + skipSamples,
                withSaturate.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Saturate=0 tail RMS: " + juce::String(baseline, 6)
                       + ", diff RMS: " + juce::String(diff, 6)
                       + ", ratio: " + juce::String(diff / baseline, 4));

            expect(diff > baseline * 0.05f);
        }

        beginTest("Fold, Fuzz, and Saturate each produce measurably DIFFERENT output at the same Waveshape amount");
        {
            auto renderWithType = [&](float type) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, type);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto foldOutput = renderWithType(0.0f);
            auto fuzzOutput = renderWithType(1.0f);
            auto saturateOutput = renderWithType(2.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto foldRms = rms(foldOutput.getReadPointer(0) + skipSamples, tailSamples);
            const auto diffFoldSaturate = rmsOfDifference(
                foldOutput.getReadPointer(0) + skipSamples,
                saturateOutput.getReadPointer(0) + skipSamples,
                tailSamples);
            const auto diffFuzzSaturate = rmsOfDifference(
                fuzzOutput.getReadPointer(0) + skipSamples,
                saturateOutput.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Fold vs Saturate diff RMS: " + juce::String(diffFoldSaturate, 6)
                       + ", Fuzz vs Saturate diff RMS: " + juce::String(diffFuzzSaturate, 6));

            expect(diffFoldSaturate > foldRms * 0.1f, "Fold and Saturate should sound clearly different at the same Waveshape amount");
            expect(diffFuzzSaturate > foldRms * 0.1f, "Fuzz and Saturate should sound clearly different at the same Waveshape amount");
        }

        beginTest("Waveshaper Type = Saturate stays finite and bounded at the worst-case combination (max Decay, full Bow)");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, 2.0f); // Saturate
            setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, 1.0f);
            juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(buffer, midi);

            const auto* data = buffer.getReadPointer(0);
            float peak = 0.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                expect(std::isfinite(data[i]), "output must stay finite through the real processor at Saturate=100%");
                peak = std::max(peak, std::abs(data[i]));
            }

            logMessage("Saturate=100% worst-case peak: " + juce::String(peak, 4));
            expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
        }

        beginTest("Waveshaper Type = BitCrush substantially changes the real plugin's rendered output vs Waveshape = 0%");
        {
            auto renderWithBitCrush = [&](float waveshape) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, 3.0f); // BitCrush
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, waveshape);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto withoutBitCrush = renderWithBitCrush(0.0f);
            auto withBitCrush = renderWithBitCrush(1.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto baseline = rms(withoutBitCrush.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                withoutBitCrush.getReadPointer(0) + skipSamples,
                withBitCrush.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("BitCrush=0 tail RMS: " + juce::String(baseline, 6)
                       + ", diff RMS: " + juce::String(diff, 6)
                       + ", ratio: " + juce::String(diff / baseline, 4));

            expect(diff > baseline * 0.05f);
        }

        beginTest("Fold, Fuzz, Saturate, and BitCrush each produce measurably DIFFERENT output at the same Waveshape amount");
        {
            auto renderWithType = [&](float type) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, type);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto foldOutput = renderWithType(0.0f);
            auto fuzzOutput = renderWithType(1.0f);
            auto saturateOutput = renderWithType(2.0f);
            auto bitCrushOutput = renderWithType(3.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto foldRms = rms(foldOutput.getReadPointer(0) + skipSamples, tailSamples);
            const auto diffFoldBitCrush = rmsOfDifference(
                foldOutput.getReadPointer(0) + skipSamples,
                bitCrushOutput.getReadPointer(0) + skipSamples,
                tailSamples);
            const auto diffFuzzBitCrush = rmsOfDifference(
                fuzzOutput.getReadPointer(0) + skipSamples,
                bitCrushOutput.getReadPointer(0) + skipSamples,
                tailSamples);
            const auto diffSaturateBitCrush = rmsOfDifference(
                saturateOutput.getReadPointer(0) + skipSamples,
                bitCrushOutput.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Fold vs BitCrush diff RMS: " + juce::String(diffFoldBitCrush, 6)
                       + ", Fuzz vs BitCrush diff RMS: " + juce::String(diffFuzzBitCrush, 6)
                       + ", Saturate vs BitCrush diff RMS: " + juce::String(diffSaturateBitCrush, 6));

            expect(diffFoldBitCrush > foldRms * 0.1f, "Fold and BitCrush should sound clearly different at the same Waveshape amount");
            expect(diffFuzzBitCrush > foldRms * 0.1f, "Fuzz and BitCrush should sound clearly different at the same Waveshape amount");
            expect(diffSaturateBitCrush > foldRms * 0.1f, "Saturate and BitCrush should sound clearly different at the same Waveshape amount");
        }

        beginTest("Waveshaper Type = BitCrush stays finite and bounded at the worst-case combination (max Decay, full Bow)");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, 3.0f); // BitCrush
            setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, 1.0f);
            juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(buffer, midi);

            const auto* data = buffer.getReadPointer(0);
            float peak = 0.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                expect(std::isfinite(data[i]), "output must stay finite through the real processor at BitCrush=100%");
                peak = std::max(peak, std::abs(data[i]));
            }

            logMessage("BitCrush=100% worst-case peak: " + juce::String(peak, 4));
            expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
        }

        beginTest("Ring Mod Amount defaults to 0% - explicitly setting it to 0% is bit-identical to never touching it");
        {
            const int numSamples = (int) (2.0 * sampleRate);
            auto withoutTouchingRingMod = renderBowedNote(60, 0.0f, 0.5f, sampleRate, numSamples);

            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::ringModAmountParamID, 0.0f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::structureParamID, 0.0f);
            setRaw(processor, KarplunkAudioProcessor::positionParamID, 0.5f);
            juce::AudioBuffer<float> buffer(2, numSamples);
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(buffer, midi);

            const auto diff = rmsOfDifference(withoutTouchingRingMod.getReadPointer(0), buffer.getReadPointer(0), numSamples);
            expectEquals(diff, 0.0f, "explicitly setting Ring Mod to 0% should render bit-identical to never touching it at all");
        }

        beginTest("Ring Mod substantially changes the real plugin's rendered output vs Ring Mod = 0%");
        {
            auto renderWithRingMod = [&](float ringModAmount) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::ringModAmountParamID, ringModAmount);
                setRaw(processor, KarplunkAudioProcessor::ringModFrequencyParamID, 200.0f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto withoutRingMod = renderWithRingMod(0.0f);
            auto withRingMod = renderWithRingMod(1.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto baseline = rms(withoutRingMod.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                withoutRingMod.getReadPointer(0) + skipSamples,
                withRingMod.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Ring Mod=0 tail RMS: " + juce::String(baseline, 6)
                       + ", diff RMS: " + juce::String(diff, 6)
                       + ", ratio: " + juce::String(diff / baseline, 4));

            expect(diff > baseline * 0.05f);
        }

        beginTest("Ring Mod never makes the sustained loop louder than the unmodulated equivalent (can only shrink/invert, see KarplunkRingModulator's own safety argument)");
        {
            // Different framing from every other Waveshaper's worst-case test: ring modulation is
            // provably bounded by the input's own magnitude (see KarplunkRingModulatorTests.cpp),
            // so through the real processor this should show as REDUCED or equal loudness, never
            // increased - a tighter, more specific claim than just "stays finite."
            auto renderRingMod = [&](float ringModAmount) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::ringModAmountParamID, ringModAmount);
                setRaw(processor, KarplunkAudioProcessor::ringModFrequencyParamID, 200.0f);
                juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto without = renderRingMod(0.0f);
            auto with = renderRingMod(1.0f);

            const int numSamples = (int) (4.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto rmsWithout = rms(without.getReadPointer(0) + skipSamples, tailSamples);
            const auto rmsWith = rms(with.getReadPointer(0) + skipSamples, tailSamples);

            logMessage("Ring Mod off tail RMS: " + juce::String(rmsWithout, 6)
                       + ", Ring Mod=100% tail RMS: " + juce::String(rmsWith, 6));

            expect(rmsWith <= rmsWithout * 1.05f, // small tolerance for measurement noise, not a real allowance for growth
                   "ring modulation should never make the sustained loop louder than the unmodulated equivalent");
        }

        beginTest("Ring Mod combined with each Waveshaper Type stays finite and bounded at the worst-case combination");
        {
            for (float waveshaperType : { 0.0f, 1.0f, 2.0f, 3.0f })
            {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, waveshaperType);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::ringModAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::ringModFrequencyParamID, 5000.0f); // worst-case: highest supported frequency
                juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);

                const auto* data = buffer.getReadPointer(0);
                float peak = 0.0f;
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    expect(std::isfinite(data[i]),
                           "output must stay finite with Ring Mod + Waveshaper Type " + juce::String(waveshaperType) + " both at 100%");
                    peak = std::max(peak, std::abs(data[i]));
                }

                logMessage("Ring Mod=100% + Waveshaper Type=" + juce::String(waveshaperType) + " worst-case peak: " + juce::String(peak, 4));
                expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
            }
        }

        beginTest("Topology defaults to Single - explicitly setting it to Single is bit-identical to never touching it");
        {
            const int numSamples = (int) (2.0 * sampleRate);
            auto withoutTouchingTopology = renderBowedNote(60, 0.0f, 0.5f, sampleRate, numSamples);

            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::topologyParamID, 0.0f); // explicit Single
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::structureParamID, 0.0f);
            setRaw(processor, KarplunkAudioProcessor::positionParamID, 0.5f);
            juce::AudioBuffer<float> buffer(2, numSamples);
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(buffer, midi);

            const auto diff = rmsOfDifference(withoutTouchingTopology.getReadPointer(0), buffer.getReadPointer(0), numSamples);
            expectEquals(diff, 0.0f, "explicitly selecting Single topology should render bit-identical to never touching Topology at all");
        }

        beginTest("Topology=Dual substantially changes the real plugin's rendered output vs Topology=Single");
        {
            auto renderWithTopology = [&](float topology) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::topologyParamID, topology);
                setRaw(processor, KarplunkAudioProcessor::crossCoupleParamID, 0.5f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto single = renderWithTopology(0.0f);
            auto dual = renderWithTopology(1.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto baseline = rms(single.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                single.getReadPointer(0) + skipSamples,
                dual.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Topology=Single tail RMS: " + juce::String(baseline, 6)
                       + ", diff RMS: " + juce::String(diff, 6)
                       + ", ratio: " + juce::String(diff / baseline, 4));

            expect(diff > baseline * 0.05f);
        }

        beginTest("Switching Topology mid-performance while a note is held triggers an implicit all-notes-off");
        {
            // Mirrors the real, established Mono/Poly precedent exactly (see processBlock()'s own
            // comment): a mid-note Topology change resets every voice rather than trying to
            // reconcile Single's one-line state with Dual's two-line state. The held note goes
            // silent - it does NOT continue on stale state, and it is NOT retriggered (matching
            // Mono/Poly's own reset(), which never retriggers a still-physically-held key either).
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::topologyParamID, 0.0f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f); // held bow - would
                                                                                // never decay on its
                                                                                // own otherwise

            const int preSwitchSamples = (int) (0.5 * sampleRate);
            juce::AudioBuffer<float> preSwitchBuffer(2, preSwitchSamples);
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(preSwitchBuffer, midi); // note-on lands, note held throughout

            const auto preSwitchRms = rms(preSwitchBuffer.getReadPointer(0), preSwitchSamples);
            expect(preSwitchRms > 0.01f, "the held bow note should be clearly audible before the switch");

            setRaw(processor, KarplunkAudioProcessor::topologyParamID, 1.0f); // flip mid-performance,
                                                                               // no note-off/note-on

            const int postSwitchSamples = (int) (1.0 * sampleRate);
            juce::AudioBuffer<float> postSwitchBuffer(2, postSwitchSamples);
            juce::MidiBuffer noMidi;
            processor.processBlock(postSwitchBuffer, noMidi);

            const auto postSwitchRms = rms(postSwitchBuffer.getReadPointer(0), postSwitchSamples);
            expect(postSwitchRms < preSwitchRms * 0.01f,
                   "a mid-performance Topology switch should silence the held note (implicit all-notes-off), not continue it on stale state or retrigger it");
        }

        beginTest("8 simultaneously held voices at Topology=Dual, max Cross-Couple, full Bow stay bounded");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::topologyParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::crossCoupleParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::dampingParamID, 0.9f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);

            juce::MidiBuffer midi;
            const int notes[8] = { 48, 52, 55, 60, 64, 67, 72, 76 };
            for (int note : notes)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);

            const int numSamples = (int) (3.0 * sampleRate);
            juce::AudioBuffer<float> buffer(2, numSamples);
            processor.processBlock(buffer, midi);

            const auto* data = buffer.getReadPointer(0);
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                expect(std::isfinite(data[i]), "8-voice Dual-topology bowed chord must stay finite");
                peak = std::max(peak, std::abs(data[i]));
            }

            logMessage("8-voice Dual/Cross-Couple=100% worst-case peak: " + juce::String(peak, 4));
            expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
        }

        beginTest("Topology=Dual combined with each Waveshaper Type and Ring Mod stays finite and bounded at the worst-case combination");
        {
            for (float waveshaperType : { 0.0f, 1.0f, 2.0f, 3.0f })
            {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::topologyParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::crossCoupleParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::detuneParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, waveshaperType);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::ringModAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::ringModFrequencyParamID, 5000.0f);
                juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);

                const auto* data = buffer.getReadPointer(0);
                float peak = 0.0f;
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    expect(std::isfinite(data[i]),
                           "output must stay finite with Topology=Dual + Waveshaper Type " + juce::String(waveshaperType) + " + Ring Mod all at worst-case settings");
                    peak = std::max(peak, std::abs(data[i]));
                }

                logMessage("Dual + Waveshaper Type=" + juce::String(waveshaperType) + " + Ring Mod worst-case peak: " + juce::String(peak, 4));
                expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
            }
        }

        beginTest("Couple Delay defaults to 0ms - explicitly setting it to 0 is bit-identical to never touching it");
        {
            auto renderWithDelay = [&](bool touchIt) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::topologyParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::crossCoupleParamID, 0.7f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                if (touchIt)
                    setRaw(processor, KarplunkAudioProcessor::coupleDelayParamID, 0.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto neverTouched = renderWithDelay(false);
            auto explicitZero = renderWithDelay(true);

            const int numSamples = (int) (2.0 * sampleRate);
            const auto diff = rmsOfDifference(neverTouched.getReadPointer(0), explicitZero.getReadPointer(0), numSamples);
            expectEquals(diff, 0.0f, "explicitly setting Couple Delay to 0ms should render bit-identical to never touching it at all");
        }

        beginTest("Couple Delay substantially changes the real plugin's rendered output vs 0ms, at the same Cross-Couple");
        {
            auto renderWithDelay = [&](float delayMs) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::topologyParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::crossCoupleParamID, 0.7f);
                setRaw(processor, KarplunkAudioProcessor::coupleDelayParamID, delayMs);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto withoutDelay = renderWithDelay(0.0f);
            auto withDelay = renderWithDelay(5.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto baseline = rms(withoutDelay.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                withoutDelay.getReadPointer(0) + skipSamples,
                withDelay.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Couple Delay=0 tail RMS: " + juce::String(baseline, 6)
                       + ", diff RMS: " + juce::String(diff, 6)
                       + ", ratio: " + juce::String(diff / baseline, 4));

            expect(diff > baseline * 0.05f);
        }

        beginTest("Couple Delay stays finite and bounded at the worst-case combination (max Decay, full Bow, max Cross-Couple)");
        {
            for (float delayMs : { 0.0f, 5.0f, 10.0f })
            {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::topologyParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::crossCoupleParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::coupleDelayParamID, delayMs);
                setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);

                const auto* data = buffer.getReadPointer(0);
                float peak = 0.0f;
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    expect(std::isfinite(data[i]), "output must stay finite with Couple Delay=" + juce::String(delayMs) + "ms at worst-case settings");
                    peak = std::max(peak, std::abs(data[i]));
                }

                logMessage("Couple Delay=" + juce::String(delayMs) + "ms worst-case peak: " + juce::String(peak, 4));
                expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
            }
        }

        beginTest("Loop Filter Type defaults to Two-Point Average - explicitly setting it is bit-identical to never touching it");
        {
            const int numSamples = (int) (2.0 * sampleRate);
            auto withoutTouchingType = renderBowedNote(60, 0.0f, 0.5f, sampleRate, numSamples);

            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::loopFilterTypeParamID, 0.0f); // explicit Two-Point Average
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::structureParamID, 0.0f);
            setRaw(processor, KarplunkAudioProcessor::positionParamID, 0.5f);
            juce::AudioBuffer<float> buffer(2, numSamples);
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(buffer, midi);

            const auto diff = rmsOfDifference(withoutTouchingType.getReadPointer(0), buffer.getReadPointer(0), numSamples);
            expectEquals(diff, 0.0f, "explicitly selecting Two-Point Average should render bit-identical to never touching Loop Filter Type at all");
        }

        beginTest("Resonance defaults to 0% - explicitly setting it to 0 is bit-identical to never touching it");
        {
            auto renderWithResonance = [&](bool touchIt) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::loopFilterTypeParamID, 1.0f); // Resonant
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                if (touchIt)
                    setRaw(processor, KarplunkAudioProcessor::resonanceParamID, 0.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto neverTouched = renderWithResonance(false);
            auto explicitZero = renderWithResonance(true);

            const int numSamples = (int) (2.0 * sampleRate);
            const auto diff = rmsOfDifference(neverTouched.getReadPointer(0), explicitZero.getReadPointer(0), numSamples);
            expectEquals(diff, 0.0f, "explicitly setting Resonance to 0% should render bit-identical to never touching it at all");
        }

        beginTest("Loop Filter Type=Resonant + Resonance=100% substantially changes the real plugin's rendered output vs Two-Point Average");
        {
            auto renderWithType = [&](float type) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::loopFilterTypeParamID, type);
                setRaw(processor, KarplunkAudioProcessor::resonanceParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto twoPoint = renderWithType(0.0f);
            auto resonant = renderWithType(1.0f);

            const int numSamples = (int) (2.0 * sampleRate);
            const int skipSamples = (int) (0.6 * sampleRate);
            const int tailSamples = numSamples - skipSamples;

            const auto baseline = rms(twoPoint.getReadPointer(0) + skipSamples, tailSamples);
            const auto diff = rmsOfDifference(
                twoPoint.getReadPointer(0) + skipSamples,
                resonant.getReadPointer(0) + skipSamples,
                tailSamples);

            logMessage("Two-Point Average tail RMS: " + juce::String(baseline, 6)
                       + ", diff RMS: " + juce::String(diff, 6)
                       + ", ratio: " + juce::String(diff / baseline, 4));

            expect(diff > baseline * 0.05f);
        }

        beginTest("Loop Filter Type=Resonant, Resonance=100%, Decay=100%, Bow=100% stays finite and bounded, held note over several seconds");
        {
            KarplunkAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            setRaw(processor, KarplunkAudioProcessor::loopFilterTypeParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::resonanceParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
            setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
            juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
            auto midi = noteOnBuffer(60, 100);
            processor.processBlock(buffer, midi);

            const auto* data = buffer.getReadPointer(0);
            float peak = 0.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                expect(std::isfinite(data[i]), "output must stay finite with Loop Filter Type=Resonant at max Resonance/Decay/Bow");
                peak = std::max(peak, std::abs(data[i]));
            }

            logMessage("Resonant loop filter worst-case peak: " + juce::String(peak, 4));
            expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
        }

        beginTest("Loop Filter Type=Resonant + Topology=Dual + Cross-Couple + each Waveshaper Type + Ring Mod stays finite and bounded");
        {
            for (float waveshaperType : { 0.0f, 1.0f, 2.0f, 3.0f })
            {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::loopFilterTypeParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::resonanceParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::topologyParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::crossCoupleParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::detuneParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::waveshaperTypeParamID, waveshaperType);
                setRaw(processor, KarplunkAudioProcessor::waveshapeParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::ringModAmountParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::ringModFrequencyParamID, 5000.0f);
                juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);

                const auto* data = buffer.getReadPointer(0);
                float peak = 0.0f;
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    expect(std::isfinite(data[i]),
                           "output must stay finite with Resonant loop filter + Dual topology + Waveshaper Type " + juce::String(waveshaperType) + " + Ring Mod all at worst-case settings");
                    peak = std::max(peak, std::abs(data[i]));
                }

                logMessage("Resonant + Dual + Waveshaper Type=" + juce::String(waveshaperType) + " + Ring Mod worst-case peak: " + juce::String(peak, 4));
                expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
            }
        }

        beginTest("Noise Color defaults to White - explicitly setting it to White is bit-identical to never touching it");
        {
            auto render = [&](bool touchColor) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                if (touchColor)
                    setRaw(processor, KarplunkAudioProcessor::noiseColorParamID, 0.0f);
                juce::AudioBuffer<float> buffer(2, (int) (1.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto untouched = render(false);
            auto explicitWhite = render(true);
            const auto* a = untouched.getReadPointer(0);
            const auto* b = explicitWhite.getReadPointer(0);
            for (int i = 0; i < untouched.getNumSamples(); ++i)
                expectWithinAbsoluteError(b[i], a[i], 1.0e-9f);
        }

        beginTest("Noise Color = Pink/Brown produce measurably different real-processor output than White, on a plucked note");
        {
            auto renderWithColor = [&](float color) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::noiseColorParamID, color);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 0.0f);
                juce::AudioBuffer<float> buffer(2, (int) (1.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto white = renderWithColor(0.0f);
            auto pink = renderWithColor(1.0f);
            auto brown = renderWithColor(2.0f);

            const auto numSamples = white.getNumSamples();
            const auto whiteRms = rms(white.getReadPointer(0), numSamples);
            const auto pinkDiff = rmsOfDifference(white.getReadPointer(0), pink.getReadPointer(0), numSamples);
            const auto brownDiff = rmsOfDifference(white.getReadPointer(0), brown.getReadPointer(0), numSamples);

            logMessage("Noise Color - White RMS: " + juce::String(whiteRms, 6)
                       + ", Pink diff RMS: " + juce::String(pinkDiff, 6)
                       + ", Brown diff RMS: " + juce::String(brownDiff, 6));

            expect(pinkDiff > whiteRms * 0.1f, "Pink should sound clearly different from White through the real processor");
            expect(brownDiff > whiteRms * 0.1f, "Brown should sound clearly different from White through the real processor");
        }

        beginTest("Noise Color also measurably affects a BOWED note (extended from Pluck-only after the user found no audible difference on Bow)");
        {
            // The bow-noise term (see KarplunkExcitation::nextBowNoiseSample()'s own comment) was
            // originally a fixed-color source, independent of Noise Color - this guards that the
            // extension actually took effect end-to-end through the real processor, not just in the
            // isolated Excitation class.
            auto renderWithColor = [&](float color) {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::noiseColorParamID, color);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (2.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);
                return buffer;
            };

            auto white = renderWithColor(0.0f);
            auto pink = renderWithColor(1.0f);
            auto brown = renderWithColor(2.0f);

            const auto numSamples = white.getNumSamples();
            const auto whiteRms = rms(white.getReadPointer(0), numSamples);
            const auto pinkDiff = rmsOfDifference(white.getReadPointer(0), pink.getReadPointer(0), numSamples);
            const auto brownDiff = rmsOfDifference(white.getReadPointer(0), brown.getReadPointer(0), numSamples);

            logMessage("Noise Color (bowed) - White RMS: " + juce::String(whiteRms, 6)
                       + ", Pink diff RMS: " + juce::String(pinkDiff, 6)
                       + ", Brown diff RMS: " + juce::String(brownDiff, 6));

            expect(pinkDiff > whiteRms * 0.02f, "Pink should measurably change a bowed note's output too");
            expect(brownDiff > whiteRms * 0.02f, "Brown should measurably change a bowed note's output too");
        }

        beginTest("Noise Color = Pink/Brown stay finite and bounded at the worst-case combination (max Decay, full Bow)");
        {
            for (float color : { 0.0f, 1.0f, 2.0f })
            {
                KarplunkAudioProcessor processor;
                processor.prepareToPlay(sampleRate, 512);
                setRaw(processor, KarplunkAudioProcessor::noiseColorParamID, color);
                setRaw(processor, KarplunkAudioProcessor::dampingParamID, 1.0f);
                setRaw(processor, KarplunkAudioProcessor::bowAmountParamID, 1.0f);
                juce::AudioBuffer<float> buffer(2, (int) (4.0 * sampleRate));
                auto midi = noteOnBuffer(60, 100);
                processor.processBlock(buffer, midi);

                const auto* data = buffer.getReadPointer(0);
                float peak = 0.0f;
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    expect(std::isfinite(data[i]), "output must stay finite with Noise Color=" + juce::String(color) + " at worst-case settings");
                    peak = std::max(peak, std::abs(data[i]));
                }
                expect(peak <= 2.5f, "peak output should stay within the same bound used elsewhere in this suite");
            }
        }
    }
};

static KarplunkProcessorTests karplunkProcessorTests;
