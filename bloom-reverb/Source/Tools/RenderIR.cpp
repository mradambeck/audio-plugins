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
//                  [--bandwidth 15000] [--bitdepth 16] [--dry 0] [--wet 100] [--wobble 0]
//
// Every parameter flag matches the units the plugin's own parameter uses (diffusion 0.3-0.7,
// feedback/damping/dry/wet as a percentage 0-100 (wet can go to 200), size as a multiplier,
// bandwidth in Hz, bitdepth in bits) - not normalised 0-1 APVTS values - so a value copied from
// this tool's output maps directly onto where the corresponding knob would sit. --dry/--wet
// default to 0/100 here specifically (NOT the plugin's own 100/40 defaults) so this tool captures
// the ALGORITHM's impulse response in isolation, without a dry click at sample 0 in the way.
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
            "                     [--diffusion 0.5] [--feedback 99] [--size 1.0] [--damping 20]\n"
            "                     [--bandwidth 19000] [--bitdepth 13] [--dry 0] [--wet 100] [--wobble 0]\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 4.0f);

    // Fallback defaults below match PluginProcessor's own createParameterLayout() defaults (tuned
    // against reference-irs/ - see that function's comments), so an unqualified run of this tool
    // renders what the plugin actually ships with, not some other placeholder - EXCEPT dry/wet,
    // deliberately overridden to 0/100 (not the plugin's real 100/40 defaults): this tool captures
    // the ALGORITHM's impulse response for comparison against reference-irs/, where a dry click at
    // sample 0 would only get in the way.
    BloomAudioProcessor processor;
    setParam(processor, BloomAudioProcessor::diffusionParamID, getFloatArg(args, "diffusion", 0.5f));
    setParam(processor, BloomAudioProcessor::feedbackParamID, getFloatArg(args, "feedback", 99.0f));
    setParam(processor, BloomAudioProcessor::sizeParamID, getFloatArg(args, "size", 1.0f));
    setParam(processor, BloomAudioProcessor::dampingParamID, getFloatArg(args, "damping", 20.0f));
    setParam(processor, BloomAudioProcessor::bandwidthHzParamID, getFloatArg(args, "bandwidth", 19000.0f));
    setParam(processor, BloomAudioProcessor::bitDepthParamID, getFloatArg(args, "bitdepth", 13.0f));
    setParam(processor, BloomAudioProcessor::dryParamID, getFloatArg(args, "dry", 0.0f));
    setParam(processor, BloomAudioProcessor::wetParamID, getFloatArg(args, "wet", 100.0f));
    setParam(processor, BloomAudioProcessor::wobbleParamID, getFloatArg(args, "wobble", 0.0f));

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

    // File::createOutputStream() appends at the current end of an existing file rather than
    // truncating it - deleting first is the standard JUCE idiom for "overwrite", and matters a lot
    // here since rerendering to the same path (the natural workflow while tuning) would otherwise
    // silently corrupt the WAV into old-data-plus-new-data rather than replacing it.
    outFile.deleteFile();

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
