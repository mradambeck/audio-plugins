#include "ConvolutionProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

// Offline render harness for the shared convolution engine. Drives the real ConvolutionProcessor -
// not a re-implementation of the DSP - and writes 32-bit float WAV, so analysis/validate.py can
// measure the things a unit test asserts but cannot show: where an impulse actually lands, what a
// bypass transition looks like sample by sample, and whether an IR sounds the same at 48 kHz and
// 96 kHz.
//
// Usage:
//   ConvBaseRenderIR --out <path.wav> [--seconds 3.0] [--sampleRate 48000] [--blockSize 256]
//                    [--channels 2] [--signal impulse|sine|silence] [--freq 220]
//                    [--ir 0] [--predelay 0] [--length 100] [--attack 0]
//                    [--lowcut 20] [--highcut 20000] [--dry 0] [--wet 100]
//                    [--bypassAt -1] [--switchIRAt -1] [--switchIRTo 1]
//
// Flags take the units the plugin's own parameters use (percentages, Hz, ms), not normalised 0-1
// APVTS values, so a number here maps onto where the knob would sit. --dry/--wet default to 0/100
// rather than the plugin's 100/40 so a render captures the wet path in isolation, with no dry
// click at sample 0 sitting on top of whatever is being measured.
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

    float floatArg(const std::map<std::string, std::string>& args, const std::string& key, float fallback)
    {
        const auto it = args.find(key);
        return it == args.end() ? fallback : std::stof(it->second);
    }

    std::string stringArg(const std::map<std::string, std::string>& args, const std::string& key,
                          const std::string& fallback)
    {
        const auto it = args.find(key);
        return it == args.end() ? fallback : it->second;
    }

    void setParam(wildjag::conv::ConvolutionProcessor& processor, const char* paramID, float rawValue)
    {
        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(rawValue));
    }
}

int main(int argc, char* argv[])
{
    using namespace wildjag::conv;

    const auto args = parseArgs(argc, argv);

    const auto outIt = args.find("out");
    if (outIt == args.end())
    {
        std::fprintf(stderr,
            "Usage: ConvBaseRenderIR --out <path.wav> [--seconds 3.0] [--sampleRate 48000]\n"
            "                        [--blockSize 256] [--channels 2] [--signal impulse|sine|silence]\n"
            "                        [--freq 220] [--ir 0] [--predelay 0] [--length 100] [--attack 0]\n"
            "                        [--lowcut 20] [--highcut 20000] [--dry 0] [--wet 100]\n"
            "                        [--bypassAt -1] [--switchIRAt -1] [--switchIRTo 1]\n");
        return 1;
    }

    const juce::File outFile(juce::File::getCurrentWorkingDirectory().getChildFile(outIt->second));
    outFile.getParentDirectory().createDirectory();

    const auto sampleRate = (double) floatArg(args, "sampleRate", 48000.0f);
    const auto seconds = floatArg(args, "seconds", 3.0f);
    const auto blockSize = (int) floatArg(args, "blockSize", 256.0f);
    const auto channels = (int) floatArg(args, "channels", 2.0f);
    const auto signal = stringArg(args, "signal", "impulse");
    const auto frequency = floatArg(args, "freq", 220.0f);
    const auto bypassAt = floatArg(args, "bypassAt", -1.0f);
    const auto switchIRAt = floatArg(args, "switchIRAt", -1.0f);
    const auto switchIRTo = (int) floatArg(args, "switchIRTo", 1.0f);

    ConvolutionProcessor processor(variantConfig());

    setParam(processor, ConvolutionProcessor::irIndexParamID, floatArg(args, "ir", 0.0f));
    setParam(processor, ConvolutionProcessor::preDelayMsParamID, floatArg(args, "predelay", 0.0f));
    setParam(processor, ConvolutionProcessor::lengthPercentParamID, floatArg(args, "length", 100.0f));
    setParam(processor, ConvolutionProcessor::attackMsParamID, floatArg(args, "attack", 0.0f));
    setParam(processor, ConvolutionProcessor::lowCutHzParamID, floatArg(args, "lowcut", 20.0f));
    setParam(processor, ConvolutionProcessor::highCutHzParamID, floatArg(args, "highcut", 20000.0f));
    setParam(processor, ConvolutionProcessor::dryParamID, floatArg(args, "dry", 0.0f));
    setParam(processor, ConvolutionProcessor::wetParamID, floatArg(args, "wet", 100.0f));

    processor.setPlayConfigDetails(channels, channels, sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);

    const auto totalSamples = (int) std::lround((double) seconds * sampleRate);
    juce::AudioBuffer<float> rendered(channels, totalSamples);
    rendered.clear();

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;

    auto bypassApplied = bypassAt < 0.0f;
    auto switchApplied = switchIRAt < 0.0f;

    for (int position = 0; position < totalSamples; position += blockSize)
    {
        const auto n = std::min(blockSize, totalSamples - position);

        block.clear();
        for (int i = 0; i < n; ++i)
        {
            const auto index = position + i;
            auto value = 0.0f;

            if (signal == "impulse")
                value = index == 0 ? 1.0f : 0.0f;
            else if (signal == "sine")
                value = 0.5f * std::sin(juce::MathConstants<float>::twoPi * frequency
                                        * (float) index / (float) sampleRate);

            for (int channel = 0; channel < channels; ++channel)
                block.setSample(channel, i, value);
        }

        const auto elapsed = (float) position / (float) sampleRate;

        if (! bypassApplied && elapsed >= bypassAt)
        {
            setParam(processor, ConvolutionProcessor::bypassParamID, 1.0f);
            bypassApplied = true;
        }

        if (! switchApplied && elapsed >= switchIRAt)
        {
            setParam(processor, ConvolutionProcessor::irIndexParamID, (float) switchIRTo);
            switchApplied = true;

            // The IR pipeline is asynchronous and debounced by design, and this render runs far
            // faster than real time - without pausing for real wall-clock milliseconds the whole
            // file would be finished before the worker had decoded anything, and the render would
            // silently contain no swap at all. Long enough to clear IRLoadWorker's debounce plus
            // the decode/resample/shape pass.
            juce::Thread::sleep(400);
        }

        processor.processBlock(block, midi);

        for (int channel = 0; channel < channels; ++channel)
            rendered.copyFrom(channel, position, block, channel, 0, n);
    }

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());

    if (stream == nullptr)
    {
        std::fprintf(stderr, "Could not open %s for writing\n", outFile.getFullPathName().toRawUTF8());
        return 1;
    }

    stream->setPosition(0);
    stream->truncate();

    // 32-bit float: these renders are measured, not listened to, and clipping or requantising them
    // would put a floor under exactly the small differences validate.py is looking for.
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.release(), sampleRate, (unsigned int) channels, 32, {}, 0));

    if (writer == nullptr)
    {
        std::fprintf(stderr, "Could not create a WAV writer\n");
        return 1;
    }

    writer->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples());
    writer.reset();

    std::printf("wrote %s (%d ch, %.0f Hz, %d samples, reported latency %d)\n",
                outFile.getFullPathName().toRawUTF8(), channels, sampleRate, totalSamples,
                processor.getLatencySamples());

    return 0;
}
