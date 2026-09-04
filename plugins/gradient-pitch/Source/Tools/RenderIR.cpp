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

// Offline render harness for Gradient (mirrors shields-reverb's ShieldsRenderIR/RenderIR.cpp
// pattern). Feeds a synthesized, deterministic multi-segment test signal through the real
// GradientAudioProcessor (not a re-implementation of the DSP) and writes the result to a WAV file,
// so a pre/post-change pair can be diffed byte-for-byte (`cmp`) or scored via
// ../common/tools/compare_wavs.py, without ever needing to load the plugin in a DAW.
//
// An impulse alone (Shields' approach - it's a reverb, characterized by its impulse response)
// wouldn't exercise this engine meaningfully: it's a pitch shifter, whose audible behaviour (splice
// crossfades, feedback regeneration, drift) only shows up with sustained tonal/noisy content. The
// synthesized signal below (two-tone sine, sweep, noise, silence tail - see buildTestSignal()) is
// generated in-process with no external file dependency, and is fully deterministic EXCEPT for
// Drift, whose RNG is seeded from each engine instance's own memory address (see
// GradientPitchShiftEngine::prepare()) and therefore varies between separate process invocations
// under ASLR - use --driftSeed to pin it for reproducible renders when Drift > 0.
//
// Usage:
//   GradientRenderIR --out <path.wav> [--seconds 5.0] [--sampleRate 44100] [--driftSeed <n>]
//                    [--pitchSemitonesA 7] [--pitchFineCentsA 0] [--delayTimeMsA 250]
//                    [--feedbackPercentA 0] [--spliceModeA glitch|soft|smart]
//                    [--crossfadeLengthMsA 8] [--driftAmountA 0] [--mixPercentA 100]
//                    [--outputTrimDbA 0]
//                    [...same flags with a B suffix for unit B...]
//                    [--dualModeEnabled 0] [--widthPercent 100]
//                    [--linkEnabled 0] [--linkPitchIntervalSemitones 0] [--linkDelayIntervalMs 0]
//                    [--crossFeedbackEnabled 0] [--bypass 0]
//                    [--monoIn 0] [--forceMonoContent 0]
//
// Flags map 1:1 onto the plugin's own APVTS parameter IDs (PluginProcessor.h) in the plugin's own
// native units - not normalised 0-1 - so a value copied from this tool's output maps directly onto
// where the corresponding knob would sit. Defaults below are NOT the plugin's shipped defaults
// (which mostly leave the pitch-shift engine in its near-bypass state: 0 semitones, 0% feedback,
// 50% mix) - deliberately overridden (Shields' RenderIR does the same for dry/wet) so an
// unqualified run actually exercises pitch-shifting, at full wet, rather than rendering a mostly-
// dry pass-through.
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

    void setParam(GradientAudioProcessor& processor, const char* paramID, float rawValue)
    {
        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(rawValue));
    }

    // Splice Mode is an AudioParameterChoice over {"Normal", "Soft", "Smart"} (PluginProcessor.cpp),
    // matching GradientPitchShiftEngine::SpliceMode's {glitch, deglitchSoft, deglitchSmart} order.
    float spliceModeIndexFromString(const std::string& name)
    {
        if (name == "soft") return 1.0f;
        if (name == "smart") return 2.0f;
        return 0.0f; // "glitch" / anything else -> Normal
    }

    // Deterministic, tool-local noise source - NOT the engine's own Drift RNG - so the input test
    // signal itself is always identical across runs regardless of --driftSeed/ASLR.
    struct SimpleRng
    {
        uint32_t state;
        explicit SimpleRng(uint32_t seed) : state(seed == 0 ? 1 : seed) {}
        float next()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) state / (float) UINT32_MAX * 2.0f - 1.0f;
        }
    };

    // Builds a deterministic stereo test signal in four equal-length segments (proportions of the
    // total duration, so the same code produces both a short correctness-matrix render and a long
    // timing-benchmark render): a two-tone sine mix (harmonic content - exposes splice artifacts),
    // a linear sine sweep (exercises boundary-crossing at varying rates), white noise (stresses the
    // DC blocker/feedback path), and a silence tail (tests decay/self-oscillation persistence). L
    // and R use different tones/seeds throughout - engine A and B are driven by structurally
    // different code paths in processBlock (direct vs. Link-override branch), so identical L/R
    // content would under-cover a bug that only manifests on B's path.
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
        // Remainder through totalSamples is left silent (the tail).

        constexpr float pi = 3.14159265358979323846f;
        constexpr float amplitude = 0.25f;

        double phaseL1 = 0.0, phaseL2 = 0.0, phaseR1 = 0.0, phaseR2 = 0.0;
        for (int i = 0; i < twoToneEnd; ++i)
        {
            left[i] = amplitude * ((float) std::sin(phaseL1) + (float) std::sin(phaseL2));
            right[i] = amplitude * ((float) std::sin(phaseR1) + (float) std::sin(phaseR2));
            phaseL1 += 2.0 * pi * 220.0 / sampleRate;
            phaseL2 += 2.0 * pi * 440.0 / sampleRate;
            phaseR1 += 2.0 * pi * 246.94 / sampleRate; // B3
            phaseR2 += 2.0 * pi * 493.88 / sampleRate; // B4 - minor third above L's pair
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

        SimpleRng noiseL(12345), noiseR(54321);
        for (int i = sweepEnd; i < noiseEnd; ++i)
        {
            left[i] = amplitude * noiseL.next();
            right[i] = amplitude * noiseR.next();
        }

        return signal;
    }
}

