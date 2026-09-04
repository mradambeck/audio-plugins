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

// Offline render harness for Caverns (mirrors gradient-pitch's GradientRenderIR / shields-reverb's
// ShieldsRenderIR pattern - an audio-in-driven effect, not a synth). Reuses
// Source/Tests/TestCreateEditorStub.cpp (already exists for CavernsTests) so this target stays
// free of the editor/LookAndFeel/BinaryData, matching alloy-bass's AlloyRenderIR.
//
// Usage:
//   CavernsRenderIR --out <path.wav> [--seconds 6] [--sampleRate 44100]
//                   [--preset "<factory preset name>"] [--<paramID> <rawValue>]...
//                   [--changeParamID <id> --changeParamValue <v> --changeAtSecond <t>]
//                   [--sampleRate2 <hz> --warmupSeconds 2]
//
// Flags map 1:1 onto the plugin's own APVTS parameter IDs (PluginProcessor.h) in native units.
// --preset applies one of the 8 factory presets first (setCurrentProgram's own
// setValueNotifyingHost() path); any --<paramID> flags override individual values on top of it.
//
// --changeParamID/--changeParamValue/--changeAtSecond (all three or none) apply ONE parameter
// change at a block boundary partway through the render - proves the filter-coefficient cache
// (see PluginProcessor.cpp's lastLowCutHz/lastHighCutHz/lastDegradeDarkenerHz) invalidates
// correctly on a genuine change, not just "the render used the same value throughout."
//
// --sampleRate2 (with --warmupSeconds) renders in TWO phases: a warm-up phase at --sampleRate
// (discarded, just to establish cached coefficient state), then prepareToPlay() is called AGAIN
// at --sampleRate2 with every parameter left untouched, and only THAT second phase is written to
// --out (at --sampleRate2). This isolates the specific bug the cache design guards against: a
// cached "unchanged Hz" value surviving a sample-rate change and producing wrong-rate
// coefficients - a same-render parameter change alone wouldn't exercise this.
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

    void setParam(CavernsAudioProcessor& processor, const std::string& paramID, float rawValue)
    {
        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(rawValue));
    }

    constexpr const char* allParamIDs[] = {
        CavernsAudioProcessor::bypassParamID, CavernsAudioProcessor::syncParamID,
        CavernsAudioProcessor::linkParamID, CavernsAudioProcessor::leftSubdivisionParamID,
        CavernsAudioProcessor::rightSubdivisionParamID, CavernsAudioProcessor::leftTimeParamID,
        CavernsAudioProcessor::rightTimeParamID, CavernsAudioProcessor::feedbackParamID,
        CavernsAudioProcessor::dryParamID, CavernsAudioProcessor::wetParamID,
        CavernsAudioProcessor::lowCutParamID, CavernsAudioProcessor::highCutParamID,
        CavernsAudioProcessor::modSpeedParamID, CavernsAudioProcessor::modDepthParamID,
        CavernsAudioProcessor::degradeParamID,
    };

    // Same four-segment deterministic test signal design as gradient-pitch's RenderIR.cpp: a
    // two-tone sine mix (harmonic content), a linear sweep, white noise, and a silence tail -
    // proportional to the total duration. L/R differ so both channels get real, non-identical
    // coverage (Caverns' L/R delay times/mod phases can differ independently).
    juce::AudioBuffer<float> buildTestSignal(double sampleRate, float totalSeconds)
    {
        const auto totalSamples = (int) (totalSeconds * sampleRate);
        juce::AudioBuffer<float> signal(2, totalSamples);
        signal.clear();

        auto* left = signal.getWritePointer(0);
        auto* right = signal.getWritePointer(1);

        const auto twoToneEnd = (int) (totalSamples * 0.30);
        const auto sweepEnd = (int) (totalSamples * 0.60);
        const auto noiseEnd = (int) (totalSamples * 0.90);

        constexpr float pi = 3.14159265358979323846f;
        constexpr float amplitude = 0.3f;

        double phaseL1 = 0.0, phaseL2 = 0.0, phaseR1 = 0.0, phaseR2 = 0.0;
        for (int i = 0; i < twoToneEnd; ++i)
        {
            left[i] = amplitude * ((float) std::sin(phaseL1) + (float) std::sin(phaseL2));
            right[i] = amplitude * ((float) std::sin(phaseR1) + (float) std::sin(phaseR2));
            phaseL1 += 2.0 * pi * 220.0 / sampleRate;
            phaseL2 += 2.0 * pi * 440.0 / sampleRate;
            phaseR1 += 2.0 * pi * 246.94 / sampleRate;
            phaseR2 += 2.0 * pi * 493.88 / sampleRate;
        }

        const auto sweepSamples = std::max(1, sweepEnd - twoToneEnd);
        double phaseSweepL = 0.0, phaseSweepR = 0.0;
        for (int i = twoToneEnd; i < sweepEnd; ++i)
        {
            const auto t = (double) (i - twoToneEnd) / (double) sweepSamples;
            const auto freqL = 100.0 + t * (4000.0 - 100.0);
            const auto freqR = 150.0 + t * (3000.0 - 150.0);
            left[i] = amplitude * (float) std::sin(phaseSweepL);
            right[i] = amplitude * (float) std::sin(phaseSweepR);
            phaseSweepL += 2.0 * pi * freqL / sampleRate;
            phaseSweepR += 2.0 * pi * freqR / sampleRate;
        }

        uint32_t rngL = 12345, rngR = 54321;
        auto nextNoise = [](uint32_t& state)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) state / (float) UINT32_MAX * 2.0f - 1.0f;
        };
        for (int i = sweepEnd; i < noiseEnd; ++i)
        {
            left[i] = amplitude * nextNoise(rngL);
            right[i] = amplitude * nextNoise(rngR);
        }

        return signal;
    }

    void renderPhase(CavernsAudioProcessor& processor, const juce::AudioBuffer<float>& inputSignal,
                      juce::AudioBuffer<float>& output, const std::string& changeParamID,
                      float changeParamValue, float changeAtSecond, double sampleRate)
    {
        constexpr int blockSize = 512;
        const auto totalSamples = inputSignal.getNumSamples();
        const auto changeAtSample = changeParamID.empty() ? -1 : (int64_t) (changeAtSecond * sampleRate);

        juce::AudioBuffer<float> block(2, blockSize);
        bool changeApplied = false;

        int written = 0;
        while (written < totalSamples)
        {
            const auto thisBlockSize = std::min(blockSize, totalSamples - written);

            if (!changeApplied && changeAtSample >= 0 && written >= changeAtSample)
            {
                setParam(processor, changeParamID, changeParamValue);
                changeApplied = true;
            }

            block.setSize(2, thisBlockSize, false, false, true);
            block.copyFrom(0, 0, inputSignal, 0, written, thisBlockSize);
            block.copyFrom(1, 0, inputSignal, 1, written, thisBlockSize);

            juce::MidiBuffer midi;
            processor.processBlock(block, midi);

            output.copyFrom(0, written, block, 0, 0, thisBlockSize);
            output.copyFrom(1, written, block, 1, 0, thisBlockSize);

            written += thisBlockSize;
        }
    }
}

