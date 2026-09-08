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

    // Phase 7's tests look machines up by a short, unique substring of their name rather than a
    // hardcoded table index, so they don't silently start testing the wrong machine if
    // ConcreteMachines.h's table order ever changes. Returns -1 if nothing matches.
    int machineTableIndexByName(const juce::String& nameFragment)
    {
        const auto& machines = getConcreteMachines();
        for (size_t i = 0; i < machines.size(); ++i)
            if (juce::String(machines[i].name).containsIgnoreCase(nameFragment))
                return (int) i;
        return -1;
    }

    // Phase 6's choke-group test needs two zones sharing a chokeGroup, which v1's UI has no way to
    // construct (see PluginProcessor.h's setRawSampleSetForTest() comment) - builds a mono sine
    // sourceBuffer directly rather than going through a temp WAV file/loadSample(), since these
    // zones are assembled by hand anyway.
    std::shared_ptr<const juce::AudioBuffer<float>> makeSineBuffer(double freqHz, double durationSeconds, double sampleRate)
    {
        const auto numSamples = (int) (durationSeconds * sampleRate);
        auto buffer = std::make_shared<juce::AudioBuffer<float>>(1, numSamples);
        auto* data = buffer->getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
            data[i] = 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate);
        return buffer;
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

        beginTest("The twelve machines are exposed as factory presets (Phase 7)");
        {
            ConcreteAudioProcessor processor;
            expectEquals(processor.getNumPrograms(), 12);
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

        beginTest("Voice Count limits real polyphony: 6 overlapping notes with a 4-voice limit "
                  "leaves exactly 4 sounding, stealing the oldest first (Phase 6)");
        {
            const auto file = writeTempSineWav(1000.0, 2.0);
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            expect(processor.loadSample(file));

            auto* voiceCount = processor.apvts.getParameter(ConcreteAudioProcessor::voiceCountParamID);
            voiceCount->setValueNotifyingHost(voiceCount->convertTo0to1(4.0f));

            // Distinct sample positions (not all exactly 0) so note-on order within the block is
            // unambiguous, regardless of how MidiBuffer breaks same-timestamp ties.
            juce::MidiBuffer sixNotesOn;
            for (int i = 0; i < 6; ++i)
                sixNotesOn.addEvent(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8) 100), i);

            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            processor.processBlock(buffer, sixNotesOn);

            expectEquals(processor.getNumActiveVoicesForTest(), 4,
                         "exactly the configured voice-count limit should end up sounding, not all 6 triggered");

            // Matches ConcreteVoiceAllocatorTests.cpp's own deterministic-stealing test: the first
            // two notes triggered (60, 61) are stolen from, in order, by the two that overflow the
            // limit (64, 65) - notes 62/63 are never touched.
            expect(!processor.isNoteSoundingForTest(60), "note 60 (triggered first) should have been stolen from");
            expect(!processor.isNoteSoundingForTest(61), "note 61 (triggered second) should have been stolen from");
            expect(processor.isNoteSoundingForTest(62), "note 62 was never stolen and should still be sounding");
            expect(processor.isNoteSoundingForTest(63), "note 63 was never stolen and should still be sounding");
            expect(processor.isNoteSoundingForTest(64), "note 64 (the first thief) should still be sounding");
            expect(processor.isNoteSoundingForTest(65), "note 65 (the second thief) should still be sounding");

            file.deleteFile();
        }

        beginTest("Regression: lowering Voice Count after voices already occupy high-index slots "
                  "must still let those slots be reclaimed (real reported bug: '1-4 chokes "
                  "properly, but after 5 it's as if I can play as many notes as I want')");
        {
            // Root cause: an earlier version of ConcreteVoiceAllocator::allocateVoiceForNoteOn()
            // restricted BOTH the free-slot search and the steal search to indices
            // [0, activeVoiceLimit) - once a voice landed in a high-index slot under a higher
            // limit (the default of 8, before Voice Count was turned down), lowering the limit
            // made that slot permanently unreachable by future stealing, so it kept sounding
            // forever no matter how many more notes were played. Fixed by comparing the total
            // active voice count against the limit and searching the WHOLE pool either way - see
            // ConcreteVoiceAllocator.h's own comment on allocateVoiceForNoteOn().
            const auto file = writeTempSineWav(1000.0, 4.0); // long enough that nothing naturally ends
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            expect(processor.loadSample(file));

            // Voice Count starts at its default (8) - fill every voice.
            juce::MidiBuffer eightNotesOn;
            for (int i = 0; i < 8; ++i)
                eightNotesOn.addEvent(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8) 100), i);

            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            processor.processBlock(buffer, eightNotesOn);
            expectEquals(processor.getNumActiveVoicesForTest(), 8, "all 8 default voices should be sounding");

            // The user turns Voice Count down to 5 mid-performance and keeps playing - one note
            // at a time, across separate blocks, the way a real keyboard would - triggering more
            // notes than the pool size so a correct implementation is guaranteed to have cycled
            // through every one of the original 8 voices at least once (see
            // ConcreteVoiceAllocatorTests.cpp's "repeated stealing cycles through voices" test for
            // why a strictly-increasing age counter guarantees that).
            auto* voiceCount = processor.apvts.getParameter(ConcreteAudioProcessor::voiceCountParamID);
            voiceCount->setValueNotifyingHost(voiceCount->convertTo0to1(5.0f));

            for (int i = 0; i < 12; ++i)
            {
                juce::MidiBuffer nextNoteOn;
                nextNoteOn.addEvent(juce::MidiMessage::noteOn(1, 100 + i, (juce::uint8) 100), 0);
                buffer.clear();
                processor.processBlock(buffer, nextNoteOn);
            }

            expect(!processor.isNoteSoundingForTest(65), "note 65 must eventually be reclaimed once Voice Count drops, wherever it landed");
            expect(!processor.isNoteSoundingForTest(66), "note 66 must eventually be reclaimed once Voice Count drops, wherever it landed");
            expect(!processor.isNoteSoundingForTest(67), "note 67 must eventually be reclaimed once Voice Count drops, wherever it landed");

            // Lowering the limit isn't retroactive (nothing here released a voice on its own) -
            // the fix's guarantee is reachability, not an instant forced cut, so the total must
            // simply never have grown past what was already sounding when the limit dropped.
            expect(processor.getNumActiveVoicesForTest() <= 8,
                   "total sounding voices must never exceed what was already active when the limit dropped");

            file.deleteFile();
        }

        beginTest("Velocity response: output level increases monotonically and roughly linearly with velocity (Phase 6)");
        {
            const auto file = writeTempSineWav(1000.0, 0.5);

            float previousPeak = -1.0f;
            for (const int velocity : { 1, 32, 64, 96, 127 })
            {
                ConcreteAudioProcessor processor;
                processor.prepareToPlay(44100.0, 512);
                expect(processor.loadSample(file));

                juce::AudioBuffer<float> buffer(2, 4096);
                buffer.clear();
                auto midi = noteOnBuffer(60, (juce::uint8) velocity);
                processor.processBlock(buffer, midi);

                // Past the 0.002s attack, well before any release (the note is never let go).
                const auto peak = buffer.getMagnitude(0, 2048, 2048);
                expect(peak > previousPeak, "output level must strictly increase as velocity increases");

                // Phase 1's flat linear velocityGain (see ConcreteVoice.cpp) means level should
                // track velocity/127 directly against the source file's own 0.5f amplitude.
                const auto expectedPeak = 0.5f * (float) velocity / 127.0f;
                expectWithinAbsoluteError(peak, expectedPeak, 0.02f,
                                           "velocity should map linearly onto output level");

                previousPeak = peak;
            }

            file.deleteFile();
        }

        beginTest("Choke groups: two zones sharing a chokeGroup, the second note-on cuts the first (Phase 6)");
        {
            // v1's UI can only ever load one zone spanning the whole keyboard, so a chokeGroup !=
            // 0 has to be constructed by hand - see setRawSampleSetForTest()'s own comment.
            ConcreteSampleZone zoneA;
            zoneA.sourceBuffer = makeSineBuffer(1000.0, 1.0, 44100.0);
            zoneA.sourceSampleRate = 44100.0;
            zoneA.rootNote = 60;
            zoneA.keyLo = 0;
            zoneA.keyHi = 63;
            zoneA.start = 0;
            zoneA.end = zoneA.sourceBuffer->getNumSamples();
            zoneA.chokeGroup = 5;

            ConcreteSampleZone zoneB = zoneA; // same content/choke group, different key range/root
            zoneB.rootNote = 90;
            zoneB.keyLo = 64;
            zoneB.keyHi = 127;

            ConcreteSampleSet::Ptr set(new ConcreteSampleSet());
            set->zones.push_back(zoneA);
            set->zones.push_back(zoneB);

            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            processor.setRawSampleSetForTest(set);

            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            auto noteOnA = noteOnBuffer(60); // zone A
            processor.processBlock(buffer, noteOnA);
            expect(processor.isNoteSoundingForTest(60), "zone A's note should be sounding right after its own note-on");

            buffer.clear();
            auto noteOnB = noteOnBuffer(90); // zone B, same choke group as zone A
            processor.processBlock(buffer, noteOnB);
            expect(!processor.isNoteSoundingForTest(60),
                   "note 60's voice should have been choked by the second note-on sharing its choke group");
            expect(processor.isNoteSoundingForTest(90), "the second note-on itself should be sounding");
        }

        beginTest("Selecting a machine applies its full parameter set (Phase 7)");
        {
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);

            const auto sk1Index = machineTableIndexByName("SK-1");
            expect(sk1Index >= 0, "the Casio SK-1 should be one of the twelve machines");

            auto* machine = processor.apvts.getParameter(ConcreteAudioProcessor::machineParamID);
            machine->setValueNotifyingHost(machine->convertTo0to1((float) (sk1Index + 1)));

            const auto& sk1 = getConcreteMachines()[(size_t) sk1Index];
            expectEquals((int) std::lround(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::bitDepthParamID)->load()),
                         sk1.bitDepthBits);
            expectWithinAbsoluteError(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::baseRateParamID)->load(),
                                       sk1.baseRateHz, 0.01f);
            expectEquals((int) std::lround(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::quantizerModeParamID)->load()),
                         (int) sk1.quantizerMode);
            expectWithinAbsoluteError(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::captureTransposeParamID)->load(),
                                       sk1.captureTransposeSemitones, 0.01f);
            expect(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::captureBypassParamID)->load() >= 0.5f,
                   "every machine ships with the capture pass off");
            expectEquals((int) std::lround(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::filterModelParamID)->load()),
                         (int) sk1.filterModel);
            expectEquals((int) std::lround(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::voiceCountParamID)->load()),
                         sk1.voiceCount);
            expectEquals((int) std::lround(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::ampEnvelopeModeParamID)->load()),
                         (int) sk1.ampEnvelopeMode);
        }

        beginTest("Switching machines does not touch the loaded sample (Phase 7 Architecture requirement)");
        {
            const auto file = writeTempSineWav(1000.0, 0.5);
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            expect(processor.loadSample(file));
            processor.setRootNoteForZone(0, 72);
            processor.setOneShotForZone(0, true);

            const auto beforeSourceBuffer = processor.getRawSampleSet()->zones[0].sourceBuffer;
            const auto beforeRootNote = processor.getRawSampleSet()->zones[0].rootNote;
            const auto beforeOneShot = processor.getRawSampleSet()->zones[0].oneShot;

            const auto k250Index = machineTableIndexByName("K250");
            auto* machine = processor.apvts.getParameter(ConcreteAudioProcessor::machineParamID);
            machine->setValueNotifyingHost(machine->convertTo0to1((float) (k250Index + 1)));

            expect(processor.getRawSampleSet()->zones[0].sourceBuffer == beforeSourceBuffer,
                   "selecting a machine must not touch the loaded sample's audio");
            expectEquals(processor.getRawSampleSet()->zones[0].rootNote, beforeRootNote,
                         "selecting a machine must not touch zone-list state like root note");
            expect(processor.getRawSampleSet()->zones[0].oneShot == beforeOneShot,
                   "selecting a machine must not touch zone-list state like one-shot");

            file.deleteFile();
        }

        beginTest("Host program list and the in-plugin Machine parameter select the same twelve, in sync (Phase 7)");
        {
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);

            expectEquals(processor.getNumPrograms(), 12);

            const auto mirageIndex = machineTableIndexByName("Mirage");
            expect(mirageIndex >= 0, "the Ensoniq Mirage should be one of the twelve machines");
            expectEquals(processor.getProgramName(mirageIndex), juce::String(getConcreteMachines()[(size_t) mirageIndex].name));

            processor.setCurrentProgram(mirageIndex);

            expectEquals(processor.getCurrentProgram(), mirageIndex,
                         "setCurrentProgram() should update getCurrentProgram() to match");
            expectEquals((int) std::lround(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::machineParamID)->load()),
                         mirageIndex + 1,
                         "picking a host program should move the in-plugin Machine parameter to match");

            const auto& mirage = getConcreteMachines()[(size_t) mirageIndex];
            expectEquals((int) std::lround(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::filterModelParamID)->load()),
                         (int) mirage.filterModel,
                         "setCurrentProgram() should apply the same values selecting the Machine parameter directly would");
        }

        beginTest("Machine index 0, \"(Custom)\", is a genuine no-op (Phase 7)");
        {
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);

            auto* filterModel = processor.apvts.getParameter(ConcreteAudioProcessor::filterModelParamID);
            filterModel->setValueNotifyingHost(filterModel->convertTo0to1(3.0f)); // CEM Compensated - a deliberately non-default value

            auto* machine = processor.apvts.getParameter(ConcreteAudioProcessor::machineParamID);
            machine->setValueNotifyingHost(machine->convertTo0to1(0.0f)); // "(Custom)"

            expectEquals((int) std::lround(processor.apvts.getRawParameterValue(ConcreteAudioProcessor::filterModelParamID)->load()), 3,
                         "selecting \"(Custom)\" must not reset or otherwise touch any other parameter");
        }

        beginTest("Regression: a customization made after selecting a machine survives a session "
                  "round-trip, rather than being clobbered back to the machine's own defaults (Phase 7)");
        {
            // Real risk this guards against: AudioProcessorValueTreeState::replaceState() can fire
            // parameterChanged() for every parameter whose value differs from what's currently
            // held, INCLUDING machineParamID if the restored session had a machine selected -
            // machineParamID's listener applies a whole machine's worth of values on change (see
            // ConcreteAudioProcessor::applyMachine()), so left registered during a restore, it
            // would silently overwrite every other just-restored parameter (including anything the
            // user had deliberately customized past the machine's own starting point) with that
            // machine's canned values instead of the session's actual saved ones. Fixed by
            // unregistering that one listener for the duration of replaceState() - see
            // setStateInformation()'s own comment.
            ConcreteAudioProcessor original;
            original.prepareToPlay(44100.0, 512);

            const auto mpc60Index = machineTableIndexByName("MPC60");
            auto* machine = original.apvts.getParameter(ConcreteAudioProcessor::machineParamID);
            machine->setValueNotifyingHost(machine->convertTo0to1((float) (mpc60Index + 1)));

            const auto& mpc60 = getConcreteMachines()[(size_t) mpc60Index];
            auto* bitDepth = original.apvts.getParameter(ConcreteAudioProcessor::bitDepthParamID);
            const auto customizedBitDepth = mpc60.bitDepthBits == 16 ? 10 : 16; // deliberately different from the machine's own value
            bitDepth->setValueNotifyingHost(bitDepth->convertTo0to1((float) customizedBitDepth));

            juce::MemoryBlock state;
            original.getStateInformation(state);

            ConcreteAudioProcessor reloaded;
            reloaded.setStateInformation(state.getData(), (int) state.getSize());

            expectEquals((int) std::lround(reloaded.apvts.getRawParameterValue(ConcreteAudioProcessor::bitDepthParamID)->load()),
                         customizedBitDepth,
                         "the customization made after selecting the machine must survive the round-trip");
            expectEquals((int) std::lround(reloaded.apvts.getRawParameterValue(ConcreteAudioProcessor::machineParamID)->load()),
                         mpc60Index + 1,
                         "the restored Machine selection itself should still round-trip correctly");
        }
    }
};

static ConcreteProcessorTests concreteProcessorTests;
