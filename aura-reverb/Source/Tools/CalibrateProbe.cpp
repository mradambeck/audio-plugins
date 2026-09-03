#include "../AuraFDNEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cstdio>
#include <map>
#include <memory>
#include <string>

// Calibration-only probe, NOT part of the shipped plugin: renders AuraFDNEngine directly at RAW
// engine coefficients (decayGain/dampingWeight), bypassing AuraParameterMap's Time/High curves
// entirely. Exists so AuraDecayGainData.h's binary-search calibration (alternating
// gain<-RT60, damping<-tonal-balance against the real captures, per that file's own comment) has
// something to drive - AuraRenderIR can't do this, it only exposes UI-facing Time/High and always
// goes through the fitted curves. Neutral on everything else this doesn't test: no pre-delay, no
// input tilt, no low cut, bit depth bypassed (24) - isolates the two parameters being calibrated.
//
// Built 2026-09-02 for a since-reverted attempt at the sub-bass decay-rate limit documented in
// AuraFDNEngine.h (inputHighPassL's KNOWN LIMIT comment): an asymmetric 16-line topology (the
// original calibrated 8 lines plus 8 new, longer, extra-damped lines meant to add low-frequency
// modal density without the short-Time density regression the earlier uniform-doubling attempt
// hit). Calibration surfaced a harder problem than density: the new lines' own transit time (up
// to ~189ms) put a hard floor on achievable decay time - even at near-zero feedback gain, RT60
// couldn't go below ~0.6s, well above the 0.1s setting's own ~0.5s target, because a long line's
// first pass alone takes that long regardless of gain. A real fix would need the extra lines'
// participation gated by Time (not just darkened), a bigger change than a recalibration - see
// git history around this comment for the full numbers if picking this back up. The probe itself
// stayed useful as a general decayGain/dampingWeight calibration tool independent of that
// specific attempt, so it was kept.
//
// Usage:
//   AuraCalibrateProbe --out <path.wav> [--seconds 10] [--sampleRate 44100]
//                       [--decayGain 0.9] [--dampingWeight 0.9]
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
        std::fprintf(stderr,
            "Usage: AuraCalibrateProbe --out <path.wav> [--seconds 10] [--sampleRate 44100]\n"
            "                          [--decayGain 0.9] [--dampingWeight 0.9]\n");
        return 1;
    }

    const juce::File outFile(outIt->second);
    const auto sampleRate = getFloatArg(args, "sampleRate", 44100.0f);
    const auto seconds = getFloatArg(args, "seconds", 10.0f);
    const auto totalSamples = (int) (seconds * sampleRate);
    const auto decayGain = getFloatArg(args, "decayGain", 0.9f);
    const auto dampingWeight = getFloatArg(args, "dampingWeight", 0.9f);

    AuraFDNEngine engine;
    engine.prepare((double) sampleRate);
    engine.setBandGains(decayGain, decayGain);
    engine.setDampingWeight(dampingWeight);
    engine.setInputTilt(0.0f);
    engine.setInputHighPassHz(1.0f); // effectively off - see setInputHighPassHz's own clamp
    engine.setLowCutHz(0.0f);
    engine.setBitDepth(24.0f);
    engine.setPreDelayMs(0.0f);

    std::vector<float> left((size_t) totalSamples, 0.0f), right((size_t) totalSamples, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;
    engine.processStereo(left.data(), right.data(), totalSamples);

    juce::AudioBuffer<float> output(2, totalSamples);
    for (int n = 0; n < totalSamples; ++n)
    {
        output.setSample(0, n, left[(size_t) n]);
        output.setSample(1, n, right[(size_t) n]);
    }

    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    if (stream == nullptr)
    {
        std::fprintf(stderr, "Could not open %s for writing\n", outFile.getFullPathName().toRawUTF8());
        return 1;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), (double) sampleRate, 2, 32, {}, 0));
    if (writer == nullptr)
    {
        std::fprintf(stderr, "Could not create WAV writer\n");
        return 1;
    }

    stream.release();
    writer->writeFromAudioSampleBuffer(output, 0, output.getNumSamples());

    std::printf("Wrote %s (%.2fs @ %.0fHz, decayGain=%.5f dampingWeight=%.5f)\n",
        outFile.getFullPathName().toRawUTF8(), (float) totalSamples / sampleRate, sampleRate,
        decayGain, dampingWeight);
    return 0;
}
