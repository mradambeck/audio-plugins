#include "PluginProcessor.h"
#include "AuraParameterMap.h"

#include <algorithm>
#include <cmath>

namespace
{
    juce::String withSign(float v, int decimals, const char* unit)
    {
        return (v > 0.0f ? juce::String("+") : juce::String()) + juce::String(v, decimals) + " " + unit;
    }
}

AuraAudioProcessor::AuraAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    timeSecondsParam = apvts.getRawParameterValue(timeSecondsParamID);
    lowCutHzParam = apvts.getRawParameterValue(lowCutHzParamID);
    highDbParam = apvts.getRawParameterValue(highDbParamID);
    preDelayMsParam = apvts.getRawParameterValue(preDelayMsParamID);
    bitDepthParam = apvts.getRawParameterValue(bitDepthParamID);
    mixPercentParam = apvts.getRawParameterValue(mixPercentParamID);
    inputGainDbParam = apvts.getRawParameterValue(inputGainDbParamID);
    outputGainDbParam = apvts.getRawParameterValue(outputGainDbParamID);
    bypassParam = apvts.getRawParameterValue(bypassParamID);
}

AuraAudioProcessor::~AuraAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout AuraAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 0.1-5.5s is the measured range (ml-toolkit/effects/ambience/findings.md); extended to 8.0s
    // for extrapolation headroom the same way Intruder's own Decay extends past its own measured
    // range. Close to literal seconds from ~1.1s up (see findings.md). Displayed as "Decay" (Adam,
    // 2026-09-02) even though the underlying parameter ID stays timeSeconds - unlike Intruder's
    // OWN "Decay" control, which is a heavily-compressed display scale on the same hardware unit,
    // Aura's is close to literal seconds; same label, different behavior, don't assume parity
    // between the two plugins' "Decay" knobs just because they share a name.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{timeSecondsParamID, 1},
        "Decay",
        juce::NormalisableRange<float>(0.1f, 8.0f, 0.01f, 0.5f),
        2.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("s")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " s"; })));

    // Repurposed from the real hardware's own "Low" knob (was -8..+6dB, unwired - see
    // AuraFDNEngine.h's setLowCutHz() comment for why: direct measurement found no onset-tone
    // effect and only a small, sign-inconsistent decay effect conditional on High, nothing safe to
    // hardcode). Now a plain 0-300Hz utility high-pass on the dry input, ahead of pre-delay and
    // the whole effect - matches Caverns' own "Low Cut" naming/range-shape convention (though
    // Caverns filters its wet tap, not the dry input - see AuraFDNEngine.h for why Aura's needs to
    // sit earlier). Default 0Hz = off, a genuine bypass (AuraFDNEngine::lowCutActive).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{lowCutHzParamID, 1},
        "Low Cut",
        juce::NormalisableRange<float>(0.0f, 300.0f, 1.0f, 0.5f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) {
                return v <= 0.0f ? juce::String("Off") : juce::String(v, 0) + " Hz"; })));

    // -8..0dB matches the captured range - see findings.md's "High" section for the broadband
    // tilt (bass up/treble down as this goes negative) plus decay-shortening behavior this drives.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{highDbParamID, 1},
        "High",
        juce::NormalisableRange<float>(-8.0f, 0.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    // 0-200ms matches the real RMX16 Ambience program's own pre-delay range (per Adam, 2026-09-02).
    // AuraFDNEngine already allocates 200ms of pre-delay buffer headroom, so this needed no engine
    // change - the parameter was just capped lower than the hardware for no good reason.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{preDelayMsParamID, 1},
        "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

    // 8-24, default 16: reproduces the real unit's measured ~90dB dynamic-range ceiling (see
    // AuraFDNEngine.h's "16-bit converters" note and findings.md's Quantization section,
    // 2026-09-02 - the captures' own decay tails bottom out at -89..-95dB before the capture
    // chain's noise floor takes over, matching what a real 16-bit-class converter delivers in
    // practice). 24 is a GENUINE bypass (AuraFDNEngine::bitDepthActive), matching the
    // "off at the default" contract ShieldsFDNEngine's own Bit Depth control - and its Low Cut,
    // Wobble - already establish, except Aura's default sits mid-range rather than at the
    // transparent end, since 16 is what the real hardware actually measures as.
    //
    // Full 65-capture Phase D re-validation at this default vs. bypass (2026-09-02): log-spectral
    // distance, per-band EQ balance and envelope correlation are all UNCHANGED (2.49dB/+1.96dB/
    // 0.938 either way - -90dB grain is far below what those whole-tail metrics can resolve).
    // Crest-factor diff improved (-0.38dB -> +0.26dB, closer to zero). Spectral-flatness diff got
    // WORSE (+1.06dB -> +1.85dB, i.e. further from matching): undithered quantization adds
    // broadband (maximally flat) grain to the tail, and the plugin's tail already read flatter/
    // more diffuse than the real hardware's own more-tonal decay before this was added, so the
    // grain pushes further the wrong way on that one metric. Unlike ShieldsFDNEngine's default of
    // 13 (chosen BECAUSE grain measurably improved its LSD match), Aura's default of 16 is NOT a
    // validation-metric win - it's chosen because it's what the real hardware measures as,
    // consistent with this project's standing rule that the aggregate metric doesn't get the last
    // word (see AuraDecayGainData.h's whole "Lesson worth keeping" note). If the flatness
    // direction ever turns out to matter more than dynamic-range accuracy on further (by-ear)
    // listening, reconsider the default - not the mechanism.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{bitDepthParamID, 1},
        "Bit Depth",
        juce::NormalisableRange<float>(8.0f, 24.0f, 0.1f),
        16.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("bit")
            .withStringFromValueFunction([](float v, int) {
                return v >= 24.0f ? juce::String("Off") : juce::String(v, 1) + " bit"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{mixPercentParamID, 1},
        "Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 0) + " %"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{inputGainDbParamID, 1},
        "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{outputGainDbParamID, 1},
        "Volume",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1}, "Bypass", false));

    return {params.begin(), params.end()};
}

void AuraAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    sampleRateHz = sampleRate;
    engine.prepare(sampleRate);
}

void AuraAudioProcessor::releaseResources() {}

bool AuraAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void AuraAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    if (buffer.getNumChannels() < 2)
        return;

    // Phase 4-equivalent mapping layer: Time/High are hardware-meaningful UI values, not DSP-
    // ready coefficients - AuraParameterMap converts them via the fitted reference curves
    // (ml-toolkit/effects/ambience/findings.md), kept separate from AuraFDNEngine so the mapping
    // can be refit without touching the DSP (same convention as Intruder).
    const auto decayParams = AuraParameterMap::mapTimeAndHighToDecayParams(timeSecondsParam->load(), highDbParam->load());
    engine.setBandGains(decayParams.decayGain, decayParams.decayGain);
    engine.setDampingWeight(decayParams.dampingWeight);
    engine.setInputTilt(AuraParameterMap::mapInputTiltDb(highDbParam->load()));
    engine.setPreDelayMs(preDelayMsParam->load());
    engine.setBitDepth(bitDepthParam->load());
    engine.setLowCutHz(lowCutHzParam->load());

    const auto inputGain = std::pow(10.0f, inputGainDbParam->load() / 20.0f);
    const auto outputGain = std::pow(10.0f, outputGainDbParam->load() / 20.0f);
    const auto mix = std::clamp(mixPercentParam->load() * 0.01f, 0.0f, 1.0f);

    const auto numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    juce::HeapBlock<float> dryL(static_cast<size_t>(numSamples));
    juce::HeapBlock<float> dryR(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
    {
        dryL[n] = left[n];
        dryR[n] = right[n];
        left[n] *= inputGain;
        right[n] *= inputGain;
    }

    engine.processStereo(left, right, numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        const auto wetL = left[n] * outputGain;
        const auto wetR = right[n] * outputGain;
        left[n] = dryL[n] * (1.0f - mix) + wetL * mix;
        right[n] = dryR[n] * (1.0f - mix) + wetR * mix;
    }
}

// Defined in PluginEditor.cpp (not here) so this file has no GUI dependency.

bool AuraAudioProcessor::hasEditor() const { return true; }

const juce::String AuraAudioProcessor::getName() const { return JucePlugin_Name; }

bool AuraAudioProcessor::acceptsMidi() const { return false; }
bool AuraAudioProcessor::producesMidi() const { return false; }
bool AuraAudioProcessor::isMidiEffect() const { return false; }
double AuraAudioProcessor::getTailLengthSeconds() const { return 10.0; }

int AuraAudioProcessor::getNumPrograms() { return 1; }
int AuraAudioProcessor::getCurrentProgram() { return currentProgramIndex; }
void AuraAudioProcessor::setCurrentProgram(int) {}
const juce::String AuraAudioProcessor::getProgramName(int) { return {}; }
void AuraAudioProcessor::changeProgramName(int, const juce::String&) {}

void AuraAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void AuraAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuraAudioProcessor();
}
