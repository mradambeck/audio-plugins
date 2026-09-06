#include "ConcreteSampleIO.h"

namespace
{
    // Shared by loadZoneFromFile() and valueTreeToZone()'s path-fallback case. Returns false
    // (leaving outBuffer/outSampleRate untouched) if the file can't be read.
    bool readAudioFile(juce::AudioFormatManager& formatManager, const juce::File& file,
                        std::shared_ptr<juce::AudioBuffer<float>>& outBuffer, double& outSampleRate)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr)
            return false;

        const auto numChannels = (int) reader->numChannels;
        const auto numSamples = (int) reader->lengthInSamples;
        if (numChannels <= 0 || numSamples <= 0)
            return false;

        auto buffer = std::make_shared<juce::AudioBuffer<float>>(numChannels, numSamples);
        reader->read(buffer.get(), 0, numSamples, 0, true, true);

        outBuffer = buffer;
        outSampleRate = reader->sampleRate;
        return true;
    }
}

namespace ConcreteSampleIO
{
    ConcreteSampleZone loadZoneFromFile(juce::AudioFormatManager& formatManager, const juce::File& file, int rootNote)
    {
        ConcreteSampleZone zone;
        zone.sourcePath = file.getFullPathName();
        zone.rootNote = rootNote;

        std::shared_ptr<juce::AudioBuffer<float>> buffer;
        double sourceSampleRate = 44100.0;
        if (!readAudioFile(formatManager, file, buffer, sourceSampleRate))
        {
            zone.sourceMissing = true;
            return zone;
        }

        zone.sourceBuffer = buffer;
        zone.sourceSampleRate = sourceSampleRate;
        zone.start = 0;
        zone.end = buffer->getNumSamples();
        zone.loopStart = 0;
        zone.loopEnd = buffer->getNumSamples();
        zone.sourceMissing = false;
        return zone;
    }

    ConcreteSampleZone relocateZone(const ConcreteSampleZone& existing, juce::AudioFormatManager& formatManager,
                                     const juce::File& newFile)
    {
        auto updated = loadZoneFromFile(formatManager, newFile, existing.rootNote);
        updated.keyLo = existing.keyLo;
        updated.keyHi = existing.keyHi;
        updated.velLo = existing.velLo;
        updated.velHi = existing.velHi;
        updated.tuneSemitones = existing.tuneSemitones;
        updated.level = existing.level;
        updated.pan = existing.pan;
        updated.chokeGroup = existing.chokeGroup;
        updated.output = existing.output;
        return updated;
    }

    juce::MemoryBlock encodeZoneAsFlac(const ConcreteSampleZone& zone)
    {
        if (zone.sourceBuffer == nullptr || zone.sourceBuffer->getNumSamples() <= 0)
            return {};

        juce::FlacAudioFormat flacFormat;
        if (!flacFormat.getPossibleSampleRates().contains((int) std::lround(zone.sourceSampleRate)))
            return {}; // an unsupported rate - caller falls back to path-only, not an error

        juce::MemoryBlock result;
        auto* outputStream = new juce::MemoryOutputStream(result, false);
        std::unique_ptr<juce::AudioFormatWriter> writer(
            flacFormat.createWriterFor(outputStream, zone.sourceSampleRate,
                                        (unsigned int) zone.sourceBuffer->getNumChannels(), 24, {}, 5));
        if (writer == nullptr)
        {
            delete outputStream;
            return {};
        }

        writer->writeFromAudioSampleBuffer(*zone.sourceBuffer, 0, zone.sourceBuffer->getNumSamples());
        writer.reset(); // flushes/finalizes the FLAC stream into `result` before it's returned
        return result;
    }

    bool decodeFlacBlock(const juce::MemoryBlock& flacBytes, juce::AudioBuffer<float>& outBuffer, double& outSampleRate)
    {
        if (flacBytes.getSize() == 0)
            return false;

        juce::FlacAudioFormat flacFormat;
        auto* inputStream = new juce::MemoryInputStream(flacBytes, false);
        std::unique_ptr<juce::AudioFormatReader> reader(flacFormat.createReaderFor(inputStream, true));
        if (reader == nullptr)
            return false;

        const auto numChannels = (int) reader->numChannels;
        const auto numSamples = (int) reader->lengthInSamples;
        if (numChannels <= 0 || numSamples <= 0)
            return false;

        outBuffer.setSize(numChannels, numSamples);
        reader->read(&outBuffer, 0, numSamples, 0, true, true);
        outSampleRate = reader->sampleRate;
        return true;
    }

    bool shouldEmbed(juce::int64 flacByteSize, juce::int64 alreadyEmbeddedBytesInInstance, bool forceEmbed) noexcept
    {
        if (flacByteSize <= 0)
            return false; // nothing to embed (encoding failed, or an empty buffer)
        if (forceEmbed)
            return true;
        return flacByteSize <= maxEmbeddedBytesPerZone
            && (alreadyEmbeddedBytesInInstance + flacByteSize) <= maxEmbeddedBytesPerInstance;
    }

