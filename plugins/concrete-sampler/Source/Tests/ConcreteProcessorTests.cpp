#include "../PluginProcessor.h"

#include <atomic>
#include <cmath>
#include <map>
#include <thread>

// Drives the real ConcreteAudioProcessor - the exact class the plugin ships - through
// prepareToPlay()/processBlock()/getStateInformation()/loadSample(), the same path a host and its
// user actually take. Phase 0's tests lock down the scaffold (silent output with nothing loaded,
// the mono/stereo bus decision, a working state round-trip with zero parameters); Phase 1 adds
// real file loading and playback on top, including the parts of Architecture #2 (embed cap,
// override, missing-file handling) that only mean something end-to-end through the real
// processor, not in isolation (see ConcreteSampleIOTests.cpp for the isolated module tests).
namespace
{
    juce::File writeTempSineWav(double freqHz, double durationSeconds, double sampleRate = 44100.0,
                                 int numChannels = 1)
    {
        auto file = juce::File::createTempFile(".wav");
        const auto numSamples = (int) (durationSeconds * sampleRate);

        juce::AudioBuffer<float> buffer(numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate);
        }

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(stream.get(), sampleRate, (unsigned int) numChannels, 16, {}, 0));
        stream.release();
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
        writer.reset();

        return file;
    }

    juce::MidiBuffer noteOnBuffer(int note, juce::uint8 velocity = 100)
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, velocity), 0);
        return midi;
    }

    // Autocorrelation-based period estimate, same technique as ConcreteVoiceTests.cpp and every
    // other synth's processor-level tests in this catalog (e.g. StrikeProcessorTests.cpp).
    float estimateFrequencyHz(const float* data, int windowSize, double sampleRate, float expectedHz, int searchRadius = 20)
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

    float maxAbsDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float worst = 0.0f;
        const auto channels = std::min(a.getNumChannels(), b.getNumChannels());
        const auto samples = std::min(a.getNumSamples(), b.getNumSamples());
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < samples; ++i)
                worst = std::max(worst, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
        return worst;
    }
}
class ConcreteProcessorTests : public juce::UnitTest
{
public:
    ConcreteProcessorTests() : juce::UnitTest("ConcreteProcessor", "Concrete") {}

