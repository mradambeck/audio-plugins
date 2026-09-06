#include "../ConcreteVoice.h"

#include <juce_core/juce_core.h>

#include <cmath>

// Exercises ConcreteVoice against hand-built synthetic buffers (no file I/O - see CMakeLists.txt's
// comment on why this belongs in ConcreteTests rather than needing ConcreteProcessorTests), which
// is also the C++-level version of Phase 1's own Analysis bullets (1kHz at root, octave up/down,
// the four mono/stereo source-vs-output combinations) - the Python/ConcreteRenderIR-based checks
// in analysis/ cover the same ground end to end through the real plugin.
namespace
{
    constexpr double sampleRate = 44100.0;

    ConcreteSampleSet::Ptr makeSetWithSineZone(double freqHz, int numChannels, int rootNote = 60,
                                                double durationSeconds = 0.5,
                                                double zoneSourceSampleRate = sampleRate)
    {
        auto* set = new ConcreteSampleSet();
        // Generated at the ZONE's own claimed source rate, not the fixed playback `sampleRate`
        // constant - otherwise a test that sets zoneSourceSampleRate != sampleRate to simulate a
        // file recorded at a different rate would build a buffer whose actual frequency content
        // doesn't match freqHz in the first place, independent of anything ConcreteVoice does.
        const auto numSamples = (int) (durationSeconds * zoneSourceSampleRate);

        auto buffer = std::make_shared<juce::AudioBuffer<float>>(numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer->getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / zoneSourceSampleRate);
        }

        ConcreteSampleZone zone;
        zone.buffer = buffer;
        zone.rootNote = rootNote;
        zone.start = 0;
        zone.end = numSamples;
        zone.sourceSampleRate = zoneSourceSampleRate;
        set->zones.push_back(zone);

        return set;
    }

    // Autocorrelation-based period estimate over a window, same technique
    // strike-synth/Source/Tests/StrikeProcessorTests.cpp uses - defined locally here since it's
    // test-only tooling, not production code.
    float estimateFrequencyHz(const float* data, int windowSize, float expectedHz, int searchRadius = 20)
    {
        const auto expectedDelay = (float) (sampleRate / (double) expectedHz);
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
}

class ConcreteVoiceTests : public juce::UnitTest
{
public:
    ConcreteVoiceTests() : juce::UnitTest("ConcreteVoice", "Concrete") {}

    void runTest() override
    {
        beginTest("Playing at the root note reproduces the source pitch");
        {
            auto set = makeSetWithSineZone(1000.0, 1);
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            juce::AudioBuffer<float> out(1, 8192);
            out.clear();
            voice.renderNextBlock(out, 0, out.getNumSamples());

            const auto measured = estimateFrequencyHz(out.getReadPointer(0) + 2048, 4096, 1000.0f);
            expectWithinAbsoluteError(measured, 1000.0f, 5.0f);
        }

        beginTest("Playing one octave up doubles the pitch");
        {
            auto set = makeSetWithSineZone(1000.0, 1);
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 72, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f); // root + 12 semitones

            juce::AudioBuffer<float> out(1, 8192);
            out.clear();
            voice.renderNextBlock(out, 0, out.getNumSamples());

            const auto measured = estimateFrequencyHz(out.getReadPointer(0) + 2048, 4096, 2000.0f);
            expectWithinAbsoluteError(measured, 2000.0f, 10.0f);
        }

        beginTest("Playing one octave down halves the pitch");
        {
            auto set = makeSetWithSineZone(1000.0, 1);
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 48, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f); // root - 12 semitones

            juce::AudioBuffer<float> out(1, 16384);
            out.clear();
            voice.renderNextBlock(out, 0, out.getNumSamples());