    juce::ValueTree zoneToValueTree(const ConcreteSampleZone& zone, int index, bool forceEmbed,
                                     juce::int64& alreadyEmbeddedBytesInInstance)
    {
        using namespace ConcreteZoneIDs;

        juce::ValueTree tree(ConcreteZoneIDs::zone);
        tree.setProperty(ConcreteZoneIDs::index, index, nullptr);
        tree.setProperty(path, zone.sourcePath, nullptr);
        tree.setProperty(root, zone.rootNote, nullptr);
        tree.setProperty(keyLo, zone.keyLo, nullptr);
        tree.setProperty(keyHi, zone.keyHi, nullptr);
        tree.setProperty(velLo, zone.velLo, nullptr);
        tree.setProperty(velHi, zone.velHi, nullptr);
        tree.setProperty(start, (juce::int64) zone.start, nullptr);
        tree.setProperty(end, (juce::int64) zone.end, nullptr);
        tree.setProperty(loopStart, (juce::int64) zone.loopStart, nullptr);
        tree.setProperty(loopEnd, (juce::int64) zone.loopEnd, nullptr);
        tree.setProperty(loopEnabled, zone.loopEnabled, nullptr);
        tree.setProperty(reverse, zone.reverse, nullptr);
        tree.setProperty(tune, (double) zone.tuneSemitones, nullptr);
        tree.setProperty(level, (double) zone.level, nullptr);
        tree.setProperty(pan, (double) zone.pan, nullptr);
        tree.setProperty(chokeGroup, zone.chokeGroup, nullptr);
        tree.setProperty(output, zone.output, nullptr);
        tree.setProperty(sourceSampleRate, zone.sourceSampleRate, nullptr);

        const auto flacBytes = encodeZoneAsFlac(zone);
        if (shouldEmbed((juce::int64) flacBytes.getSize(), alreadyEmbeddedBytesInInstance, forceEmbed))
        {
            tree.setProperty(embeddedAudio, juce::var(flacBytes), nullptr);
            alreadyEmbeddedBytesInInstance += (juce::int64) flacBytes.getSize();
        }

        return tree;
    }

    ConcreteSampleZone valueTreeToZone(const juce::ValueTree& zoneTree, juce::AudioFormatManager& formatManager)
    {
        using namespace ConcreteZoneIDs;

        ConcreteSampleZone zone;
        zone.sourcePath = zoneTree.getProperty(path, juce::String()).toString();
        zone.rootNote = (int) zoneTree.getProperty(root, 60);
        zone.keyLo = (int) zoneTree.getProperty(keyLo, 0);
        zone.keyHi = (int) zoneTree.getProperty(keyHi, 127);
        zone.velLo = (int) zoneTree.getProperty(velLo, 0);
        zone.velHi = (int) zoneTree.getProperty(velHi, 127);
        zone.start = (juce::int64) zoneTree.getProperty(start, (juce::int64) 0);
        zone.end = (juce::int64) zoneTree.getProperty(end, (juce::int64) 0);
        zone.loopStart = (juce::int64) zoneTree.getProperty(loopStart, (juce::int64) 0);
        zone.loopEnd = (juce::int64) zoneTree.getProperty(loopEnd, (juce::int64) 0);
        zone.loopEnabled = (bool) zoneTree.getProperty(loopEnabled, false);
        zone.reverse = (bool) zoneTree.getProperty(reverse, false);
        zone.tuneSemitones = (float) (double) zoneTree.getProperty(tune, 0.0);
        zone.level = (float) (double) zoneTree.getProperty(level, 1.0);
        zone.pan = (float) (double) zoneTree.getProperty(pan, 0.0);
        zone.chokeGroup = (int) zoneTree.getProperty(chokeGroup, 0);
        zone.output = (int) zoneTree.getProperty(output, 0);
        zone.sourceSampleRate = (double) zoneTree.getProperty(sourceSampleRate, 44100.0);

        if (zoneTree.hasProperty(embeddedAudio))
        {
            const auto embeddedProp = zoneTree.getProperty(embeddedAudio);
            juce::MemoryBlock flacBytes;
            if (embeddedProp.isBinaryData())
                flacBytes = *embeddedProp.getBinaryData();
            else
                flacBytes.fromBase64Encoding(embeddedProp.toString());

            juce::AudioBuffer<float> decoded;
            double decodedRate = zone.sourceSampleRate;
            if (decodeFlacBlock(flacBytes, decoded, decodedRate))
            {
                zone.sourceBuffer = std::make_shared<juce::AudioBuffer<float>>(std::move(decoded));
                zone.sourceSampleRate = decodedRate;
                zone.sourceMissing = false;
                return zone;
            }
        }

        if (zone.sourcePath.isNotEmpty())
        {
            std::shared_ptr<juce::AudioBuffer<float>> buffer;
            double sourceSampleRate = zone.sourceSampleRate;
            if (readAudioFile(formatManager, juce::File(zone.sourcePath), buffer, sourceSampleRate))
            {
                zone.sourceBuffer = buffer;
                zone.sourceSampleRate = sourceSampleRate;
                zone.sourceMissing = false;
                return zone;
            }
        }

        zone.sourceMissing = true;
        return zone;
    }

    juce::ValueTree sampleSetToValueTree(const ConcreteSampleSet& set, bool embedOverride)
    {
        juce::ValueTree zonesTree(ConcreteZoneIDs::zones);
        zonesTree.setProperty(ConcreteZoneIDs::embedOverride, embedOverride, nullptr);

        juce::int64 alreadyEmbeddedBytesInInstance = 0;
        for (int i = 0; i < (int) set.zones.size(); ++i)
            zonesTree.appendChild(
                zoneToValueTree(set.zones[(size_t) i], i, embedOverride, alreadyEmbeddedBytesInInstance), nullptr);

        return zonesTree;
    }

    ConcreteSampleSet::Ptr valueTreeToSampleSet(const juce::ValueTree& zonesTree, juce::AudioFormatManager& formatManager)
    {
        ConcreteSampleSet::Ptr set(new ConcreteSampleSet());
        if (!zonesTree.isValid())
            return set;

        for (int i = 0; i < zonesTree.getNumChildren(); ++i)
            set->zones.push_back(valueTreeToZone(zonesTree.getChild(i), formatManager));

        return set;
    }
}