int main(int argc, char* argv[])
{
    const auto args = parseArgs(argc, argv);

    const auto outIt = args.find("out");
    if (outIt == args.end())
    {
        std::fprintf(stderr,
            "Usage: GradientRenderIR --out <path.wav> [--seconds 5.0] [--sampleRate 44100]\n"
            "                       [--driftSeed <n>] [--pitchSemitonesA 7] [--delayTimeMsA 250]\n"
            "                       [--feedbackPercentA 0] [--spliceModeA glitch|soft|smart]\n"
            "                       [--driftAmountA 0] [--mixPercentA 100] [--dualModeEnabled 0] ...\n"
            "See this file's header comment for the full flag list.\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 5.0f);

    GradientAudioProcessor processor;

    auto setUnitParams = [&](const std::string& suffix, float defaultPitch)
    {
        setParam(processor, ("pitchSemitones" + suffix).c_str(), getFloatArg(args, "pitchSemitones" + suffix, defaultPitch));
        setParam(processor, ("pitchFineCents" + suffix).c_str(), getFloatArg(args, "pitchFineCents" + suffix, 0.0f));
        setParam(processor, ("delayTimeMs" + suffix).c_str(), getFloatArg(args, "delayTimeMs" + suffix, 250.0f));
        setParam(processor, ("delaySyncEnabled" + suffix).c_str(), getFloatArg(args, "delaySyncEnabled" + suffix, 0.0f));
        setParam(processor, ("delaySubdivision" + suffix).c_str(), getFloatArg(args, "delaySubdivision" + suffix, 5.0f));
        setParam(processor, ("feedbackPercent" + suffix).c_str(), getFloatArg(args, "feedbackPercent" + suffix, 0.0f));
        setParam(processor, ("spliceMode" + suffix).c_str(),
            spliceModeIndexFromString(getStringArg(args, "spliceMode" + suffix, "glitch")));
        setParam(processor, ("crossfadeLengthMs" + suffix).c_str(), getFloatArg(args, "crossfadeLengthMs" + suffix, 8.0f));
        setParam(processor, ("driftAmount" + suffix).c_str(), getFloatArg(args, "driftAmount" + suffix, 0.0f));
        setParam(processor, ("mixPercent" + suffix).c_str(), getFloatArg(args, "mixPercent" + suffix, 100.0f));
        setParam(processor, ("outputTrimDb" + suffix).c_str(), getFloatArg(args, "outputTrimDb" + suffix, 0.0f));
    };

    setUnitParams("A", 7.0f);
    setUnitParams("B", -5.0f); // deliberately different from A's default so dual-mode renders are meaningful

    setParam(processor, GradientAudioProcessor::dualModeEnabledParamID, getFloatArg(args, "dualModeEnabled", 0.0f));
    setParam(processor, GradientAudioProcessor::widthPercentParamID, getFloatArg(args, "widthPercent", 100.0f));
    setParam(processor, GradientAudioProcessor::linkEnabledParamID, getFloatArg(args, "linkEnabled", 0.0f));
    setParam(processor, GradientAudioProcessor::linkPitchIntervalSemitonesParamID, getFloatArg(args, "linkPitchIntervalSemitones", 0.0f));
    setParam(processor, GradientAudioProcessor::linkDelayIntervalMsParamID, getFloatArg(args, "linkDelayIntervalMs", 0.0f));
    setParam(processor, GradientAudioProcessor::crossFeedbackEnabledParamID, getFloatArg(args, "crossFeedbackEnabled", 0.0f));
    setParam(processor, GradientAudioProcessor::bypassParamID, getFloatArg(args, "bypass", 0.0f));

    // --monoIn 1: negotiates a mono-input/stereo-output bus layout (as a mono host track would) and
    // deliberately leaves the render buffer's channel 1 unpopulated below - processBlock() must
    // derive it from channel 0 itself via getTotalNumInputChannels() < 2, exactly like a real mono
    // host buffer, not from this tool feeding it real content.
    //
    // --forceMonoContent 1: independent of --monoIn - makes the STEREO bus path (the pre-existing,
    // unmodified-by-the-mono-fix code) receive identical L=R content instead of the normal L!=R test
    // signal. Pairing `--forceMonoContent 1` (stereo bus) against `--monoIn 1` (mono bus, same other
    // params) with otherwise-identical settings gives two renders that SHOULD be byte-identical if
    // the mono-input fix is correct: both ultimately run the same per-sample computation against
    // identical L=R content, one via real stereo channels and one via the mono-duplication path.
    const bool monoIn = getFloatArg(args, "monoIn", 0.0f) > 0.5f;
    const bool forceMonoContent = getFloatArg(args, "forceMonoContent", 0.0f) > 0.5f;

    if (monoIn)
    {
        juce::AudioProcessor::BusesLayout monoInStereoOut;
        monoInStereoOut.inputBuses.add(juce::AudioChannelSet::mono());
        monoInStereoOut.outputBuses.add(juce::AudioChannelSet::stereo());
        if (! processor.setBusesLayout(monoInStereoOut))
        {
            std::fprintf(stderr, "setBusesLayout(mono-in/stereo-out) was rejected\n");
            return 1;
        }
    }

    constexpr int blockSize = 512;
    processor.prepareToPlay((double) sampleRate, blockSize);

    const auto driftSeedIt = args.find("driftSeed");
    if (driftSeedIt != args.end())
    {
        const auto seedA = (uint32_t) std::stoul(driftSeedIt->second);
        processor.setDriftSeedForTesting(seedA, seedA + 1); // never identical - preserves the "two engines never lock-step" property under deterministic testing
    }

    const auto inputSignal = buildTestSignal(sampleRate, seconds);
    const auto totalSamples = inputSignal.getNumSamples();

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
        if (! monoIn)
            block.copyFrom(1, 0, inputSignal, forceMonoContent ? 0 : 1, written, thisBlockSize);
        // else: monoIn - channel 1 is deliberately left as whatever the buffer already held.

        processor.processBlock(block, midi);

        output.copyFrom(0, written, block, 0, 0, thisBlockSize);
        output.copyFrom(1, written, block, 1, 0, thisBlockSize);

        written += thisBlockSize;
    }

    outFile.getParentDirectory().createDirectory();

    // File::createOutputStream() appends at the current end of an existing file rather than
    // truncating it - deleting first is the standard JUCE idiom for "overwrite" (see Shields'
    // RenderIR.cpp, same reasoning).
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
