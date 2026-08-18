#include "../PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

// Offline impulse-response render harness for Bloom (build order step 2). Feeds a single-sample
// impulse through the real BloomAudioProcessor (not a re-implementation of the DSP) and writes the
// result to a WAV file, so tools/compare_irs.py can compare it against a captured reference IR in
// reference-irs/ without ever needing to build/load the plugin itself in a DAW.
//
// Usage:
//   BloomRenderIR --out <path.wav> [--seconds 4.0] [--sampleRate 44100]
//                  [--diffusion 0.5] [--feedback 85] [--size 1.0] [--damping 35]
//                  [--bandwidth 15000] [--bitdepth 16] [--mix 100]
//
// Every parameter flag matches the units the plugin's own parameter uses (diffusion 0.3-0.7,
// feedback/damping/mix as a percentage 0-100, size as a multiplier, bandwidth in Hz, bitdepth in
// bits) - not normalised 0-1 APVTS values - so a value copied from this tool's output maps
// directly onto where the corresponding knob would sit.
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

    void setParam(BloomAudioProcessor& processor, const char* paramID, float rawValue)
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
            "Usage: BloomRenderIR --out <path.wav> [--seconds 4.0] [--sampleRate 44100]\n"
            "                     [--diffusion 0.5] [--feedback 85] [--size 1.0] [--damping 35]\n"
            "                     [--bandwidth 15000] [--bitdepth 16] [--mix 100]\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 4.0f);

    BloomAudioProcessor processor;
    setParam(processor, BloomAudioProcessor::diffusionParamID, getFloatArg(args, "diffusion", 0.5f));
    setParam(processor, BloomAudioProcessor::feedbackParamID, getFloatArg(args, "feedback", 85.0f));
    setParam(processor, BloomAudioProcessor::sizeParamID, getFloatArg(args, "size", 1.0f));
    setParam(processor, BloomAudioProcessor::dampingParamID, getFloatArg(args, "damping", 35.0f));
    setParam(processor, BloomAudioProcessor::bandwidthHzParamID, getFloatArg(args, "bandwidth", 15000.0f));
    setParam(processor, BloomAudioProcessor::bitDepthParamID, getFloatArg(args, "bitdepth", 16.0f));
    // Default 100% wet: this tool captures the ALGORITHM's impulse response for comparison
    // against reference-irs/, where a dry click at sample 0 would only get in the way.
    setParam(processor, BloomAudioProcessor::mixParamID, getFloatArg(args, "mix", 100.0f));

    constexpr int blockSize = 512;
    processor.prepareToPlay((double) sampleRate, blockSize);

    const auto totalSamples = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> output(2, totalSamples);
    output.clear();

    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;

    int written = 0;
    while (written < totalSamples)
    {
        const auto thisBlockSize = std::min(blockSize, totalSamples - written);
        block.setSize(2, thisBlockSize, false, false, true);
        block.clear();

        if (written == 0)
        {
            block.setSample(0, 0, 1.0f);
            block.setSample(1, 0, 1.0f);
        }

        processor.processBlock(block, midi);

        output.copyFrom(0, written, block, 0, 0, thisBlockSize);
        output.copyFrom(1, written, block, 1, 0, thisBlockSize);

        written += thisBlockSize;
    }

    outFile.getParentDirectory().createDirectory();

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
