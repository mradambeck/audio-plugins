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

// Offline render harness for Aura (mirrors shields-reverb's ShieldsRenderIR / intruder-gated-
// reverb's IntruderRenderIR pattern). Feeds a single impulse through the real AuraAudioProcessor
// (not a re-implementation of the DSP) and writes the result to WAV, for Phase D validation
// against the reference captures in ml-toolkit/effects/ambience/captures/, or scoring via
// ../common/tools/compare_wavs.py.
//
// Usage:
//   AuraRenderIR --out <path.wav> [--seconds 3.0] [--sampleRate 44100]
//                [--preset "<factory preset name>"]
//                [--timeSeconds 2.0] [--lowCutHz 0] [--highDb 0] [--preDelayMs 0]
//                [--bitDepth 8] [--dry 0] [--wet 100]
//                [--bypass 0]
//
// Flags map 1:1 onto the plugin's own APVTS parameter IDs (PluginProcessor.h) in the plugin's own
// native units - not normalised 0-1. Dry/Wet default to 0/100 here (pure wet reverb, isolated from
// the dry tap), unlike the plugin's own 100%/50% defaults, so an unqualified run renders the
// reverb itself rather than a dry-contaminated blend.
//
// --preset applies one of the factory presets first (setCurrentProgram's own
// setValueNotifyingHost() path, see PluginProcessor.cpp's getFactoryPresets()); any --<paramID>
// flags explicitly given override individual values on top of it. Without --preset, every param
// still falls back to this tool's own hardcoded default above (not the plugin's own parameter
// default) exactly as before - analysis/validate.py relies on those defaults - so omitting
// --preset leaves existing behavior byte-for-byte unchanged.
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

    void setParam(AuraAudioProcessor& processor, const char* paramID, float rawValue)
    {
        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(rawValue));
    }

    // bitDepthParamID is a 3-choice AudioParameterChoice (index 0/1/2), not a continuous 8-24
    // range - this flag still takes a plain bit count for CLI convenience and converts to the
    // nearest choice index here, same convention as PluginProcessor::getBitDepthForChoiceIndex()
    // in reverse.
    float bitDepthChoiceIndexFor(float bits)
    {
        if (bits >= 20.0f)
            return 2.0f;
        if (bits >= 12.0f)
            return 1.0f;
        return 0.0f;
    }
}

int main(int argc, char* argv[])
{
    const auto args = parseArgs(argc, argv);

    const auto outIt = args.find("out");
    if (outIt == args.end())
    {
        std::fprintf(stderr,
            "Usage: AuraRenderIR --out <path.wav> [--seconds 3.0] [--sampleRate 44100]\n"
            "                     [--preset \"<name>\"]\n"
            "                     [--timeSeconds 2.0] [--lowCutHz 0] [--highDb 0]\n"
            "                     [--preDelayMs 0] [--bitDepth 8]\n"
            "                     [--dry 0] [--wet 100] [--bypass 0]\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 3.0f);
    const auto totalSamples = (int) (seconds * sampleRate);

    juce::AudioBuffer<float> inputSignal(2, totalSamples);
    inputSignal.clear();
    inputSignal.setSample(0, 0, 1.0f);
    inputSignal.setSample(1, 0, 1.0f);

    AuraAudioProcessor processor;

    const auto presetIt = args.find("preset");
    const bool presetApplied = presetIt != args.end();
    if (presetApplied)
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

    // Each flag below only falls back to this tool's own hardcoded default (not the plugin's own
    // parameter default) when neither a preset nor the flag itself was given - preserves this
    // tool's long-standing byte-for-byte behavior when --preset is omitted (analysis/validate.py
    // relies on exactly these defaults). Once --preset IS given, an unset flag instead leaves the
    // preset's own value alone - only flags the caller actually passed override it, matching
    // flux-phaser's own --preset contract.
    auto applyParam = [&](const char* paramID, const char* flagName, float toolDefault)
    {
        const auto it = args.find(flagName);
        if (it != args.end())
            setParam(processor, paramID, std::stof(it->second));
        else if (!presetApplied)
            setParam(processor, paramID, toolDefault);
    };

    applyParam(AuraAudioProcessor::timeSecondsParamID, "timeSeconds", 2.0f);
    applyParam(AuraAudioProcessor::lowCutHzParamID, "lowCutHz", 0.0f);
    applyParam(AuraAudioProcessor::highDbParamID, "highDb", 0.0f);
    applyParam(AuraAudioProcessor::preDelayMsParamID, "preDelayMs", 0.0f);
    {
        // bitDepth needs its own block: the flag takes a plain bit count, converted to the
        // 3-choice index setParam() actually needs (see bitDepthChoiceIndexFor()'s comment).
        const auto it = args.find("bitDepth");
        if (it != args.end())
            setParam(processor, AuraAudioProcessor::bitDepthParamID, bitDepthChoiceIndexFor(std::stof(it->second)));
        else if (!presetApplied)
            setParam(processor, AuraAudioProcessor::bitDepthParamID, bitDepthChoiceIndexFor(8.0f));
    }
    applyParam(AuraAudioProcessor::dryParamID, "dry", 0.0f);
    applyParam(AuraAudioProcessor::wetParamID, "wet", 100.0f);
    applyParam(AuraAudioProcessor::bypassParamID, "bypass", 0.0f);

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
    outFile.deleteFile(); // File::createOutputStream() appends, not truncates - see Shields'/Intruder's RenderIR.cpp

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
