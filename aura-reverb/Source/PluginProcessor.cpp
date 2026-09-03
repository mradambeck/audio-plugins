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

    // Factory presets: raw parameter values (the same values setValueNotifyingHost() takes after
    // normalising, not display percentages) applied in one shot when the preset is selected -
    // decoded directly from .aupreset files Adam saved via Logic's native preset UI (2026-09-03,
    // ~/Library/Audio/Presets/Wild Jag/Aura - Reverb/), same convention as every other Wild Jag
    // plugin's own getFactoryPresets() (see common/Presets/FactoryPreset.h's class comment). Dry/Wet
    // are 0%/100% (pure wet) in every one of these - Adam's own choice for auditioning the reverb
    // in isolation.
    //
    // "Default"'s own values are also createParameterLayout()'s actual parameter defaults (Adam,
    // 2026-09-03: a freshly-instantiated plugin should sound identical to selecting "Default" from
    // this menu) - kept as an explicit, selectable entry here too (rather than removed as
    // redundant) so it's still reachable as a genuine "reset to default" menu choice after wandering
    // to another preset, same as every other plugin's own "Default"/"Init"-style entry.
    const std::vector<wildjag::FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<wildjag::FactoryPreset> presets = {
            { "Default", {
                { AuraAudioProcessor::bitDepthParamID, 2.0f },
                { AuraAudioProcessor::bypassParamID, 0.0f },
                { AuraAudioProcessor::dryParamID, 0.0f },
                { AuraAudioProcessor::highDbParamID, 0.0f },
                { AuraAudioProcessor::lowCutHzParamID, 10.0f },
                { AuraAudioProcessor::preDelayMsParamID, 32.0f },
                { AuraAudioProcessor::timeSecondsParamID, 1.309999942779541f },
                { AuraAudioProcessor::wetParamID, 100.0f },
            } },
            { "Far Out", {
                { AuraAudioProcessor::bitDepthParamID, 2.0f },
                { AuraAudioProcessor::bypassParamID, 0.0f },
                { AuraAudioProcessor::dryParamID, 0.0f },
                { AuraAudioProcessor::highDbParamID, -2.199999809265137f },
                { AuraAudioProcessor::lowCutHzParamID, 38.0f },
                { AuraAudioProcessor::preDelayMsParamID, 46.10000228881836f },
                { AuraAudioProcessor::timeSecondsParamID, 2.879999876022339f },
                { AuraAudioProcessor::wetParamID, 100.0f },
            } },
            { "It's a Vibe", {
                { AuraAudioProcessor::bitDepthParamID, 1.0f },
                { AuraAudioProcessor::bypassParamID, 0.0f },
                { AuraAudioProcessor::dryParamID, 0.0f },
                { AuraAudioProcessor::highDbParamID, -4.599999904632568f },
                { AuraAudioProcessor::lowCutHzParamID, 74.0f },
                { AuraAudioProcessor::preDelayMsParamID, 14.5f },
                { AuraAudioProcessor::timeSecondsParamID, 1.870000004768372f },
                { AuraAudioProcessor::wetParamID, 100.0f },
            } },
            { "No Cap", {
                { AuraAudioProcessor::bitDepthParamID, 0.0f },
                { AuraAudioProcessor::bypassParamID, 0.0f },
                { AuraAudioProcessor::dryParamID, 0.0f },
                { AuraAudioProcessor::highDbParamID, -4.800000190734863f },
                { AuraAudioProcessor::lowCutHzParamID, 220.0f },
                { AuraAudioProcessor::preDelayMsParamID, 0.0f },
                { AuraAudioProcessor::timeSecondsParamID, 4.519999980926514f },
                { AuraAudioProcessor::wetParamID, 100.0f },
            } },
            { "Rizz", {
                { AuraAudioProcessor::bitDepthParamID, 0.0f },
                { AuraAudioProcessor::bypassParamID, 0.0f },
                { AuraAudioProcessor::dryParamID, 0.0f },
                { AuraAudioProcessor::highDbParamID, 0.0f },
                { AuraAudioProcessor::lowCutHzParamID, 220.0f },
                { AuraAudioProcessor::preDelayMsParamID, 0.0f },
                { AuraAudioProcessor::timeSecondsParamID, 1.649999976158142f },
                { AuraAudioProcessor::wetParamID, 100.0f },
            } },
            { "That's Tight", {
                { AuraAudioProcessor::bitDepthParamID, 2.0f },
                { AuraAudioProcessor::bypassParamID, 0.0f },
                { AuraAudioProcessor::dryParamID, 0.0f },
                { AuraAudioProcessor::highDbParamID, -5.800000190734863f },
                { AuraAudioProcessor::lowCutHzParamID, 131.0f },
                { AuraAudioProcessor::preDelayMsParamID, 55.70000076293945f },
                { AuraAudioProcessor::timeSecondsParamID, 0.550000011920929f },
                { AuraAudioProcessor::wetParamID, 100.0f },
            } },
            { "Thirst Trap", {
                { AuraAudioProcessor::bitDepthParamID, 1.0f },
                { AuraAudioProcessor::bypassParamID, 0.0f },
                { AuraAudioProcessor::dryParamID, 0.0f },
                { AuraAudioProcessor::highDbParamID, -1.799999952316284f },
                { AuraAudioProcessor::lowCutHzParamID, 121.0f },
                { AuraAudioProcessor::preDelayMsParamID, 20.0f },
                { AuraAudioProcessor::timeSecondsParamID, 1.039999961853027f },
                { AuraAudioProcessor::wetParamID, 100.0f },
            } },
        };

        return presets;
    }
}

