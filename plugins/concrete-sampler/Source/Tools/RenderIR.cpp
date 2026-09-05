#include "../PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cstdio>
#include <map>
#include <memory>
#include <string>

// Offline render harness for Concrete (mirrors alloy-bass's AlloyRenderIR - a MIDI-driven synth,
// not an audio-in-driven effect). Reuses Source/Tests/TestCreateEditorStub.cpp so this target
// stays free of the editor/LookAndFeel/BinaryData.
//
// Usage:
//   ConcreteRenderIR --out <path.wav> [--seconds 6] [--sampleRate 44100]
//
// Phase 0 only: there is no sample zone or voice engine yet (see
// concrete-sampler-plugin-plan.md), so this renders silence and exists to prove the render/WAV-
// write harness itself is correct ahead of every later phase depending on it.
// --sample/--note/--velocity/--sequence arrive in Phase 1 once ConcreteSampleSet/ConcreteVoice
// exist to give them something to do.
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
}

int main(int argc, char* argv[])
{
    const auto args = parseArgs(argc, argv);

    const auto outIt = args.find("out");
    if (outIt == args.end())
    {
        std::fprintf(stderr, "Usage: ConcreteRenderIR --out <path.wav> [--seconds 6] [--sampleRate 44100]\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 6.0f);
    constexpr int blockSize = 512;

    ConcreteAudioProcessor processor;
    processor.prepareToPlay((double) sampleRate, blockSize);

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

        juce::MidiBuffer midi;
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