    void runTest() override
    {
        beginTest("Mono and stereo main output are both accepted");
        {
            ConcreteAudioProcessor processor;
            using Layout = juce::AudioProcessor::BusesLayout;

            Layout mono;
            mono.outputBuses.add(juce::AudioChannelSet::mono());
            expect(processor.isBusesLayoutSupported(mono), "mono main output should be supported");

            Layout stereo;
            stereo.outputBuses.add(juce::AudioChannelSet::stereo());
            expect(processor.isBusesLayoutSupported(stereo), "stereo main output should be supported");
        }

        beginTest("Other main output layouts are rejected (main stereo only - see Architecture #1)");
        {
            ConcreteAudioProcessor processor;
            using Layout = juce::AudioProcessor::BusesLayout;

            Layout quad;
            quad.outputBuses.add(juce::AudioChannelSet::quadraphonic());
            expect(! processor.isBusesLayoutSupported(quad), "quadraphonic main output should be rejected");

            Layout disabled;
            disabled.outputBuses.add(juce::AudioChannelSet::disabled());
            expect(! processor.isBusesLayoutSupported(disabled), "a disabled main output should be rejected");
        }

        beginTest("processBlock produces silence with no sample loaded, even with MIDI in");
        {
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);

            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            // Fill with non-zero content first so a real clear(), not a no-op, is what's verified.
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), 1.0f, buffer.getNumSamples());

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);

            processor.processBlock(buffer, midi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    expectEquals(buffer.getSample(ch, i), 0.0f, "output must be exact silence in Phase 0");

            processor.releaseResources();
        }

        beginTest("getStateInformation()/setStateInformation() round-trip without corrupting state");
        {
            ConcreteAudioProcessor processor;

            juce::MemoryBlock block;
            processor.getStateInformation(block);
            expect(block.getSize() > 0, "state XML should be non-empty even with zero parameters");

            ConcreteAudioProcessor other;
            other.setStateInformation(block.getData(), (int) block.getSize());
            expect(other.apvts.state.getType() == processor.apvts.state.getType(),
                   "reloaded state should keep the same ValueTree type");
        }

        beginTest("No factory presets exist yet (Phase 7 defines the twelve machines)");
        {
            ConcreteAudioProcessor processor;
            expectEquals(processor.getNumPrograms(), 0);
        }

        beginTest("loadSample() loads a real file and playing at root reproduces its pitch");
        {
            const auto file = writeTempSineWav(1000.0, 1.0);
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            expect(processor.loadSample(file));

            juce::AudioBuffer<float> buffer(2, 8192);
            buffer.clear();
            auto midi = noteOnBuffer(60);
            processor.processBlock(buffer, midi);

            const auto measured = estimateFrequencyHz(buffer.getReadPointer(0) + 2048, 4096, 44100.0, 1000.0f);
            expectWithinAbsoluteError(measured, 1000.0f, 5.0f);

            file.deleteFile();
        }

        beginTest("Playing a transposed note pitches the sample up/down correctly");
        {
            const auto file = writeTempSineWav(1000.0, 1.0);
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            processor.loadSample(file);

            {
                juce::AudioBuffer<float> buffer(2, 8192);
                buffer.clear();
                auto midi = noteOnBuffer(72); // +12 semitones
                processor.processBlock(buffer, midi);
                const auto measured = estimateFrequencyHz(buffer.getReadPointer(0) + 2048, 4096, 44100.0, 2000.0f);
                expectWithinAbsoluteError(measured, 2000.0f, 10.0f, "an octave up should double the frequency");
            }

            file.deleteFile();
        }

        beginTest("Mono and stereo sources render correctly through both mono and stereo output buses");
        {
            const auto monoFile = writeTempSineWav(1000.0, 0.3, 44100.0, 1);
            const auto stereoFile = writeTempSineWav(1000.0, 0.3, 44100.0, 2);

            for (const auto* file : { &monoFile, &stereoFile })
            {
                for (const int outChannels : { 1, 2 })
                {
                    ConcreteAudioProcessor processor;
                    processor.prepareToPlay(44100.0, 512);
                    expect(processor.loadSample(*file));

                    juce::AudioBuffer<float> buffer(outChannels, 4096);
                    buffer.clear();
                    auto midi = noteOnBuffer(60);
                    processor.processBlock(buffer, midi);

                    float energy = 0.0f;
                    for (int ch = 0; ch < outChannels; ++ch)
                        for (int i = 0; i < buffer.getNumSamples(); ++i)
                            energy += std::abs(buffer.getSample(ch, i));
                    expect(energy > 0.0f, "every source/output channel-count combination must produce audible output");
                }
            }

            monoFile.deleteFile();
            stereoFile.deleteFile();
        }

        beginTest("Loading a new sample mid-note doesn't disturb the voice already playing (Architecture #2)");
        {
            const auto firstFile = writeTempSineWav(1000.0, 2.0);
            const auto secondFile = writeTempSineWav(3000.0, 2.0);

            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            processor.loadSample(firstFile);

            juce::AudioBuffer<float> firstHalf(2, 4096);
            firstHalf.clear();
            auto midi = noteOnBuffer(60);
            processor.processBlock(firstHalf, midi);

            // Load a completely different sample while the first note is still sounding.
            processor.loadSample(secondFile);

            juce::AudioBuffer<float> secondHalf(2, 8192);
            secondHalf.clear();
            juce::MidiBuffer noMidi;
            processor.processBlock(secondHalf, noMidi);

            // The already-sounding voice must keep playing the FIRST file's 1kHz content, not
            // jump to the second file's 3kHz content or glitch/produce NaNs.
            const auto measured = estimateFrequencyHz(secondHalf.getReadPointer(0) + 2048, 4096, 44100.0, 1000.0f);
            expectWithinAbsoluteError(measured, 1000.0f, 10.0f,
                                       "an in-flight voice must finish on the sample data it started on");

            for (int i = 0; i < secondHalf.getNumSamples(); ++i)
                expect(std::isfinite(secondHalf.getSample(0, i)), "no NaN/inf should appear from the mid-note reload");

            firstFile.deleteFile();
            secondFile.deleteFile();
        }

        beginTest("setRootNoteForZone() changes which note plays at the source pitch");
        {
            const auto file = writeTempSineWav(1000.0, 1.0);
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            processor.loadSample(file);
            processor.setRootNoteForZone(0, 72); // the sample's natural pitch now plays at note 72

            juce::AudioBuffer<float> buffer(2, 8192);
            buffer.clear();
            auto midi = noteOnBuffer(72);
            processor.processBlock(buffer, midi);

            const auto measured = estimateFrequencyHz(buffer.getReadPointer(0) + 2048, 4096, 44100.0, 1000.0f);
            expectWithinAbsoluteError(measured, 1000.0f, 5.0f, "note 72 should now reproduce the source pitch directly");

            file.deleteFile();
        }

        beginTest("Save state, reload into a fresh processor, render again: numerically identical output");
        {
            const auto file = writeTempSineWav(1234.0, 0.5);
            ConcreteAudioProcessor original;
            original.prepareToPlay(44100.0, 512);
            original.loadSample(file);

            juce::MemoryBlock state;
            original.getStateInformation(state);

            ConcreteAudioProcessor reloaded;
            reloaded.prepareToPlay(44100.0, 512);
            reloaded.setStateInformation(state.getData(), (int) state.getSize());

            juce::AudioBuffer<float> bufferA(2, 4096);
            bufferA.clear();
            auto midiA = noteOnBuffer(60);
            original.processBlock(bufferA, midiA);

            juce::AudioBuffer<float> bufferB(2, 4096);
            bufferB.clear();
            auto midiB = noteOnBuffer(60);
            reloaded.processBlock(bufferB, midiB);

            // Not asserted bit-exact: the sample is small enough to auto-embed (Architecture #2's
            // default), and embedding round-trips through 24-bit FLAC rather than the original
            // 16-bit WAV bytes verbatim - functionally identical, but not guaranteed to be
            // floating-point bit-for-bit against a lossy-at-a-given-depth codec's own rounding.
            expect(maxAbsDifference(bufferA, bufferB) < 1.0e-4f,
                   "a reloaded session should render essentially identically to the original");

            file.deleteFile();
        }

        beginTest("Embed override round-trips through save/reload");
        {
            ConcreteAudioProcessor processor;
            expect(!processor.getEmbedSamplesOverride(), "default should be off");
            processor.setEmbedSamplesOverride(true);

            juce::MemoryBlock state;
            processor.getStateInformation(state);

            ConcreteAudioProcessor reloaded;
            reloaded.setStateInformation(state.getData(), (int) state.getSize());
            expect(reloaded.getEmbedSamplesOverride(), "the override preference should persist across save/reload");
        }

        beginTest("A moved source file with no embedded copy reloads as missing, and relocateZone() recovers it");
        {
            // Load a file large enough (long/loud enough for its 16-bit content, at a rate FLAC
            // supports) is not what forces path-only here - instead, force it directly by
            // toggling the override off and simulating the file having moved, matching
            // ConcreteSampleIOTests.cpp's same approach for the same reason (small test WAVs
            // auto-embed by design, so exercising "missing" requires stripping that deliberately).
            const auto file = writeTempSineWav(900.0, 0.2);
            ConcreteAudioProcessor processor;
            processor.loadSample(file);

            juce::MemoryBlock state;
            processor.getStateInformation(state);

            // Strip the embedded copy out of the saved state so reload must depend on the path.
            // getStateInformation()'s output is AudioProcessor's own binary-wrapped XML format
            // (an 8-byte header, then UTF8 XML text), not raw parseable XML - getXmlFromBinary()
            // is the correct inverse (this is exactly what setStateInformation() itself uses).
            // A binary-valued ValueTree property is written to XML with a "base64:" prefix on its
            // attribute name (see juce_NamedValueSet.cpp), which is also how ValueTree::fromXml()
            // recognises it should decode back to a binary var rather than a plain string.
            auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
            auto* zonesXml = xml->getChildByName(ConcreteZoneIDs::zones.toString());
            zonesXml->getChildElement(0)->removeAttribute("base64:" + ConcreteZoneIDs::embeddedAudio.toString());
            juce::MemoryBlock strippedState;
            juce::AudioProcessor::copyXmlToBinary(*xml, strippedState);

            file.deleteFile(); // now simulate the file having moved before the session is reopened

            ConcreteAudioProcessor reloaded;
            reloaded.setStateInformation(strippedState.getData(), (int) strippedState.getSize());
            auto missingSet = reloaded.getCurrentSampleSet();
            expect(missingSet->zones[0].sourceMissing, "a moved file with no embedded copy must reload as missing");

            const auto newFile = writeTempSineWav(1100.0, 0.2);
            expect(reloaded.relocateZone(0, newFile), "relocating to a real file should succeed");
            auto relocatedSet = reloaded.getCurrentSampleSet();
            expect(!relocatedSet->zones[0].sourceMissing, "after relocating, the zone should no longer be missing");

            newFile.deleteFile();
        }

        beginTest("Hammering capture-pass parameter changes on another thread while 8 voices are actively "
                  "rendering produces no crashes, no NaN/inf output, and no audio-thread allocation bugs "
                  "(see the plan's Phase 4 thread-safety Analysis - build with -fsanitize=thread to catch "
                  "genuine data races, which this test alone cannot detect)");
        {
            const auto file = writeTempSineWav(500.0, 2.0);
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            expect(processor.loadSample(file), "test file must load for this stress test to mean anything");

            std::atomic<bool> keepGoing { true };
            std::thread paramThread([&]
            {
                juce::Random rng(1234);
                // Every parameter that's a re-bake trigger (see PluginProcessor.h's
                // parameterChanged() comment) - hammered simultaneously and as fast as possible,
                // which is the scenario the plan calls out ("automate capture transpose").
                const char* paramIDs[] = {
                    ConcreteAudioProcessor::bitDepthParamID,
                    ConcreteAudioProcessor::quantizerModeParamID,
                    ConcreteAudioProcessor::captureTransposeParamID,
                    ConcreteAudioProcessor::captureDriveParamID,
                    ConcreteAudioProcessor::captureBypassParamID,
                    ConcreteAudioProcessor::captureIterationsParamID,
                };
                while (keepGoing.load(std::memory_order_relaxed))
                    for (auto* paramID : paramIDs)
                        if (auto* param = processor.apvts.getParameter(paramID))
                            param->setValueNotifyingHost(rng.nextFloat());
            });

            juce::MidiBuffer chordOn;
            for (int n = 0; n < 8; ++n)
                chordOn.addEvent(juce::MidiMessage::noteOn(1, 60 + n, (juce::uint8) 100), 0);

            juce::AudioBuffer<float> block(2, 512);
            constexpr int numBlocks = 200; // ~2.3s at 512 samples/44.1kHz - close to the file's own 2s length
            for (int i = 0; i < numBlocks; ++i)
            {
                block.clear();
                auto midi = (i == 0) ? chordOn : juce::MidiBuffer();
                processor.processBlock(block, midi);

                for (int ch = 0; ch < block.getNumChannels(); ++ch)
                    for (int s = 0; s < block.getNumSamples(); ++s)
                        expect(std::isfinite(block.getSample(ch, s)),
                               "output must stay finite even while a re-bake races with playback");
            }

            keepGoing = false;
            paramThread.join();

            file.deleteFile();
        }

        beginTest("Repeated re-bakes with changing capture-pass settings don't compound-shrink the "
                  "zone (regression test for a real reported bug)");
        {
            // Bug: bakeZone() rescales start/end by (newWorkingLength / sourceLength) - re-baking
            // from the previously-BAKED result (whose start/end were already rescaled once) instead
            // of from a stable, never-rescaled raw zone list compounded that rescale on every
            // subsequent re-bake, shrinking the zone toward nothing after enough of them. Fixed by
            // always re-baking from rawSampleSet - see that member's own comment.
            const auto file = writeTempSineWav(500.0, 1.0);
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            processor.loadSample(file);

            auto* transpose = processor.apvts.getParameter(ConcreteAudioProcessor::captureTransposeParamID);
            auto* bypass = processor.apvts.getParameter(ConcreteAudioProcessor::captureBypassParamID);
            auto* iterations = processor.apvts.getParameter(ConcreteAudioProcessor::captureIterationsParamID);
            auto* drive = processor.apvts.getParameter(ConcreteAudioProcessor::captureDriveParamID);

            bypass->setValueNotifyingHost(0.0f); // engage the capture pass
            transpose->setValueNotifyingHost(transpose->convertTo0to1(5.0f)); // a fixed, non-zero transpose throughout
            processor.rebakeNow();

            const auto firstBake = processor.getCurrentSampleSet();
            const auto firstSpan = firstBake->zones[0].end - firstBake->zones[0].start;
            expect(firstSpan > 0, "the zone must have a real, non-empty span after the first bake");

            // Repeatedly nudge Capture Drive - which changes sample VALUES, not buffer LENGTH -
            // and force a re-bake each time, exactly like a user dragging that control around.
            // Transpose and iterations are unchanged throughout, so the resulting span must stay
            // the SAME every time, never shrinking further with each additional re-bake.
            for (int i = 0; i < 10; ++i)
            {
                drive->setValueNotifyingHost((float) i / 10.0f);
                processor.rebakeNow();

                const auto span = processor.getCurrentSampleSet()->zones[0].end - processor.getCurrentSampleSet()->zones[0].start;
                expect(span == firstSpan,
                       "the zone's span must not shrink (or grow) across repeated re-bakes at a fixed transpose/iterations");
            }

            // Capture Iterations legitimately changes the working buffer's length by design (more
            // iterations compounds the resample ratio - see ConcreteCapturePass.h) - cycling it
            // through several values and back should reproduce the EXACT SAME span each time a
            // given iteration count recurs, proving there's no hidden accumulation underneath that
            // legitimate, by-design length change.
            std::map<int, juce::int64> spanAtIterationCount;
            for (int cycle = 0; cycle < 3; ++cycle)
            {
                for (int iterationCount = 1; iterationCount <= 4; ++iterationCount)
                {
                    iterations->setValueNotifyingHost(iterations->convertTo0to1((float) iterationCount));
                    processor.rebakeNow();

                    const auto span = processor.getCurrentSampleSet()->zones[0].end - processor.getCurrentSampleSet()->zones[0].start;
                    if (spanAtIterationCount.count(iterationCount) == 0)
                        spanAtIterationCount[iterationCount] = span;
                    else
                        expectEquals((int) span, (int) spanAtIterationCount[iterationCount],
                                     "the span for a given iteration count must be identical every time it recurs, "
                                     "not shrink further on each pass through the cycle");
                }
            }

            file.deleteFile();
        }

        beginTest("Save/reload with an active (non-bypassed) capture pass round-trips the raw "
                  "zone's source-space start/end correctly (regression test for a related bug)");
        {
            // The companion bug to the one above: getStateInformation() used to persist the BAKED
            // zone's start/end (rescaled to the working buffer's length) alongside the embedded
            // SOURCE audio (always source-length) - on reload those two disagreed. Fixed by
            // persisting rawSampleSet (always source-space) instead - see getStateInformation()'s
            // own comment.
            const auto file = writeTempSineWav(500.0, 1.0);
            ConcreteAudioProcessor processor;
            processor.loadSample(file);

            const auto originalRawZone = processor.getRawSampleSet()->zones[0];

            auto* bypass = processor.apvts.getParameter(ConcreteAudioProcessor::captureBypassParamID);
            auto* transpose = processor.apvts.getParameter(ConcreteAudioProcessor::captureTransposeParamID);
            bypass->setValueNotifyingHost(0.0f);
            transpose->setValueNotifyingHost(transpose->convertTo0to1(12.0f)); // halves the working buffer's length
            processor.rebakeNow();

            expect(processor.getCurrentSampleSet()->zones[0].end < originalRawZone.end,
                   "sanity check: the baked zone really is shorter than the raw one at +12 semitones");

            juce::MemoryBlock state;
            processor.getStateInformation(state);

            ConcreteAudioProcessor reloaded;
            reloaded.setStateInformation(state.getData(), (int) state.getSize());

            const auto reloadedRawZone = reloaded.getRawSampleSet()->zones[0];
            expectEquals((int) reloadedRawZone.start, (int) originalRawZone.start,
                         "reloaded raw start must match the original source-space value, not the baked/rescaled one");
            expectEquals((int) reloadedRawZone.end, (int) originalRawZone.end,
                         "reloaded raw end must match the original source-space value, not the baked/rescaled one");

            file.deleteFile();
        }

        beginTest("A one-shot zone keeps sounding through the real MIDI dispatch path, well past a quick "
                  "key press and past where an ordinary release tail would have finished");
        {
            const auto file = writeTempSineWav(1000.0, 1.0); // 1 second - plenty long enough to still be sounding
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            expect(processor.loadSample(file));
            processor.setOneShotForZone(0, true);

            // A very quick key press: note-on at sample 0, note-off at sample 50, both in the same
            // block - about as short a "hold" as a real performance could produce.
            juce::MidiBuffer quickPress;
            quickPress.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            quickPress.addEvent(juce::MidiMessage::noteOff(1, 60), 50);

            juce::AudioBuffer<float> firstBlock(2, 512);
            firstBlock.clear();
            processor.processBlock(firstBlock, quickPress);

            // The default amp envelope release is 0.05s (~2205 samples) - render several blocks'
            // worth well past that (and past the first block above) with no further MIDI at all,
            // and confirm real audio is still coming out.
            float energyPastWhereReleaseWouldHaveEnded = 0.0f;
            juce::AudioBuffer<float> laterBlock(2, 512);
            juce::MidiBuffer noMidi;
            for (int block = 0; block < 8; ++block) // 8*512 = 4096 samples past the first block
            {
                laterBlock.clear();
                processor.processBlock(laterBlock, noMidi);
                for (int ch = 0; ch < laterBlock.getNumChannels(); ++ch)
                    for (int i = 0; i < laterBlock.getNumSamples(); ++i)
                        energyPastWhereReleaseWouldHaveEnded += std::abs(laterBlock.getSample(ch, i));
            }
            expect(energyPastWhereReleaseWouldHaveEnded > 0.0f,
                   "a one-shot zone must still be audibly playing long after such a short key press, "
                   "well past where the amp envelope's own release would have silenced a gated zone");

            file.deleteFile();
        }
    }
};

static ConcreteProcessorTests concreteProcessorTests;
