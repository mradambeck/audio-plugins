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

// Offline render harness for Intruder (mirrors shields-reverb's ShieldsRenderIR / gradient-pitch's
// GradientRenderIR pattern). Feeds a single impulse through the real IntruderAudioProcessor (not a
// re-implementation of the DSP) and writes the result to WAV, for Phase 6 validation against the
// reference IRs in ir-captures/ via analysis/validate.py, or scoring via
// ../common/tools/compare_wavs.py.
//
// Usage:
//   IntruderRenderIR --out <path.wav> [--seconds 3.0] [--sampleRate 44100]
//                     [--decaySeconds 1.5] [--preDelayMs 0] [--tiltDb 0] [--tighter 0]
//                     [--mixPercent 100] [--inputGainDb 0] [--outputGainDb 0] [--bypass 0]
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

    std::string getStringArg(const std::map<std::string, std::string>& args, const std::string& key, const std::string& defaultValue)
    {
        const auto it = args.find(key);
        return it == args.end() ? defaultValue : it->second;
    }

    void setParam(IntruderAudioProcessor& processor, const char* paramID, float rawValue)
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
            "Usage: IntruderRenderIR --out <path.wav> [--seconds 3.0] [--sampleRate 44100]\n"
            "                        [--decaySeconds 1.5] [--preDelayMs 0] [--tiltDb 0]\n"
            "                        [--tighter 0] [--mixPercent 100] [--inputGainDb 0]\n"
            "                        [--outputGainDb 0] [--triggerThresholdDb -36] [--bypass 0]\n"
            "                        [--inputWav <path.wav>] (feeds a real file through the\n"
            "                        processor at its own native sample rate instead of a\n"
            "                        synthetic --testSignal; overrides --sampleRate/--seconds)\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 3.0f);
    const auto inputWavIt = args.find("inputWav");

    juce::AudioBuffer<float> inputSignal;
    int totalSamples = 0;

    if (inputWavIt != args.end())
    {
        const juce::File inFile(inputWavIt->second);
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatReader> reader(
            wavFormat.createReaderFor(inFile.createInputStream().release(), true));
        if (reader == nullptr)
        {
            std::fprintf(stderr, "Could not read --inputWav: %s\n", inFile.getFullPathName().toRawUTF8());
            return 1;
        }
        sampleRate = (float) reader->sampleRate; // native rate, not resampled
        totalSamples = (int) reader->lengthInSamples;
        inputSignal.setSize(2, totalSamples);
        reader->read(&inputSignal, 0, totalSamples, 0, true, reader->numChannels > 1);
        if (reader->numChannels == 1)
            inputSignal.copyFrom(1, 0, inputSignal, 0, 0, totalSamples);
    }
    else
    {
        totalSamples = (int) (seconds * sampleRate);
        inputSignal.setSize(2, totalSamples);
        inputSignal.clear();
    }

    IntruderAudioProcessor processor;

    setParam(processor, IntruderAudioProcessor::decaySecondsParamID, getFloatArg(args, "decaySeconds", 1.5f));
    setParam(processor, IntruderAudioProcessor::preDelayMsParamID, getFloatArg(args, "preDelayMs", 0.0f));
    setParam(processor, IntruderAudioProcessor::tiltDbParamID, getFloatArg(args, "tiltDb", 0.0f));
    setParam(processor, IntruderAudioProcessor::tighterParamID, getFloatArg(args, "tighter", 0.0f));
    setParam(processor, IntruderAudioProcessor::mixPercentParamID, getFloatArg(args, "mixPercent", 100.0f));
    setParam(processor, IntruderAudioProcessor::inputGainDbParamID, getFloatArg(args, "inputGainDb", 0.0f));
    setParam(processor, IntruderAudioProcessor::outputGainDbParamID, getFloatArg(args, "outputGainDb", 0.0f));
    setParam(processor, IntruderAudioProcessor::triggerThresholdDbParamID, getFloatArg(args, "triggerThresholdDb", -36.0f));
    setParam(processor, IntruderAudioProcessor::bypassParamID, getFloatArg(args, "bypass", 0.0f));

    constexpr int blockSize = 512;
    processor.prepareToPlay((double) sampleRate, blockSize);

    const auto testSignal = getStringArg(args, "testSignal", "impulse");
    if (inputWavIt != args.end())
    {
        // Real-file mode already populated inputSignal above - skip the synthetic generators.
    }
    else if (testSignal == "heldNoteWithDip")
    {
        // A single held note, decaying gradually (not a hard on/off gate - see "sustained"
        // below for that), with one brief (15ms) dip in level partway through, mimicking natural
        // musical dynamics (vibrato, a hand easing off, pick noise settling) rather than an
        // actual new note or a real stop. Built to test whether a momentary threshold dip on an
        // otherwise-continuous note causes a spurious envelope retrigger ("ping").
        auto* left = inputSignal.getWritePointer(0);
        auto* right = inputSignal.getWritePointer(1);
        constexpr float pi = 3.14159265358979323846f;
        double phase = 0.0;
        constexpr float toneHz = 220.0f;
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto t = (double) i / sampleRate;
            float envelope;
            if (t < 0.2) envelope = 0.0f; // silence before the note starts
            else if (t < 0.25) envelope = (float) ((t - 0.2) / 0.05); // 50ms attack ramp
            else envelope = (float) std::exp(-(t - 0.25) * 0.6); // slow natural-ish decay over several seconds
            // One brief 15ms dip to true silence at t=1.5s, then back to the same decaying
            // envelope - simulating a momentary dynamic dip (not a real note-off). Silence, not
            // just "quieter", so this reliably crosses the trigger floor regardless of Gain
            // staging - an earlier version only attenuated by 20x, which stayed above the
            // trigger floor entirely once combined with a hot input Gain setting.
            if (t > 1.5 && t < 1.515)
                envelope = 0.0f;
            const auto s = 0.5f * envelope * (float) std::sin(phase);
            left[i] = s;
            right[i] = s;
            phase += 2.0 * pi * toneHz / sampleRate;
        }
    }
    else if (testSignal == "sustained")
    {
        // Continuous playing, not a single hit: a held tone with a few quiet gaps, to reproduce
        // "volume drops out occasionally while playing" - an impulse alone can't exercise the
        // envelope's retrigger-on-transient behavior over a sustained passage.
        auto* left = inputSignal.getWritePointer(0);
        auto* right = inputSignal.getWritePointer(1);
        constexpr float pi = 3.14159265358979323846f;
        double phase = 0.0;
        constexpr float amplitude = 0.4f;
        constexpr float toneHz = 220.0f;
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto t = (double) i / sampleRate;
            // Silent for the first 0.2s (envelope starts closed, matching a real host), then tone
            // continuously except for two brief gaps to test retrigger.
            bool silent = t < 0.2 || (t > 2.0 && t < 2.15) || (t > 4.0 && t < 4.4);
            const auto s = silent ? 0.0f : amplitude * (float) std::sin(phase);
            left[i] = s;
            right[i] = s;
            phase += 2.0 * pi * toneHz / sampleRate;
        }
    }
    else
    {
        inputSignal.setSample(0, 0, 1.0f);
        inputSignal.setSample(1, 0, 1.0f);
    }

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
    outFile.deleteFile(); // File::createOutputStream() appends, not truncates - see Shields'/Gradient's RenderIR.cpp

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
