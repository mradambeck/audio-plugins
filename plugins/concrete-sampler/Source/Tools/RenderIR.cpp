#include "../PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Offline render harness for Concrete (mirrors alloy-bass's AlloyRenderIR - a MIDI-driven synth,
// not an audio-in-driven effect). Reuses Source/Tests/TestCreateEditorStub.cpp so this target
// stays free of the editor/LookAndFeel/BinaryData.
//
// Usage:
//   ConcreteRenderIR --out <path.wav> [--seconds 6] [--sampleRate 44100]
//                    [--sample <path.wav>] [--note 60] [--velocity 100]
//                    [--sequence "note:velocity:onSeconds:durationSeconds,..."]
//                    [--<paramID> <rawValue>]...
//
// With no --sample, renders silence (Phase 0's original behavior - there's nothing loaded to
// trigger). With --sample, loads it (via ConcreteAudioProcessor::loadSample(), the same
// synchronous, real-time-unsafe path the editor's Load button would hop to a background thread
// for - fine to call directly here since this is a batch tool, not an audio thread) and triggers
// either a single held note at --note/--velocity (default 60/100, no note-off - held for the
// whole render, matching what a "1kHz at root" analysis run wants), or, if --sequence is given, a
// scripted multi-note sequence instead: a comma-separated list of note:velocity:onSeconds:
// durationSeconds entries (durationSeconds of 0 means no note-off - the note rings/one-shots).
//
// --<paramID> flags map 1:1 onto the plugin's own APVTS parameter IDs (PluginProcessor.h) in
// native units, applied via setValueNotifyingHost() BEFORE the note-on so a voice starting picks
// them up - matches every other plugin's RenderIR convention in this catalog. Phase 2 adds
// pitchEngineMode (0=Reference, 1=Mode A, 2=Mode B, 3=Mode C - the same index order as the
// AudioParameterChoice), baseRate (Hz), coarseTune (semitones), fineTune (cents). Phase 3 adds
// bitDepth (1-16) and quantizerMode (0=Linear, 1=Companded). Phase 4 adds captureTranspose
// (semitones), captureDrive (dB), captureAutoCompensate (0/1), captureBypass (0/1), and
// captureIterations (1-4). Capture-pass parameters are applied (via setValueNotifyingHost(),
// same as every other flag here) BEFORE --sample is loaded, so the sample's initial bake already
// reflects them - see ConcreteAudioProcessor::loadSample(). Phase 5 adds the live filter
// (filterModel 0-5 = Bypass/SSM/CEM Loss/CEM Compensated/Digital+VCA/One-Pole, filterCutoff Hz,
// filterResonance 0-1, filterEnvAmount octaves, filterKeyTrack 0-1). Phase 6 adds voiceCount
// (1-18, the runtime polyphony cap - see ConcreteVoiceAllocator.h) and ampEnvelopeMode (0=ADSR,
// 1=Contoured - see ConcreteContourEnvelope.h).
namespace
{
    std::map<std::string, std::string> parseArgs(int argc, char* argv[])
    {
        std::map<std::string, std::string> args;
        for (int i = 1; i + 1 < argc; i += 2)
        {
            std::string key = argv[i];
            if (key.rfind("--", 0) == 0)
                args[key.substr(2)] = argv[i + 1];
        }
        return args;
    }

    float getFloatArg(const std::map<std::string, std::string>& args, const std::string& key, float defaultValue)
    {
        const auto it = args.find(key);
        return it == args.end() ? defaultValue : std::stof(it->second);
    }

    int getIntArg(const std::map<std::string, std::string>& args, const std::string& key, int defaultValue)
    {
        const auto it = args.find(key);
        return it == args.end() ? defaultValue : std::stoi(it->second);
    }

