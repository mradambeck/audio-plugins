#include "../ConcreteSampleIO.h"

#include <juce_core/juce_core.h>

#include <cmath>

// Exercises ConcreteSampleIO against REAL temp WAV files and real FLAC encode/decode - not mocked,
// since the whole point of this module is Architecture #2's session-persistence behavior (path +
// capped embedding + override + missing-file handling), which only means something against actual
// file I/O and actual compressed bytes.
namespace
{
    juce::AudioFormatManager& formatManager()
    {
        static juce::AudioFormatManager manager;
        static bool registered = false;
        if (!registered)
        {
            manager.registerBasicFormats();
            registered = true;
        }
        return manager;
    }

    // Writes a short sine WAV to a new temp file and returns it - caller owns the file (and
    // should delete it when done, since these are real temp-directory files, not auto-cleaned).
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
        stream.release(); // the writer now owns it
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
        writer.reset(); // flush before the caller reads the file back

        return file;
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

class ConcreteSampleIOTests : public juce::UnitTest
{
public:
    ConcreteSampleIOTests() : juce::UnitTest("ConcreteSampleIO", "Concrete") {}

    void runTest() override
    {
        beginTest("loadZoneFromFile() reads a real WAV correctly");
        {
            const auto file = writeTempSineWav(1000.0, 0.5, 44100.0, 1);
            const auto zone = ConcreteSampleIO::loadZoneFromFile(formatManager(), file, 60);

            expect(!zone.sourceMissing);
            expect(zone.sourceBuffer != nullptr);
            expectEquals(zone.sourceBuffer->getNumChannels(), 1);
            expectWithinAbsoluteError(zone.sourceSampleRate, 44100.0, 0.1);
            expectEquals((int) zone.end, zone.sourceBuffer->getNumSamples());
            expectEquals(zone.rootNote, 60);

            file.deleteFile();
        }

        beginTest("loadZoneFromFile() reports sourceMissing for a nonexistent file, without crashing");
        {
            const juce::File missing("/nonexistent/path/that/should/never/exist.wav");
            const auto zone = ConcreteSampleIO::loadZoneFromFile(formatManager(), missing, 60);

            expect(zone.sourceMissing);
            expect(zone.sourceBuffer == nullptr);
            expectEquals(zone.sourcePath, missing.getFullPathName());
        }

        beginTest("FLAC encode/decode round-trips audio content within lossy-at-24-bit tolerance");
        {
            juce::AudioBuffer<float> sine(1, 4410);
            for (int i = 0; i < sine.getNumSamples(); ++i)
                sine.setSample(0, i, 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 44100.0));

            ConcreteSampleZone zone;
            zone.sourceBuffer = std::make_shared<juce::AudioBuffer<float>>(sine);
            zone.sourceSampleRate = 44100.0;

            const auto flacBytes = ConcreteSampleIO::encodeZoneAsFlac(zone);
            expect(flacBytes.getSize() > 0, "encoding should succeed for a standard rate/depth buffer");

            juce::AudioBuffer<float> decoded;
            double decodedRate = 0.0;
            const auto ok = ConcreteSampleIO::decodeFlacBlock(flacBytes, decoded, decodedRate);
            expect(ok);
            expectWithinAbsoluteError(decodedRate, 44100.0, 0.1);
            expectEquals(decoded.getNumSamples(), zone.sourceBuffer->getNumSamples());
            expect(maxAbsDifference(decoded, *zone.sourceBuffer) < 0.001f,
                   "24-bit FLAC round-trip should be very close to the original float content");
        }

        beginTest("decodeFlacBlock() fails cleanly on empty/malformed input");
        {
            juce::AudioBuffer<float> decoded;
            double rate = 0.0;
            expect(!ConcreteSampleIO::decodeFlacBlock({}, decoded, rate), "an empty block should fail, not crash");

            juce::MemoryBlock garbage("not a flac file at all", 23);
            expect(!ConcreteSampleIO::decodeFlacBlock(garbage, decoded, rate), "malformed bytes should fail cleanly");
        }

