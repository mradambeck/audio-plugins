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
//                [--timeSeconds 2.0] [--lowDb 0] [--highDb 0] [--preDelayMs 0]
//                [--bitDepth 16] [--mixPercent 100] [--inputGainDb 0] [--outputGainDb 0]
//                [--bypass 0]
//
// Flags map 1:1 onto the plugin's own APVTS parameter IDs (PluginProcessor.h) in the plugin's own
// native units - not normalised 0-1. Mix defaults to 100 (full wet) here, unlike the plugin's own
// 50% default, so an unqualified run renders the reverb itself rather than a half-dry blend.
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
}

int main(int argc, char* argv[])
{
    const auto args = parseArgs(argc, argv);

    const auto outIt = args.find("out");
    if (outIt == args.end())
    {
        std::fprintf(stderr,
            "Usage: AuraRenderIR --out <path.wav> [--seconds 3.0] [--sampleRate 44100]\n"
            "                     [--timeSeconds 2.0] [--lowDb 0] [--highDb 0]\n"
            "                     [--preDelayMs 0] [--bitDepth 16] [--mixPercent 100]\n"
            "                     [--inputGainDb 0] [--outputGainDb 0] [--bypass 0]\n");
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

    setParam(processor, AuraAudioProcessor::timeSecondsParamID, getFloatArg(args, "timeSeconds", 2.0f));
    setParam(processor, AuraAudioProcessor::lowDbParamID, getFloatArg(args, "lowDb", 0.0f));
    setParam(processor, AuraAudioProcessor::highDbParamID, getFloatArg(args, "highDb", 0.0f));
    setParam(processor, AuraAudioProcessor::preDelayMsParamID, getFloatArg(args, "preDelayMs", 0.0f));
    setParam(processor, AuraAudioProcessor::bitDepthParamID, getFloatArg(args, "bitDepth", 16.0f));
    setParam(processor, AuraAudioProcessor::mixPercentParamID, getFloatArg(args, "mixPercent", 100.0f));
    setParam(processor, AuraAudioProcessor::inputGainDbParamID, getFloatArg(args, "inputGainDb", 0.0f));
    setParam(processor, AuraAudioProcessor::outputGainDbParamID, getFloatArg(args, "outputGainDb", 0.0f));
    setParam(processor, AuraAudioProcessor::bypassParamID, getFloatArg(args, "bypass", 0.0f));

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