AuraAudioProcessor::AuraAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets())
{
    timeSecondsParam = apvts.getRawParameterValue(timeSecondsParamID);
    lowCutHzParam = apvts.getRawParameterValue(lowCutHzParamID);
    highDbParam = apvts.getRawParameterValue(highDbParamID);
    preDelayMsParam = apvts.getRawParameterValue(preDelayMsParamID);
    bitDepthParam = apvts.getRawParameterValue(bitDepthParamID);
    dryParam = apvts.getRawParameterValue(dryParamID);
    wetParam = apvts.getRawParameterValue(wetParamID);
    bypassParam = apvts.getRawParameterValue(bypassParamID);
}

AuraAudioProcessor::~AuraAudioProcessor() = default;

const juce::StringArray& AuraAudioProcessor::getConverterChoices()
{
    static const juce::StringArray choices { "8 bit", "16 bit", "24 bit" };
    return choices;
}

float AuraAudioProcessor::getBitDepthForChoiceIndex(int index)
{
    switch (juce::jlimit(0, 2, index))
    {
        case 0:  return 8.0f;
        case 1:  return 16.0f;
        default: return 24.0f;
    }
}

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
    //
    // Default 1.31s (Adam, 2026-09-03) matches the "Default" factory preset exactly (see
    // getFactoryPresets() below) - Adam's explicit request that a freshly-instantiated plugin
    // sound identical to selecting "Default" from the preset menu, not a separately-chosen value.
    // Every parameter's default below follows the same rule; see dryParamID's own comment for the
    // one place that actually changes runtime behavior (Low Cut goes from off to active).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{timeSecondsParamID, 1},
        "Decay",
        juce::NormalisableRange<float>(0.1f, 8.0f, 0.01f, 0.5f),
        1.309999942779541f,
        juce::AudioParameterFloatAttributes()
            .withLabel("s")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " s"; })));

    // Repurposed from the real hardware's own "Low" knob (was -8..+6dB, unwired - see
    // AuraFDNEngine.h's setLowCutHz() comment for why: direct measurement found no onset-tone
    // effect and only a small, sign-inconsistent decay effect conditional on High, nothing safe to
    // hardcode). Now a plain 0-300Hz utility high-pass on the dry input, ahead of pre-delay and
    // the whole effect - matches Caverns' own "Low Cut" naming/range-shape convention (though
    // Caverns filters its wet tap, not the dry input - see AuraFDNEngine.h for why Aura's needs to
    // sit earlier). 0Hz is a genuine bypass (AuraFDNEngine::lowCutActive) - still true at 10Hz's
    // new default below, it's just no longer WHERE the default sits. This is the one parameter
    // where matching the "Default" preset (see timeSecondsParamID's comment) actually changes
    // runtime behavior at startup: the filter is now active (10Hz) rather than off by default.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{lowCutHzParamID, 1},
        "Low Cut",
        juce::NormalisableRange<float>(0.0f, 300.0f, 1.0f, 0.5f),
        10.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) {
                return v <= 0.0f ? juce::String("Off") : juce::String(v, 0) + " Hz"; })));

    // -8..0dB matches the captured range - see findings.md's "High" section for the broadband
    // tilt (bass up/treble down as this goes negative) plus decay-shortening behavior this drives.
    // Displayed as "Color" (Adam, 2026-09-02) - underlying param ID stays highDb.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{highDbParamID, 1},
        "Color",
        juce::NormalisableRange<float>(-8.0f, 0.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    // 0-200ms matches the real RMX16 Ambience program's own pre-delay range (per Adam, 2026-09-02).
    // AuraFDNEngine already allocates 200ms of pre-delay buffer headroom, so this needed no engine
    // change - the parameter was just capped lower than the hardware for no good reason. Default
    // 32ms matches the "Default" factory preset (see timeSecondsParamID's comment).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{preDelayMsParamID, 1},
        "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        32.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

    // "Converter" (Adam, 2026-09-03): a discrete 3-position bit-depth selector - a button that
    // cycles through 8/16/24 bit, with an LED list on the panel showing which is active (matches
    // flux-phaser's own Stages Shift-button/LED-list pattern) - replacing the original continuous
    // 8-24 knob. Underlying param ID stays bitDepth (unchanged since the original knob) -
    // AuraFDNEngine::setBitDepth still takes a plain bit count; getBitDepthForChoiceIndex() below
    // maps this choice's index to that value, same split as flux-phaser's stagesParam/
    // getStageCountForChoiceIndex(). 24 bit is still a GENUINE bypass (AuraFDNEngine::
    // bitDepthActive) - unchanged from the original knob's contract.
    //
    // Default index 2 (24 bit, Adam, 2026-09-03) matches the "Default" factory preset (see
    // timeSecondsParamID's comment) - supersedes this control's earlier 8-bit default (chosen the
    // same day, before the preset-matching request) and, before that, the original continuous
    // knob's 16-bit "matches the real hardware's measured dynamic range" default. The choice of
    // WHICH bit depth sits at the default is independent of the mechanism and could change again
    // without touching the DSP.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{bitDepthParamID, 1},
        "Converter",
        getConverterChoices(),
        2));

    // Independent Dry/Wet level pair (Adam, 2026-09-02), replacing the old single Blend + Volume
    // pair - same convention as caverns-delay's own dryParamID/wetParamID (percent, not dB;
    // multiplicative gain, not a crossfade). Wet goes past 100% (unity) up to 200% so the reverb
    // can be pushed louder than the dry tap, matching Caverns' own headroom rationale. Dry/Wet
    // default to 0%/100% (pure wet), matching the "Default" factory preset (see
    // timeSecondsParamID's comment) - supersedes the original 100%/50% (mostly-dry) default.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{dryParamID, 1},
        "Dry",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{wetParamID, 1},
        "Wet",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

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
    engine.setBitDepth(getBitDepthForChoiceIndex((int) bitDepthParam->load()));
    engine.setLowCutHz(lowCutHzParam->load());
    engine.setSubBassGain(AuraParameterMap::mapTimeToSubBassGain(timeSecondsParam->load()));

    // Independent Dry/Wet level pair, not a crossfade - see createParameterLayout()'s comment.
    // Percent-to-linear, matching caverns-delay's own dryGain/wetGain convention exactly.
    const auto dryGain = dryParam->load() * 0.01f;
    const auto wetGain = wetParam->load() * 0.01f;

    const auto numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    juce::HeapBlock<float> dryL(static_cast<size_t>(numSamples));
    juce::HeapBlock<float> dryR(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
    {
        dryL[n] = left[n];
        dryR[n] = right[n];
    }

    engine.processStereo(left, right, numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        const auto wetL = left[n] * wetGain;
        const auto wetR = right[n] * wetGain;
        left[n] = dryL[n] * dryGain + wetL;
        right[n] = dryR[n] * dryGain + wetR;
    }
}

// Defined in PluginEditor.cpp (not here) so this file has no GUI dependency.

bool AuraAudioProcessor::hasEditor() const { return true; }

const juce::String AuraAudioProcessor::getName() const { return JucePlugin_Name; }

bool AuraAudioProcessor::acceptsMidi() const { return false; }
bool AuraAudioProcessor::producesMidi() const { return false; }
bool AuraAudioProcessor::isMidiEffect() const { return false; }
double AuraAudioProcessor::getTailLengthSeconds() const { return 10.0; }

int AuraAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int AuraAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void AuraAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String AuraAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }
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