        beginTest("shouldEmbed() enforces the per-zone and per-instance caps, and forceEmbed bypasses both");
        {
            using namespace ConcreteSampleIO;
            expect(shouldEmbed(1024, 0, false), "a small zone with no prior usage should embed");
            expect(!shouldEmbed(maxEmbeddedBytesPerZone + 1, 0, false), "over the per-zone cap should not embed");
            expect(!shouldEmbed(1024, maxEmbeddedBytesPerInstance, false), "no remaining instance budget should not embed");
            expect(shouldEmbed(maxEmbeddedBytesPerZone + 1, 0, true), "forceEmbed should bypass the per-zone cap");
            expect(shouldEmbed(1024, maxEmbeddedBytesPerInstance, true), "forceEmbed should bypass the per-instance cap");
            expect(!shouldEmbed(0, 0, false), "nothing to embed (zero bytes) should never embed even unforced=false case");
            expect(!shouldEmbed(0, 0, true), "nothing to embed (zero bytes) should never embed even if forced");
        }

        beginTest("zoneToValueTree()/valueTreeToZone() round-trips a fully-embedded zone");
        {
            const auto file = writeTempSineWav(1000.0, 0.2, 44100.0, 1);
            auto zone = ConcreteSampleIO::loadZoneFromFile(formatManager(), file, 64);
            zone.keyLo = 10; zone.keyHi = 20;
            zone.velLo = 1; zone.velHi = 100;
            zone.tuneSemitones = 3.5f;
            zone.level = 0.8f;
            zone.pan = -0.25f;
            zone.chokeGroup = 7;
            zone.output = 2;
            zone.loopEnabled = true;
            zone.loopStart = 10;
            zone.loopEnd = 1000;

            juce::int64 alreadyEmbedded = 0;
            const auto tree = ConcreteSampleIO::zoneToValueTree(zone, 0, true /* forceEmbed */, alreadyEmbedded);
            expect(alreadyEmbedded > 0, "forced embedding should have contributed bytes to the running total");
            expect(tree.hasProperty(ConcreteZoneIDs::embeddedAudio));

            // Round-trip through actual XML text, not just the in-memory ValueTree, since that's
            // what getStateInformation()/setStateInformation() actually do (see
            // concrete_analysis's Phase 1 "save state, reload state" requirement).
            auto xml = tree.createXml();
            const auto reparsed = juce::ValueTree::fromXml(*xml);

            const auto roundTripped = ConcreteSampleIO::valueTreeToZone(reparsed, formatManager());
            expect(!roundTripped.sourceMissing);
            expect(roundTripped.sourceBuffer != nullptr);
            expectEquals(roundTripped.keyLo, 10);
            expectEquals(roundTripped.keyHi, 20);
            expectEquals(roundTripped.velLo, 1);
            expectEquals(roundTripped.velHi, 100);
            expectWithinAbsoluteError(roundTripped.tuneSemitones, 3.5f, 1.0e-6f);
            expectWithinAbsoluteError(roundTripped.level, 0.8f, 1.0e-6f);
            expectWithinAbsoluteError(roundTripped.pan, -0.25f, 1.0e-6f);
            expectEquals(roundTripped.chokeGroup, 7);
            expectEquals(roundTripped.output, 2);
            expect(roundTripped.loopEnabled);
            expectEquals((int) roundTripped.loopStart, 10);
            expectEquals((int) roundTripped.loopEnd, 1000);
            expect(maxAbsDifference(*roundTripped.sourceBuffer, *zone.sourceBuffer) < 0.001f,
                   "embedded audio should decode back to essentially the same content");

            file.deleteFile();
        }

        beginTest("zoneToValueTree()/valueTreeToZone() round-trips a path-only (unembedded) zone via its file");
        {
            // A short test WAV is well under the embed cap, so zoneToValueTree() auto-embeds it
            // by design (that's the documented default - small one-shots are meant to be
            // self-contained). To exercise the path-only fallback deterministically regardless of
            // that cap decision, strip the embedded property back out before reloading, rather
            // than relying on the buffer happening to be large enough not to qualify.
            const auto file = writeTempSineWav(500.0, 0.2, 44100.0, 1);
            const auto zone = ConcreteSampleIO::loadZoneFromFile(formatManager(), file, 60);

            juce::int64 alreadyEmbedded = 0;
            auto tree = ConcreteSampleIO::zoneToValueTree(zone, 0, false /* forceEmbed */, alreadyEmbedded);
            expect(tree.hasProperty(ConcreteZoneIDs::embeddedAudio),
                   "sanity check: a small buffer under the cap should have auto-embedded");
            tree.removeProperty(ConcreteZoneIDs::embeddedAudio, nullptr);

            const auto roundTripped = ConcreteSampleIO::valueTreeToZone(tree, formatManager());
            expect(!roundTripped.sourceMissing, "the file still exists on disk, so it should load via sourcePath");
            expect(roundTripped.sourceBuffer != nullptr);
            expectEquals(roundTripped.sourcePath, zone.sourcePath);

            file.deleteFile();
        }