int main(int argc, char* argv[])
{
    const auto args = parseArgs(argc, argv);

    const auto outIt = args.find("out");
    if (outIt == args.end())
    {
        std::fprintf(stderr,
            "Usage: CavernsRenderIR --out <path.wav> [--seconds 6] [--sampleRate 44100]\n"
            "                      [--preset \"<name>\"] [--<paramID> <rawValue>]...\n"
            "                      [--changeParamID <id> --changeParamValue <v> --changeAtSecond <t>]\n"
            "                      [--sampleRate2 <hz> --warmupSeconds 2]\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 6.0f);
    constexpr int blockSize = 512;

    CavernsAudioProcessor processor;
    processor.prepareToPlay((double) sampleRate, blockSize);

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

    const auto changeParamID = args.count("changeParamID") ? args.at("changeParamID") : std::string();
    const auto changeParamValue = getFloatArg(args, "changeParamValue", 0.0f);
    const auto changeAtSecond = getFloatArg(args, "changeAtSecond", 0.0f);

    const auto sampleRate2It = args.find("sampleRate2");
    double writeSampleRate = sampleRate;
    juce::AudioBuffer<float> output;

    if (sampleRate2It != args.end())
    {
        // Phase 1 (discarded): establishes cached coefficient state at the original sample rate.
        const auto warmupSeconds = getFloatArg(args, "warmupSeconds", 2.0f);
        const auto warmupSignal = buildTestSignal(sampleRate, warmupSeconds);
        juce::AudioBuffer<float> warmupOutput(2, warmupSignal.getNumSamples());
        renderPhase(processor, warmupSignal, warmupOutput, {}, 0.0f, 0.0f, sampleRate);

        // Phase 2 (written): re-prepare at the new sample rate with every parameter left
        // untouched - if the cache incorrectly survives this, the filters would still be using
        // phase-1-rate-derived coefficients here.
        const auto sampleRate2 = std::stof(sampleRate2It->second);
        processor.prepareToPlay((double) sampleRate2, blockSize);
        writeSampleRate = sampleRate2;

        const auto phase2Signal = buildTestSignal(sampleRate2, seconds);
        output.setSize(2, phase2Signal.getNumSamples());
        output.clear();
        renderPhase(processor, phase2Signal, output, changeParamID, changeParamValue, changeAtSecond, sampleRate2);
    }
    else
    {
        const auto inputSignal = buildTestSignal(sampleRate, seconds);
        output.setSize(2, inputSignal.getNumSamples());
        output.clear();
        renderPhase(processor, inputSignal, output, changeParamID, changeParamValue, changeAtSecond, sampleRate);
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
        wavFormat.createWriterFor(stream.get(), writeSampleRate, 2, 32, {}, 0));

    if (writer == nullptr)
    {
        std::fprintf(stderr, "Could not create WAV writer\n");
        return 1;
    }

    stream.release(); // the writer now owns the stream
    writer->writeFromAudioSampleBuffer(output, 0, output.getNumSamples());

    std::printf("Wrote %s (%.2fs @ %.0fHz)\n", outFile.getFullPathName().toRawUTF8(),
                (double) output.getNumSamples() / writeSampleRate, writeSampleRate);
    return 0;
}
