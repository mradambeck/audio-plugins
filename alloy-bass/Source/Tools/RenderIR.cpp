#include "../PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Offline render harness for Alloy (mirrors gradient-pitch's GradientRenderIR / shields-reverb's
// ShieldsRenderIR pattern), but MIDI-driven rather than audio-in-driven - Alloy is a synth, not an
// effect, so there's no audio input to process; output comes entirely from a scripted MIDI note
// sequence. Reuses Source/Tests/TestCreateEditorStub.cpp (already exists for AlloyTests) so this
// target stays free of the editor/LookAndFeel/BinaryData the way AlloyTests already is - lighter
// than Gradient/Shields' RenderIR tools, which had no such stub available.
//
// Usage:
//   AlloyRenderIR --out <path.wav> [--seconds 6] [--sampleRate 44100] [--ageSeed <n>]
//                [--preset "<factory preset name>"] [--<paramID> <rawValue>]...
//
// Flags map 1:1 onto the plugin's own APVTS parameter IDs (PluginProcessor.h) in native units.
// --preset applies one of the 8 baked-in factory presets by name (see getProgramName() at
// runtime, or PluginProcessor.cpp's getFactoryPresets() for the list) via the same
// setCurrentProgram()/setValueNotifyingHost() path a host's program-change UI would use; any
// --<paramID> flags given afterward override individual values on top of it. --ageSeed pins
// ageNoiseRandom (see setAgeSeedForTesting()'s comment) so Age>0 renders are reproducible across
// separate process invocations, which they otherwise are not.
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

    void setParam(AlloyAudioProcessor& processor, const char* paramID, float rawValue)
    {
        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(rawValue));
    }

    // Every APVTS parameter ID this plugin exposes (PluginProcessor.h:110-165) - any of these may
    // be passed as a --<id> <rawValue> flag to override a preset/default value.
    constexpr const char* allParamIDs[] = {
        AlloyAudioProcessor::analogWaveformParamID, AlloyAudioProcessor::analogOctaveParamID,
        AlloyAudioProcessor::analogUnisonParamID, AlloyAudioProcessor::analogDetuneParamID,
        AlloyAudioProcessor::analogFilterCutoffParamID, AlloyAudioProcessor::analogFilterResonanceParamID,
        AlloyAudioProcessor::analogFilterEnvAmountParamID, AlloyAudioProcessor::analogVelocityToFilterParamID,
        AlloyAudioProcessor::analogFilterAttackParamID, AlloyAudioProcessor::analogFilterDecayParamID,
        AlloyAudioProcessor::analogFilterSustainParamID, AlloyAudioProcessor::analogFilterReleaseParamID,
        AlloyAudioProcessor::analogAmpAttackParamID, AlloyAudioProcessor::analogAmpDecayParamID,
        AlloyAudioProcessor::analogAmpSustainParamID, AlloyAudioProcessor::analogAmpReleaseParamID,
        AlloyAudioProcessor::analogGlideTimeParamID, AlloyAudioProcessor::analogVolumeParamID,
        AlloyAudioProcessor::subEnabledParamID, AlloyAudioProcessor::subWaveformParamID,
        AlloyAudioProcessor::subOctaveParamID, AlloyAudioProcessor::subVolumeParamID,
        AlloyAudioProcessor::fmCarrierWaveformParamID, AlloyAudioProcessor::fmCarrierOctaveParamID,
        AlloyAudioProcessor::fmCarrierVolumeParamID, AlloyAudioProcessor::fmVelocityToCarrierParamID,
        AlloyAudioProcessor::fmCarrierAttackParamID, AlloyAudioProcessor::fmCarrierDecayParamID,
        AlloyAudioProcessor::fmCarrierSustainParamID, AlloyAudioProcessor::fmCarrierReleaseParamID,
        AlloyAudioProcessor::fmModulatorWaveformParamID, AlloyAudioProcessor::fmModulatorOctaveParamID,
        AlloyAudioProcessor::fmModulatorVolumeParamID, AlloyAudioProcessor::fmVelocityToBrightnessParamID,
        AlloyAudioProcessor::fmModulatorBrightnessParamID, AlloyAudioProcessor::fmModulatorAttackParamID,
        AlloyAudioProcessor::fmModulatorDecayParamID, AlloyAudioProcessor::fmModulatorSustainParamID,
        AlloyAudioProcessor::fmModulatorReleaseParamID, AlloyAudioProcessor::arpEnabledParamID,
        AlloyAudioProcessor::arpSyncParamID, AlloyAudioProcessor::arpDivisionParamID,
        AlloyAudioProcessor::arpRateParamID, AlloyAudioProcessor::arpPatternParamID,
        AlloyAudioProcessor::arpOctaveRangeParamID, AlloyAudioProcessor::arpGateParamID,
        AlloyAudioProcessor::arpHoldParamID, AlloyAudioProcessor::mixDriveParamID,
        AlloyAudioProcessor::mixToneParamID, AlloyAudioProcessor::mixOutputParamID,
        AlloyAudioProcessor::mixAgeParamID,
    };

    struct TimedEvent
    {
        int64_t samplePosition;
        juce::MidiMessage message;
    };

    // A scripted bass-line-ish MIDI sequence, scaled to --seconds, deliberately exercising every
    // path the findings in this optimization pass depend on:
    //  - a note held long enough to reach genuine ADSR sustain (decay times run up to 2s) and
    //    then a release + silence gap long enough to reach exact idle (finding #1/#5)
    //  - a note-on landing mid-block (not at a block boundary) at a DIFFERENT velocity than the
    //    currently-sounding note (this is what exposed a real bug in an earlier draft of finding
    //    #1's fix - see the plan)
    //  - a legato overlap (note-on while the gate is already open - forceRetrigger=false path)
    //    alongside a forced retrigger (note-on after full silence - forceRetrigger=true path)
    std::vector<TimedEvent> buildMidiSequence(double sampleRate, float totalSeconds)
    {
        std::vector<TimedEvent> events;
        auto at = [sampleRate](float seconds) { return (int64_t) (seconds * sampleRate); };

        const auto scale = totalSeconds / 8.0f; // sequence is authored for an 8s baseline, then scaled

        // Long-held low note - reaches sustain, then release + idle gap.
        events.push_back({ at(0.0f), juce::MidiMessage::noteOn(1, 36, (juce::uint8) 100) });
        // Mid-block (non-zero, non-block-boundary) note-on at a DIFFERENT velocity, still legato
        // (previous note still held) - exercises the currentVelocity-mid-block path directly.
        events.push_back({ at(1.0f * scale) + 137, juce::MidiMessage::noteOn(1, 43, (juce::uint8) 60) });
        // A second, different-velocity legato note.
        events.push_back({ at(1.6f * scale), juce::MidiMessage::noteOn(1, 41, (juce::uint8) 110) });
        // Release everything - full silence, long enough to reach exact idle.
        events.push_back({ at(2.2f * scale), juce::MidiMessage::noteOff(1, 41) });
        events.push_back({ at(2.2f * scale), juce::MidiMessage::noteOff(1, 43) });
        events.push_back({ at(2.2f * scale), juce::MidiMessage::noteOff(1, 36) });

        // Forced retrigger after full silence (wasEmpty -> forceRetrigger=true), held into
        // sustain again, at a third velocity.
        events.push_back({ at(4.0f * scale), juce::MidiMessage::noteOn(1, 48, (juce::uint8) 80) });
        events.push_back({ at(6.0f * scale), juce::MidiMessage::noteOff(1, 48) });

        return events;
    }

    // Extracts events whose absolute sample position falls in [blockStart, blockStart+blockSize)
    // into a block-local MidiBuffer, at position (absolute - blockStart).
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
            "Usage: AlloyRenderIR --out <path.wav> [--seconds 6] [--sampleRate 44100]\n"
            "                    [--ageSeed <n>] [--preset \"<name>\"] [--<paramID> <rawValue>]...\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 6.0f);

    AlloyAudioProcessor processor;

    constexpr int blockSize = 512;
    processor.prepareToPlay((double) sampleRate, blockSize);

    const auto ageSeedIt = args.find("ageSeed");
    if (ageSeedIt != args.end())
        processor.setAgeSeedForTesting((juce::int64) std::stoll(ageSeedIt->second));

    const auto presetIt = args.find("preset");
    if (presetIt != args.end())
    {
        int foundIndex = -1;
        for (int i = 0; i < processor.getNumPrograms(); ++i)
        {
            if (processor.getProgramName(i) == juce::String(presetIt->second))
            {
                foundIndex = i;
                break;
            }
        }
        if (foundIndex < 0)
        {
            std::fprintf(stderr, "Unknown preset \"%s\"\n", presetIt->second.c_str());
            return 1;
        }
        processor.setCurrentProgram(foundIndex);
    }

    for (auto* paramID : allParamIDs)
    {
        const auto it = args.find(paramID);
        if (it != args.end())
            setParam(processor, paramID, std::stof(it->second));
    }

    const auto midiEvents = buildMidiSequence(sampleRate, seconds);
    size_t nextEventIndex = 0;

    const auto totalSamples = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> output(2, totalSamples);
    output.clear();

    juce::AudioBuffer<float> block(2, blockSize);

    int written = 0;
    while (written < totalSamples)
    {
        const auto thisBlockSize = std::min(blockSize, totalSamples - written);
        block.setSize(2, thisBlockSize, false, false, true);
        block.clear();

        auto midi = sliceMidiForBlock(midiEvents, nextEventIndex, written, thisBlockSize);
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

    std::printf("Wrote %s (%.2fs @ %.0fHz)\n", outFile.getFullPathName().toRawUTF8(), seconds, sampleRate);
    return 0;
}