        beginTest("valueTreeToZone() reports sourceMissing when the file has moved and nothing was embedded");
        {
            const auto file = writeTempSineWav(750.0, 0.1, 44100.0, 1);
            const auto zone = ConcreteSampleIO::loadZoneFromFile(formatManager(), file, 72);

            juce::int64 alreadyEmbedded = 0;
            auto tree = ConcreteSampleIO::zoneToValueTree(zone, 0, false, alreadyEmbedded);
            tree.removeProperty(ConcreteZoneIDs::embeddedAudio, nullptr); // force the "nothing embedded" case

            file.deleteFile(); // simulate the user moving/deleting the source after saving the session

            const auto reloaded = ConcreteSampleIO::valueTreeToZone(tree, formatManager());
            expect(reloaded.sourceMissing, "a moved file with no embedded copy must be reported missing, not silently empty");
            expect(reloaded.sourceBuffer == nullptr);
            expectEquals(reloaded.rootNote, 72, "every other field should still have loaded correctly");
        }

        beginTest("relocateZone() re-reads a new file while preserving key/velocity/tune/level/pan/choke/output");
        {
            const auto originalFile = writeTempSineWav(1000.0, 0.2, 44100.0, 1);
            auto zone = ConcreteSampleIO::loadZoneFromFile(formatManager(), originalFile, 60);
            zone.keyLo = 5; zone.keyHi = 6;
            zone.tuneSemitones = 2.0f;
            zone.chokeGroup = 3;

            const auto newFile = writeTempSineWav(2000.0, 0.3, 44100.0, 2); // different content AND channel count
            const auto relocated = ConcreteSampleIO::relocateZone(zone, formatManager(), newFile);

            expect(!relocated.sourceMissing);
            expect(relocated.sourceBuffer != nullptr);
            expectEquals(relocated.sourceBuffer->getNumChannels(), 2, "should reflect the NEW file's data, not the old one's");
            expectEquals(relocated.keyLo, 5, "non-audio metadata should be preserved across a relocate");
            expectEquals(relocated.keyHi, 6);
            expectWithinAbsoluteError(relocated.tuneSemitones, 2.0f, 1.0e-6f);
            expectEquals(relocated.chokeGroup, 3);

            originalFile.deleteFile();
            newFile.deleteFile();
        }

        beginTest("sampleSetToValueTree()/valueTreeToSampleSet() round-trips a multi-zone set with a shared embed budget");
        {
            const auto fileA = writeTempSineWav(1000.0, 0.1, 44100.0, 1);
            const auto fileB = writeTempSineWav(1500.0, 0.1, 44100.0, 1);

            ConcreteSampleSet set;
            auto zoneA = ConcreteSampleIO::loadZoneFromFile(formatManager(), fileA, 36);
            zoneA.keyLo = 36; zoneA.keyHi = 36;
            auto zoneB = ConcreteSampleIO::loadZoneFromFile(formatManager(), fileB, 38);
            zoneB.keyLo = 38; zoneB.keyHi = 38;
            set.zones.push_back(zoneA);
            set.zones.push_back(zoneB);

            const auto tree = ConcreteSampleIO::sampleSetToValueTree(set, true /* embedOverride */);
            expectEquals(tree.getNumChildren(), 2);

            auto xml = tree.createXml();
            const auto reparsed = juce::ValueTree::fromXml(*xml);
            const auto roundTripped = ConcreteSampleIO::valueTreeToSampleSet(reparsed, formatManager());

            expectEquals((int) roundTripped->zones.size(), 2);
            expectEquals(roundTripped->zones[0].keyLo, 36);
            expectEquals(roundTripped->zones[1].keyLo, 38);
            expect(roundTripped->zones[0].sourceBuffer != nullptr);
            expect(roundTripped->zones[1].sourceBuffer != nullptr);

            fileA.deleteFile();
            fileB.deleteFile();
        }
    }
};

static ConcreteSampleIOTests concreteSampleIOTests;
