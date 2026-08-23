#include "../PluginProcessor.h"

#include <cmath>

// Every other Karplunk test (KarplunkTests) drives the isolated SingleLineKarplunkVoice DSP class
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
    }
};

static KarplunkProcessorTests karplunkProcessorTests;
