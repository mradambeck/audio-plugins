#include "../PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

// Offline render harness for Inhalt (mirrors shields-reverb's ShieldsRenderIR / aura-reverb's
// AuraRenderIR pattern). Feeds a single impulse through the real InhaltAudioProcessor (not a
// re-implementation of the DSP) and writes the result to WAV, for effects/nonlin's
// analysis/validate.py to measure against ml-toolkit/effects/nonlin/captures/.
//
// Because Inhalt convolves a synthesized IR rather than running a live tank, feeding one impulse
// through the real processor with Dry=0/Wet=100 reproduces that IR AS SHAPED by the plugin's own
// other controls (Pre-Delay, Low Cut, Width, Converter) - exactly the end-to-end behaviour
// validation needs, not a special case.
//
// MUST write stereo (never collapse to mono) - stereo decorrelation is a validated property of
// this plugin, and a mono render would make half of the project plan's Phase 6 verification
// impossible.
//
// Usage:
//   InhaltRenderIR --out <path.wav> [--seconds 1.5] [--sampleRate 44100]
//                   [--timeKnob 2.2] [--high 0] [--preDelayMs 0] [--lowCutHz 0]
//                   [--converter 0] [--width 100] [--dry 0] [--wet 100] [--bypass 0]
//
// Flags map 1:1 onto the plugin's own APVTS parameter IDs in the plugin's own native units - not
// normalised 0-1. Dry/Wet default to 0/100 here (pure wet, isolated from the dry tap), unlike the
// plugin's own 100%/50% defaults.
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

    void setParam(InhaltAudioProcessor& processor, const char* paramID, float rawValue)
    {
        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(rawValue));
    }
}

int main(int argc, char* argv[])
{
    const auto args = parseArgs(argc, argv);

    const auto outIt = args.find("out");
    if (outIt == args.end())
    {
        std::fprintf(stderr,
            "Usage: InhaltRenderIR --out <path.wav> [--seconds 1.5] [--sampleRate 44100]\n"
            "                       [--timeKnob 2.2] [--high 0] [--preDelayMs 0] [--lowCutHz 0]\n"
            "                       [--converter 0] [--width 100]\n"
            "                       [--dry 0] [--wet 100] [--bypass 0]\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 1.5f);
    const auto totalSamples = (int) (seconds * sampleRate);

    juce::AudioBuffer<float> inputSignal(2, totalSamples);
    inputSignal.clear();
    inputSignal.setSample(0, 0, 1.0f);
    inputSignal.setSample(1, 0, 1.0f);

    InhaltAudioProcessor processor;

    setParam(processor, InhaltAudioProcessor::timeKnobParamID, getFloatArg(args, "timeKnob", 2.2f));
    setParam(processor, InhaltAudioProcessor::highParamID, getFloatArg(args, "high", 0.0f));
    setParam(processor, InhaltAudioProcessor::preDelayMsParamID, getFloatArg(args, "preDelayMs", 0.0f));
    setParam(processor, InhaltAudioProcessor::lowCutHzParamID, getFloatArg(args, "lowCutHz", 0.0f));
    setParam(processor, InhaltAudioProcessor::converterParamID, getFloatArg(args, "converter", 0.0f));
    setParam(processor, InhaltAudioProcessor::widthParamID, getFloatArg(args, "width", 100.0f));
    setParam(processor, InhaltAudioProcessor::dryParamID, getFloatArg(args, "dry", 0.0f));
    setParam(processor, InhaltAudioProcessor::wetParamID, getFloatArg(args, "wet", 100.0f));
    setParam(processor, InhaltAudioProcessor::bypassParamID, getFloatArg(args, "bypass", 0.0f));

    constexpr int blockSize = 512;
    processor.prepareToPlay((double) sampleRate, blockSize);

    juce::AudioBuffer<float> output(2, totalSamples);
    output.clear();

    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;

    int written = 0;
    while (written < totalSamples)
    {
        const auto thisBlockSize = std::min(blockSize, totalSamples - written);
        block.setSize(2, thisBlockSize, false, false, true);
        block.copyFrom(0, 0, inputSignal, 0, written, thisBlockSize);
        block.copyFrom(1, 0, inputSignal, 1, written, thisBlockSize);

        processor.processBlock(block, midi);

        output.copyFrom(0, written, block, 0, 0, thisBlockSize);
        output.copyFrom(1, written, block, 1, 0, thisBlockSize);

        written += thisBlockSize;
    }

    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile(); // File::createOutputStream() appends, not truncates

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
        (float) totalSamples / sampleRate, sampleRate);
    return 0;
}