            const auto measured = estimateFrequencyHz(out.getReadPointer(0) + 4096, 8192, 500.0f);
            expectWithinAbsoluteError(measured, 500.0f, 3.0f);
        }

        beginTest("A source recorded at a different rate than prepare() still plays at the correct pitch");
        {
            // Built as if the buffer were captured at 48kHz, but the voice is prepared at
            // 44.1kHz - without the sourceSampleRate compensation this would play back sharp.
            auto set = makeSetWithSineZone(1000.0, 1, 60, 0.5, 48000.0);

            ConcreteVoice voice;
            voice.prepare(sampleRate); // 44100
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, 48000.0, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            juce::AudioBuffer<float> out(1, 8192);
            out.clear();
            voice.renderNextBlock(out, 0, out.getNumSamples());

            const auto measured = estimateFrequencyHz(out.getReadPointer(0) + 2048, 4096, 1000.0f);
            expectWithinAbsoluteError(measured, 1000.0f, 5.0f,
                                       "sample-rate mismatch between source and host must be compensated for");
        }

        beginTest("Voice becomes inactive once it reaches the zone's end (one-shot, no loop)");
        {
            auto set = makeSetWithSineZone(1000.0, 1, 60, 0.05); // short: ~2205 samples
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            expect(voice.isActive(), "should be active immediately after startNote()");

            juce::AudioBuffer<float> out(1, 8192); // longer than the zone's own content
            out.clear();
            voice.renderNextBlock(out, 0, out.getNumSamples());

            expect(! voice.isActive(), "should have gone inactive after running past the zone's end");
        }

        beginTest("A mono zone replicates identically to every output channel");
        {
            auto set = makeSetWithSineZone(1000.0, 1);
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            juce::AudioBuffer<float> out(2, 512);
            out.clear();
            voice.renderNextBlock(out, 0, out.getNumSamples());

            for (int i = 0; i < out.getNumSamples(); ++i)
                expectWithinAbsoluteError(out.getSample(0, i), out.getSample(1, i), 1.0e-6f,
                                           "a mono zone must produce identical L/R output");
        }

        beginTest("A stereo zone reads L/R independently into a stereo output (no channel dropped)");
        {
            auto* rawSet = new ConcreteSampleSet();
            auto buffer = std::make_shared<juce::AudioBuffer<float>>(2, 4410);
            for (int i = 0; i < buffer->getNumSamples(); ++i)
            {
                buffer->setSample(0, i, 0.8f);  // constant left
                buffer->setSample(1, i, -0.8f); // constant right, opposite sign
            }
            ConcreteSampleZone zone;
            zone.buffer = buffer;
            zone.end = buffer->getNumSamples();
            zone.sourceSampleRate = sampleRate;
            rawSet->zones.push_back(zone);
            ConcreteSampleSet::Ptr set(rawSet);

            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            juce::AudioBuffer<float> out(2, 256);
            out.clear();
            voice.renderNextBlock(out, 0, out.getNumSamples());

            // Envelope attack is only 0.002s, well under 256 samples at 44.1kHz, but check near
            // the end of the block to be clear of any residual attack ramp.
            expect(out.getSample(0, 200) > 0.5f, "left output should reflect the zone's left channel");
            expect(out.getSample(1, 200) < -0.5f, "right output should reflect the zone's right channel, not be dropped");
        }

        beginTest("A stereo zone downmixes to mono by averaging, not by dropping the right channel");
        {
            auto* rawSet = new ConcreteSampleSet();
            auto buffer = std::make_shared<juce::AudioBuffer<float>>(2, 4410);
            for (int i = 0; i < buffer->getNumSamples(); ++i)
            {
                buffer->setSample(0, i, 1.0f);
                buffer->setSample(1, i, -1.0f);
            }
            ConcreteSampleZone zone;
            zone.buffer = buffer;
            zone.end = buffer->getNumSamples();
            zone.sourceSampleRate = sampleRate;
            rawSet->zones.push_back(zone);
            ConcreteSampleSet::Ptr set(rawSet);

            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            juce::AudioBuffer<float> out(1, 256);
            out.clear();
            voice.renderNextBlock(out, 0, out.getNumSamples());

            // If the right channel were simply dropped (reading only channel 0), this would read
            // ~1.0f instead - the averaged mixdown must read close to 0.
            expectWithinAbsoluteError(out.getSample(0, 200), 0.0f, 0.05f,
                                       "mono downmix of +1/-1 stereo content must average toward silence, not drop a channel");
        }

        beginTest("renderNextBlock() adds to existing buffer content rather than overwriting it");
        {
            auto set = makeSetWithSineZone(1000.0, 1);
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            juce::AudioBuffer<float> out(1, 128);
            out.clear();
            for (int i = 0; i < out.getNumSamples(); ++i)
                out.setSample(0, i, 0.25f);

            voice.renderNextBlock(out, 0, out.getNumSamples());

            bool anyAboveFloor = false;
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (out.getSample(0, i) > 0.25f + 1.0e-6f)
                    anyAboveFloor = true;
            expect(anyAboveFloor, "voice output should have been added on top of the pre-existing 0.25f, not replaced it");
        }

        beginTest("stopNote(false) silences immediately");
        {
            auto set = makeSetWithSineZone(1000.0, 1);
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);
            voice.stopNote(false, true);

            expect(! voice.isActive(), "a hard stop should be immediate");
        }

        beginTest("stopNote(true) lets the release tail play before going inactive");
        {
            auto set = makeSetWithSineZone(1000.0, 1, 60, 1.0); // long enough to reach sustain
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            juce::AudioBuffer<float> warmup(1, 4096); // reach sustain, well past the 0.002s attack
            warmup.clear();
            voice.renderNextBlock(warmup, 0, warmup.getNumSamples());
            expect(voice.isActive(), "should still be active after reaching sustain");

            voice.stopNote(true, true);
            expect(voice.isActive(), "should still be active immediately at the start of the release tail");

            // Default release is 0.05s (~2205 samples) - render less than that and confirm it's
            // still going, then render past it and confirm it has finished.
            juce::AudioBuffer<float> midRelease(1, 1000);
            midRelease.clear();
            voice.renderNextBlock(midRelease, 0, midRelease.getNumSamples());
            expect(voice.isActive(), "should still be releasing partway through the release time");

            juce::AudioBuffer<float> pastRelease(1, 4000);
            pastRelease.clear();
            voice.renderNextBlock(pastRelease, 0, pastRelease.getNumSamples());
            expect(! voice.isActive(), "should have finished releasing by now");
        }

        beginTest("A one-shot zone ignores an ordinary note-off and keeps playing to its own end");
        {
            auto set = makeSetWithSineZone(1000.0, 1, 60, 0.5);
            set->zones[0].oneShot = true;
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            juce::AudioBuffer<float> warmup(1, 4096);
            warmup.clear();
            voice.renderNextBlock(warmup, 0, warmup.getNumSamples());
            expect(voice.isActive(), "should still be active after reaching sustain");

            // isForced=false, exactly what an ordinary MIDI note-off passes.
            voice.stopNote(true, false);
            expect(voice.isActive(), "an ordinary note-off on a one-shot zone must be a complete no-op");

            // Render well past where a NON-one-shot voice's release tail would have finished -
            // a one-shot voice must still be going, since it never even started releasing.
            juce::AudioBuffer<float> pastWhereReleaseWouldEnd(1, 8000);
            pastWhereReleaseWouldEnd.clear();
            voice.renderNextBlock(pastWhereReleaseWouldEnd, 0, pastWhereReleaseWouldEnd.getNumSamples());
            expect(voice.isActive(), "a one-shot voice must keep playing long after an ordinary note-off, "
                                       "unaffected by the amp envelope's own release time");

            // Now run it all the way to the zone's own end (0.5s = 22050 samples; ~12096 samples
            // consumed by the renders above) and confirm it finishes there, on its own.
            juce::AudioBuffer<float> toTheEnd(1, 20000);
            toTheEnd.clear();
            voice.renderNextBlock(toTheEnd, 0, toTheEnd.getNumSamples());
            expect(! voice.isActive(), "a one-shot voice must still deactivate once it reaches the zone's own end");
        }

        beginTest("A one-shot zone still responds to a forced stop (voice stealing / a choke group)");
        {
            auto set = makeSetWithSineZone(1000.0, 1, 60, 0.5);
            set->zones[0].oneShot = true;
            ConcreteVoice voice;
            voice.prepare(sampleRate);
            voice.startNote(set, 0, 60, 1.0f, ConcretePitchEngine::Mode::reference, sampleRate, 0, 0.0f, true, ConcreteFilterModel::Mode::bypass, 20000.0f, 0.0f, 0.0f, 0.0f);

            juce::AudioBuffer<float> warmup(1, 4096);
            warmup.clear();
            voice.renderNextBlock(warmup, 0, warmup.getNumSamples());

            voice.stopNote(false, true); // isForced=true, exactly what a choke group / voice steal passes
            expect(! voice.isActive(), "a forced stop must cut a one-shot voice immediately, same as any other voice");
        }
    }
};

static ConcreteVoiceTests concreteVoiceTests;