    void setParam(ConcreteAudioProcessor& processor, const juce::String& paramID, float rawValue)
    {
        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(rawValue));
    }

    constexpr const char* allParamIDs[] = {
        ConcreteAudioProcessor::pitchEngineModeParamID,
        ConcreteAudioProcessor::baseRateParamID,
        ConcreteAudioProcessor::coarseTuneParamID,
        ConcreteAudioProcessor::fineTuneParamID,
        ConcreteAudioProcessor::bitDepthParamID,
        ConcreteAudioProcessor::quantizerModeParamID,
        ConcreteAudioProcessor::captureTransposeParamID,
        ConcreteAudioProcessor::captureDriveParamID,
        ConcreteAudioProcessor::captureAutoCompensateParamID,
        ConcreteAudioProcessor::captureBypassParamID,
        ConcreteAudioProcessor::captureIterationsParamID,
        ConcreteAudioProcessor::filterModelParamID,
        ConcreteAudioProcessor::filterCutoffParamID,
        ConcreteAudioProcessor::filterResonanceParamID,
        ConcreteAudioProcessor::filterEnvAmountParamID,
        ConcreteAudioProcessor::filterKeyTrackParamID,
        ConcreteAudioProcessor::voiceCountParamID,
        ConcreteAudioProcessor::ampEnvelopeModeParamID,
    };

    struct TimedEvent
    {
        int64_t samplePosition;
        juce::MidiMessage message;
    };

    std::vector<TimedEvent> buildHeldNoteEvent(int note, int velocity)
    {
        return { { 0, juce::MidiMessage::noteOn(1, note, (juce::uint8) velocity) } };
    }

    // Parses "note:velocity:onSeconds:durationSeconds,..." into sample-accurate note-on/note-off
    // events, sorted by position. durationSeconds <= 0 omits the note-off (the note rings/
    // one-shots, same as buildHeldNoteEvent()'s default).
    std::vector<TimedEvent> parseSequence(const std::string& spec, double sampleRate)
    {
        std::vector<TimedEvent> events;
        std::stringstream entries(spec);
        std::string entry;
        while (std::getline(entries, entry, ','))
        {
            std::stringstream fields(entry);
            std::string noteStr, velocityStr, onStr, durationStr;
            std::getline(fields, noteStr, ':');
            std::getline(fields, velocityStr, ':');
            std::getline(fields, onStr, ':');
            std::getline(fields, durationStr, ':');

            const auto note = std::stoi(noteStr);
            const auto velocity = std::stoi(velocityStr);
            const auto onSeconds = std::stod(onStr);
            const auto durationSeconds = durationStr.empty() ? 0.0 : std::stod(durationStr);

            const auto onSample = (int64_t) (onSeconds * sampleRate);
            events.push_back({ onSample, juce::MidiMessage::noteOn(1, note, (juce::uint8) velocity) });

            if (durationSeconds > 0.0)
            {
                const auto offSample = (int64_t) ((onSeconds + durationSeconds) * sampleRate);
                events.push_back({ offSample, juce::MidiMessage::noteOff(1, note) });
            }
        }

        std::sort(events.begin(), events.end(),
                   [](const TimedEvent& a, const TimedEvent& b) { return a.samplePosition < b.samplePosition; });
        return events;
    }

    // Extracts events whose absolute sample position falls in [blockStart, blockStart+blockSize)
    // into a block-local MidiBuffer, at position (absolute - blockStart). Matches alloy-bass's
    // AlloyRenderIR sliceMidiForBlock().
    juce::MidiBuffer sliceMidiForBlock(const std::vector<TimedEvent>& events, size_t& nextEventIndex,
                                        int64_t blockStart, int blockSize)
    {
        juce::MidiBuffer buffer;
        while (nextEventIndex < events.size()
               && events[nextEventIndex].samplePosition < blockStart + blockSize)
        {
            const auto& event = events[nextEventIndex];
            const auto localPosition = (int) (event.samplePosition - blockStart);
            buffer.addEvent(event.message, std::max(0, localPosition));
            ++nextEventIndex;
        }
        return buffer;
    }
}

int main(int argc, char* argv[])
{
    const auto args = parseArgs(argc, argv);

    const auto outIt = args.find("out");
    if (outIt == args.end())
    {
        std::fprintf(stderr,
            "Usage: ConcreteRenderIR --out <path.wav> [--seconds 6] [--sampleRate 44100]\n"
            "                       [--sample <path.wav>] [--note 60] [--velocity 100]\n"
            "                       [--sequence \"note:velocity:onSeconds:durationSeconds,...\"]\n"
            "                       [--<paramID> <rawValue>]...\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 6.0f);
    constexpr int blockSize = 512;

    ConcreteAudioProcessor processor;
    processor.prepareToPlay((double) sampleRate, blockSize);

    for (auto* paramID : allParamIDs)
    {
        const auto it = args.find(paramID);
        if (it != args.end())
            setParam(processor, paramID, std::stof(it->second));
    }

    const auto sampleIt = args.find("sample");
    if (sampleIt != args.end())
    {
        if (!processor.loadSample(juce::File(sampleIt->second)))
        {
            std::fprintf(stderr, "Could not load sample \"%s\"\n", sampleIt->second.c_str());
            return 1;
        }
    }

    std::vector<TimedEvent> events;
    if (sampleIt != args.end())
    {
        const auto sequenceIt = args.find("sequence");
        events = sequenceIt != args.end()
            ? parseSequence(sequenceIt->second, (double) sampleRate)
            : buildHeldNoteEvent(getIntArg(args, "note", 60), getIntArg(args, "velocity", 100));
    }

    const auto totalSamples = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> output(2, totalSamples);
    output.clear();

    juce::AudioBuffer<float> block(2, blockSize);
    size_t nextEventIndex = 0;
    int written = 0;
    while (written < totalSamples)
    {
        const auto thisBlockSize = std::min(blockSize, totalSamples - written);
        block.setSize(2, thisBlockSize, false, false, true);
        block.clear();

        auto midi = sliceMidiForBlock(events, nextEventIndex, written, thisBlockSize);
        processor.processBlock(block, midi);

        output.copyFrom(0, written, block, 0, 0, thisBlockSize);
        output.copyFrom(1, written, block, 1, 0, thisBlockSize);

        written += thisBlockSize;
    }

    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile(); // File::createOutputStream() appends by default - delete first to overwrite

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    if (stream == nullptr)
    {
        std::fprintf(stderr, "Could not open %s for writing\n", outFile.getFullPathName().toRawUTF8());
        return 1;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), sampleRate, 2, 32, {}, 0));

    if (writer == nullptr)
    {
        std::fprintf(stderr, "Could not create WAV writer\n");
        return 1;
    }

    stream.release(); // the writer now owns the stream
    writer->writeFromAudioSampleBuffer(output, 0, output.getNumSamples());

    std::printf("Wrote %s (%.2fs @ %.0fHz)\n", outFile.getFullPathName().toRawUTF8(),
                (double) output.getNumSamples() / sampleRate, (double) sampleRate);
    return 0;
}
