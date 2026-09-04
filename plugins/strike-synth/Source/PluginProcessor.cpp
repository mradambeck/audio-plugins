#include "PluginProcessor.h"

namespace
{
    // Ramp time for the two live-smoothed parameters (damping, output level) - short enough to
    // feel immediate, long enough to eliminate zipper noise/clicks when moved during playback.
    constexpr double smoothingRampSeconds = 0.02;

    // Master pre-gain, applied alongside the adaptive headroom below, at the user's explicit
    // request: the default patch (a single plucked note, every control at its own default)
    // measured its attack peak at -15.3dBFS even after the adaptive-headroom fix (see
    // headroomSmoothed's own comment) closed the Mono/Poly mismatch that accounted for most of
    // the original -24.3dBFS gap - the user wanted the default patch averaging closer to -10dBFS.
    // +5.3dB (measured, not guessed) closes the remaining gap exactly. Re-verified against every
    // existing worst-case "stays bounded" test in this suite before landing on this value - none
    // of them needed their own bound raised, so this is real headroom the instrument already had,
    // not a value that trades away safety margin elsewhere.
    constexpr float masterPreGain = 1.8415f; // +5.3dB

    // Factory presets: raw parameter values (the same values setValueNotifyingHost() takes
    // after normalising, not display percentages) applied in one shot when the preset is
    // selected. Captured from seven .aupreset files the user saved via a host's native preset UI
    // (decoded from each preset's embedded jucePluginState, not hand-tuned).
    const std::vector<wildjag::FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<wildjag::FactoryPreset> presets = {
            { "A Little Mad Sometimes", {
                { StrikeAudioProcessor::dampingParamID, 0.0f },
                { StrikeAudioProcessor::outputLevelParamID, 0.7999986410140991f },
                { StrikeAudioProcessor::brightnessParamID, 0.8020000457763672f },
                { StrikeAudioProcessor::bowAmountParamID, 0.0f },
                { StrikeAudioProcessor::bowForceParamID, 0.0f },
                { StrikeAudioProcessor::noiseColorParamID, 1.0f },
                { StrikeAudioProcessor::structureParamID, 0.718000054359436f },
                { StrikeAudioProcessor::positionParamID, 0.1610000133514404f },
                { StrikeAudioProcessor::monoParamID, 0.0f },
                { StrikeAudioProcessor::waveshapeParamID, 0.0f },
                { StrikeAudioProcessor::waveshaperTypeParamID, 1.0f },
                { StrikeAudioProcessor::distortionPositionParamID, 0.0f },
                { StrikeAudioProcessor::ringModAmountParamID, 0.0f },
                { StrikeAudioProcessor::ringModFrequencyParamID, 20.0f },
                { StrikeAudioProcessor::topologyParamID, 1.0f },
                { StrikeAudioProcessor::crossCoupleParamID, 1.0f },
                { StrikeAudioProcessor::coupleDelayParamID, 10.0f },
                { StrikeAudioProcessor::detuneParamID, 0.1280000060796738f },
                { StrikeAudioProcessor::loopFilterTypeParamID, 0.0f },
                { StrikeAudioProcessor::resonanceParamID, 0.0f },
                { StrikeAudioProcessor::filterCutoffParamID, 20.0f },
                { StrikeAudioProcessor::filterEnvAmountParamID, -1.0f },
                { StrikeAudioProcessor::filterAttackParamID, 1.0f },
                { StrikeAudioProcessor::filterDecayParamID, 5.0f },
            } },
            { "Drop da Bass", {
                { StrikeAudioProcessor::dampingParamID, 0.06599999964237213f },
                { StrikeAudioProcessor::outputLevelParamID, 0.5399986505508423f },
                { StrikeAudioProcessor::brightnessParamID, 0.3490000069141388f },
                { StrikeAudioProcessor::bowAmountParamID, 0.0f },
                { StrikeAudioProcessor::bowForceParamID, 0.0f },
                { StrikeAudioProcessor::noiseColorParamID, 2.0f },
                { StrikeAudioProcessor::structureParamID, 0.4510000348091125f },
                { StrikeAudioProcessor::positionParamID, 0.3710000216960907f },
                { StrikeAudioProcessor::monoParamID, 0.0f },
                { StrikeAudioProcessor::waveshapeParamID, 0.4260000288486481f },
                { StrikeAudioProcessor::waveshaperTypeParamID, 1.0f },
                { StrikeAudioProcessor::distortionPositionParamID, 0.0f },
                { StrikeAudioProcessor::ringModAmountParamID, 0.0f },
                { StrikeAudioProcessor::ringModFrequencyParamID, 200.0f },
                { StrikeAudioProcessor::topologyParamID, 1.0f },
                { StrikeAudioProcessor::crossCoupleParamID, 1.0f },
                { StrikeAudioProcessor::coupleDelayParamID, 10.0f },
                { StrikeAudioProcessor::detuneParamID, 0.5480000376701355f },
                { StrikeAudioProcessor::loopFilterTypeParamID, 1.0f },
                { StrikeAudioProcessor::resonanceParamID, 0.101000003516674f },
                { StrikeAudioProcessor::filterCutoffParamID, 7151.126953125f },
                { StrikeAudioProcessor::filterEnvAmountParamID, -0.6309999823570251f },
                { StrikeAudioProcessor::filterAttackParamID, 107.0f },
                { StrikeAudioProcessor::filterDecayParamID, 1427.0f },
            } },
            { "Game Over", {
                { StrikeAudioProcessor::dampingParamID, 0.4290000200271606f },
                { StrikeAudioProcessor::outputLevelParamID, -2.050001382827759f },
                { StrikeAudioProcessor::brightnessParamID, 0.8170000314712524f },
                { StrikeAudioProcessor::bowAmountParamID, 0.05100000277161598f },
                { StrikeAudioProcessor::bowForceParamID, 0.2160000056028366f },
                { StrikeAudioProcessor::noiseColorParamID, 0.0f },
                { StrikeAudioProcessor::structureParamID, 0.3380000293254852f },
                { StrikeAudioProcessor::positionParamID, 0.3040000200271606f },
                { StrikeAudioProcessor::monoParamID, 0.0f },
                { StrikeAudioProcessor::waveshapeParamID, 0.193000003695488f },
                { StrikeAudioProcessor::waveshaperTypeParamID, 1.0f },
                { StrikeAudioProcessor::distortionPositionParamID, 0.0f },
                { StrikeAudioProcessor::ringModAmountParamID, 0.0f },
                { StrikeAudioProcessor::ringModFrequencyParamID, 20.0f },
                { StrikeAudioProcessor::topologyParamID, 1.0f },
                { StrikeAudioProcessor::crossCoupleParamID, 0.0f },
                { StrikeAudioProcessor::coupleDelayParamID, 0.0f },
                { StrikeAudioProcessor::detuneParamID, 0.445000022649765f },
                { StrikeAudioProcessor::loopFilterTypeParamID, 0.0f },
                { StrikeAudioProcessor::resonanceParamID, 0.0f },
                { StrikeAudioProcessor::filterCutoffParamID, 14870.7099609375f },
                { StrikeAudioProcessor::filterEnvAmountParamID, -0.621999979019165f },
                { StrikeAudioProcessor::filterAttackParamID, 27.0f },
                { StrikeAudioProcessor::filterDecayParamID, 1835.0f },
            } },
            { "Lost in Time", {
                { StrikeAudioProcessor::dampingParamID, 1.0f },
                { StrikeAudioProcessor::outputLevelParamID, -10.34000110626221f },
                { StrikeAudioProcessor::brightnessParamID, 0.2790000140666962f },
                { StrikeAudioProcessor::bowAmountParamID, 0.0f },
                { StrikeAudioProcessor::bowForceParamID, 0.0f },
                { StrikeAudioProcessor::noiseColorParamID, 0.0f },
                { StrikeAudioProcessor::structureParamID, 0.1280000060796738f },
                { StrikeAudioProcessor::positionParamID, 0.3660000264644623f },
                { StrikeAudioProcessor::monoParamID, 0.0f },
                { StrikeAudioProcessor::waveshapeParamID, 0.05100000277161598f },
                { StrikeAudioProcessor::waveshaperTypeParamID, 0.0f },
                { StrikeAudioProcessor::distortionPositionParamID, 0.0f },
                { StrikeAudioProcessor::ringModAmountParamID, 0.1210000067949295f },
                { StrikeAudioProcessor::ringModFrequencyParamID, 440.0000305175781f },
                { StrikeAudioProcessor::topologyParamID, 0.0f },
                { StrikeAudioProcessor::crossCoupleParamID, 0.0f },
                { StrikeAudioProcessor::coupleDelayParamID, 0.0f },
                { StrikeAudioProcessor::detuneParamID, 0.445000022649765f },
                { StrikeAudioProcessor::loopFilterTypeParamID, 0.0f },
                { StrikeAudioProcessor::resonanceParamID, 0.0f },
                { StrikeAudioProcessor::filterCutoffParamID, 14870.7099609375f },
                { StrikeAudioProcessor::filterEnvAmountParamID, -0.621999979019165f },
                { StrikeAudioProcessor::filterAttackParamID, 27.0f },
                { StrikeAudioProcessor::filterDecayParamID, 1835.0f },
            } },
            { "Make it Work", {
                { StrikeAudioProcessor::dampingParamID, 0.7120000123977661f },
                { StrikeAudioProcessor::outputLevelParamID, -12.34000110626221f },
                { StrikeAudioProcessor::brightnessParamID, 1.0f },
                { StrikeAudioProcessor::bowAmountParamID, 0.01800000108778477f },
                { StrikeAudioProcessor::bowForceParamID, 0.6670000553131104f },
                { StrikeAudioProcessor::noiseColorParamID, 1.0f },
                { StrikeAudioProcessor::structureParamID, 1.0f },
                { StrikeAudioProcessor::positionParamID, 0.136000007390976f },
                { StrikeAudioProcessor::monoParamID, 1.0f },
                { StrikeAudioProcessor::waveshapeParamID, 0.0f },
                { StrikeAudioProcessor::waveshaperTypeParamID, 0.0f },
                { StrikeAudioProcessor::distortionPositionParamID, 0.0f },
                { StrikeAudioProcessor::ringModAmountParamID, 0.0f },
                { StrikeAudioProcessor::ringModFrequencyParamID, 200.0f },
                { StrikeAudioProcessor::topologyParamID, 1.0f },
                { StrikeAudioProcessor::crossCoupleParamID, 0.0f },
                { StrikeAudioProcessor::coupleDelayParamID, 0.0f },
                { StrikeAudioProcessor::detuneParamID, 0.5480000376701355f },
                { StrikeAudioProcessor::loopFilterTypeParamID, 1.0f },
                { StrikeAudioProcessor::resonanceParamID, 0.0f },
                { StrikeAudioProcessor::filterCutoffParamID, 7151.126953125f },
                { StrikeAudioProcessor::filterEnvAmountParamID, -0.6639999747276306f },
                { StrikeAudioProcessor::filterAttackParamID, 151.0f },
                { StrikeAudioProcessor::filterDecayParamID, 1427.0f },
            } },
            { "Morphin Time", {
                { StrikeAudioProcessor::dampingParamID, 0.5670000314712524f },
                { StrikeAudioProcessor::outputLevelParamID, 1.129998683929443f },
                { StrikeAudioProcessor::brightnessParamID, 1.0f },
                { StrikeAudioProcessor::bowAmountParamID, 0.0f },
                { StrikeAudioProcessor::bowForceParamID, 0.0f },
                { StrikeAudioProcessor::noiseColorParamID, 0.0f },
                { StrikeAudioProcessor::structureParamID, 0.2310000061988831f },
                { StrikeAudioProcessor::positionParamID, 0.1140000075101852f },
                { StrikeAudioProcessor::monoParamID, 1.0f },
                { StrikeAudioProcessor::waveshapeParamID, 0.4740000367164612f },
                { StrikeAudioProcessor::waveshaperTypeParamID, 0.0f },
                { StrikeAudioProcessor::distortionPositionParamID, 0.0f },
                { StrikeAudioProcessor::ringModAmountParamID, 0.4760000109672546f },
                { StrikeAudioProcessor::ringModFrequencyParamID, 200.0f },
                { StrikeAudioProcessor::topologyParamID, 0.0f },
                { StrikeAudioProcessor::crossCoupleParamID, 0.0f },
                { StrikeAudioProcessor::coupleDelayParamID, 0.0f },
                { StrikeAudioProcessor::detuneParamID, 0.2850000262260437f },
                { StrikeAudioProcessor::loopFilterTypeParamID, 1.0f },
                { StrikeAudioProcessor::resonanceParamID, 0.1610000133514404f },
                { StrikeAudioProcessor::filterCutoffParamID, 1133.210083007812f },
                { StrikeAudioProcessor::filterEnvAmountParamID, 0.03100004978477955f },
                { StrikeAudioProcessor::filterAttackParamID, 98.0f },
                { StrikeAudioProcessor::filterDecayParamID, 1256.0f },
            } },
            { "The Spice Must Flow", {
                { StrikeAudioProcessor::dampingParamID, 0.4830000102519989f },
                { StrikeAudioProcessor::outputLevelParamID, 0.5799986720085144f },
                { StrikeAudioProcessor::brightnessParamID, 0.8170000314712524f },
                { StrikeAudioProcessor::bowAmountParamID, 0.08400000631809235f },
                { StrikeAudioProcessor::bowForceParamID, 0.7800000309944153f },
                { StrikeAudioProcessor::noiseColorParamID, 1.0f },
                { StrikeAudioProcessor::structureParamID, 1.0f },
                { StrikeAudioProcessor::positionParamID, 0.07400000095367432f },
                { StrikeAudioProcessor::monoParamID, 0.0f },
                { StrikeAudioProcessor::waveshapeParamID, 0.0f },
                { StrikeAudioProcessor::waveshaperTypeParamID, 0.0f },
                { StrikeAudioProcessor::distortionPositionParamID, 0.0f },
                { StrikeAudioProcessor::ringModAmountParamID, 0.1280000060796738f },
                { StrikeAudioProcessor::ringModFrequencyParamID, 3203.013671875f },
                { StrikeAudioProcessor::topologyParamID, 1.0f },
                { StrikeAudioProcessor::crossCoupleParamID, 0.0f },
                { StrikeAudioProcessor::coupleDelayParamID, 0.0f },
                { StrikeAudioProcessor::detuneParamID, 0.445000022649765f },
                { StrikeAudioProcessor::loopFilterTypeParamID, 0.0f },
                { StrikeAudioProcessor::resonanceParamID, 0.0f },
                { StrikeAudioProcessor::filterCutoffParamID, 14870.7099609375f },
                { StrikeAudioProcessor::filterEnvAmountParamID, -0.621999979019165f },
                { StrikeAudioProcessor::filterAttackParamID, 27.0f },
                { StrikeAudioProcessor::filterDecayParamID, 1835.0f },
            } },
        };
        return presets;
    }

}

StrikeAudioProcessor::StrikeAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets())
{
    dampingParam = apvts.getRawParameterValue(dampingParamID);
    outputLevelParam = apvts.getRawParameterValue(outputLevelParamID);
    brightnessParam = apvts.getRawParameterValue(brightnessParamID);
    bowAmountParam = apvts.getRawParameterValue(bowAmountParamID);
    bowForceParam = apvts.getRawParameterValue(bowForceParamID);
    noiseColorParam = apvts.getRawParameterValue(noiseColorParamID);
    structureParam = apvts.getRawParameterValue(structureParamID);
    positionParam = apvts.getRawParameterValue(positionParamID);
    monoParam = apvts.getRawParameterValue(monoParamID);
    waveshapeParam = apvts.getRawParameterValue(waveshapeParamID);
    waveshaperTypeParam = apvts.getRawParameterValue(waveshaperTypeParamID);
    distortionPositionParam = apvts.getRawParameterValue(distortionPositionParamID);
    ringModAmountParam = apvts.getRawParameterValue(ringModAmountParamID);
    ringModFrequencyParam = apvts.getRawParameterValue(ringModFrequencyParamID);
    topologyParam = apvts.getRawParameterValue(topologyParamID);
    crossCoupleParam = apvts.getRawParameterValue(crossCoupleParamID);
    coupleDelayParam = apvts.getRawParameterValue(coupleDelayParamID);
    detuneParam = apvts.getRawParameterValue(detuneParamID);
    loopFilterTypeParam = apvts.getRawParameterValue(loopFilterTypeParamID);
    resonanceParam = apvts.getRawParameterValue(resonanceParamID);
    filterCutoffParam = apvts.getRawParameterValue(filterCutoffParamID);
    filterEnvAmountParam = apvts.getRawParameterValue(filterEnvAmountParamID);
    filterAttackParam = apvts.getRawParameterValue(filterAttackParamID);
    filterDecayParam = apvts.getRawParameterValue(filterDecayParamID);
}

StrikeAudioProcessor::~StrikeAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout StrikeAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{dampingParamID, 1},
        "Decay",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.6f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{outputLevelParamID, 1},
        "Output Level",
        juce::NormalisableRange<float>(-60.0f, 6.0f, 0.01f),
        -6.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(
            [](float value, int) { return juce::String(value, 1) + " dB"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{brightnessParamID, 1},
        "Pluck Brightness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Range compressed from the full 0-100% to 0-30% - the user found only that first third of the
    // knob's travel sounded good, so the knob's full physical turn now covers exactly that range
    // (100% right = the old 30%) instead of cramming the useful part into the first third.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{bowAmountParamID, 1},
        "Pluck / Bow",
        juce::NormalisableRange<float>(0.0f, 0.3f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Pluck-side noise generator's own spectral color, a runtime dropdown like Waveshaper Type/
    // Loop Filter Type (see StrikeExcitation::setNoiseColor()'s own comment) - defaults to Cold
    // (index 0, the flat-white noise source), preserving every existing preset/test's behavior
    // exactly. Labeled Cold/Warm/Dark rather than White/Pink/Brown in the UI (the user's own
    // choice of names for the same three underlying colors) - the internal DSP naming (White/Pink/
    // Brown noise) is unchanged, only the on-screen labels differ.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{noiseColorParamID, 1},
        "Noise Color",
        juce::StringArray{"Cold", "Warm", "Dark"},
        0));

    // Bow-side only (no effect at Pluck/Bow=0%) - the friction bow model's own "Bow Pressure"
    // control (see StrikeExcitation::setBowForce()'s own comment). Defaults to 50%, STK's own
    // literal default (frictionSlope=3.0, the midpoint of its 1.0-5.0 span) - a real,
    // literature-anchored default, not invented.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{bowForceParamID, 1},
        "Bow Force",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Defaults to 0% - a bit-exact passthrough (no dispersion/inharmonicity applied), matching
    // this project's established convention for non-breaking parameter defaults.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{structureParamID, 1},
        "Structure",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // StrikeVoice.h's clampedPosition formula (0.5 - 0.98*|position-0.5|) is symmetric around
    // position=0.5, so the original 0-100% range gave two mirrored halves that sounded identical
    // (0%->50% traced the same clampedPosition sweep as 100%->50%, just in reverse) - the user
    // asked to halve the control to remove that redundant duplicate half. Range compressed to
    // 0-50%: full left (0%) is the old position=0 (minimal tap effect), full right (50%) is the
    // old position=0.5 (maximal tap effect, still the default), covering every unique sound
    // exactly once across the knob's whole travel.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{positionParamID, 1},
        "Position",
        juce::NormalisableRange<float>(0.0f, 0.5f, 0.001f),
        0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Defaults to off (polyphonic) - preserves existing behavior for anyone who saved a preset
    // before this control existed, matching this project's convention of non-breaking defaults.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{monoParamID, 1}, "Mono", false));

    // The Waveshaper seam (see StrikeWaveshaper.h) - defaults to 0% (bit-exact no-op, the
    // Waveshaper is never even called at this value - see StrikeStringLineChannel::
    // renderChannelSample()), matching every other new-control convention in this project.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{waveshapeParamID, 1},
        "Waveshape",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Runtime choice between the Waveshaper seam's two concrete implementations (see
    // StrikeWaveshaper.h's own comment for why this one seam is a runtime dropdown rather than
    // a compile-time template parameter like the other three) - defaults to Fold (index 0),
    // matching every build/listening session so far. Fuzz and Saturate were removed at the user's
    // request.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{waveshaperTypeParamID, 1},
        "Waveshaper Type",
        juce::StringArray{"Fold", "BitCrush"},
        0));

    // Pre/Post Filter - at the user's explicit request, so the Waveshaper (Fold or BitCrush) can
    // sit before or after the Resonant loop filter's own coloring in the output signal path - see
    // StrikeVoice.h's own setDistortionPosition() comment for the exact scope (output-only
    // reorder; Fold's in-loop recirculating character is unaffected either way). Defaults to Pre
    // Filter (index 0), matching this project's pre-existing behavior bit-for-bit.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{distortionPositionParamID, 1},
        "Distortion Position",
        juce::StringArray{"Pre Filter", "Post Filter"},
        0));

    // Ring Modulator (see StrikeRingModulator.h) - its own area, not a Waveshaper Type, since it
    // needs its own Frequency control and runs alongside whichever Waveshaper Type is selected
    // (applied in-loop, after the Waveshaper - see StrikeStringLineChannel::renderChannelSample()).
    // Amount defaults to 0% (bit-exact no-op, the oscillator isn't even advanced at this value),
    // matching every other new-control convention in this project.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ringModAmountParamID, 1},
        "Ring Mod",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // 20Hz-5kHz, the classic ring-mod pedal range - skewed so most of the knob's travel sits in
    // the lower/mid range where the effect reads as pitched sidebands rather than pure noise-like
    // aliasing. Default (200Hz) picked as an audibly obvious starting point, not derived - to be
    // confirmed by listening, same convention as every other constant in this feature.
    juce::NormalisableRange<float> ringModFrequencyRange(20.0f, 5000.0f);
    ringModFrequencyRange.setSkewForCentre(500.0f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ringModFrequencyParamID, 1},
        "Ring Mod Freq",
        ringModFrequencyRange,
        200.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value)) + " Hz"; })));

    // The Feedback Topology seam (see StrikeVoice.h) - a runtime dropdown like Waveshaper Type,
    // at the user's explicit request, so Single and Dual can be A/B'd live. Defaults to Single
    // (index 0) - preserves every existing preset/test's behavior exactly.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{topologyParamID, 1},
        "Topology",
        juce::StringArray{"Single", "Dual"},
        0));

    // Dual-topology only (has no effect at Topology=Single, since StrikeVoice::renderNextSample()
    // never reads it in that branch) - how much of each line's write-back value comes from the
    // OTHER line, live/every-sample. Provably safe across the entire 0-100% range with no ceiling
    // needed - see StrikeVoice.h's own two-part safety argument (per-sample boundedness via
    // convex combination, plus a closed-form steady-state loop-gain analysis) - unlike every
    // Waveshaper curve's maxDrive, this isn't a "how far can we safely push this" tuning question.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{crossCoupleParamID, 1},
        "Cross-Couple",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Dual-topology only - a short delay inserted into the cross-coupling path itself (each
    // direction independently), live/every-sample like Cross-Couple. 0ms is a bit-exact match for
    // the original (undelayed, same-instant) coupling formula. Turns the coupling from a flat,
    // broadband effect into a harmonic-dependent one - see StrikeVoice.h's own comment for why
    // this needs no new safety ceiling either (a pure delay only rotates phase, never adds gain).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{coupleDelayParamID, 1},
        "Couple Delay",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(
            [](float value, int) { return juce::String(value, 1) + " ms"; })));

    // Dual-topology only - latched at noteOn (not live), same convention as Brightness, since real
    // unison detuning isn't a performance gesture the way Cross-Couple sweeping might be. 0% = both
    // lines at the identical target pitch (the primary, physically-grounded design - real coupled
    // piano unisons are nominally same-pitch strings, see StrikeVoice.h's own header comment);
    // 100% offsets line B by StrikeVoice::maxDetuneSemitones (~50 cents, raised from an initial
    // ~20 cent starting point once the user heard it and wanted more range).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{detuneParamID, 1},
        "Detune",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // The Loop Filter seam (see StrikeLoopFilter.h) - a runtime dropdown like Waveshaper Type,
    // at the user's explicit request, so Two-Point Average and Resonant can be A/B'd live.
    // Defaults to Two-Point Average (index 0) - preserves every existing preset/test's behavior
    // exactly.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{loopFilterTypeParamID, 1},
        "Loop Filter Type",
        juce::StringArray{"Two-Point Average", "Resonant"},
        0));

    // Resonant-loop-filter-only (no effect at Loop Filter Type=Two-Point Average) - live/every-
    // sample. A traditional subtractive-synth lowpass's own Resonance (Q) control - see
    // StrikeLoopFilter.h's own comment for why this needs no safety ceiling (output-only, never
    // recirculated into the string).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{resonanceParamID, 1},
        "Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // 20Hz-18kHz, skewed toward a musically central range - deliberately spans close to the full
    // audible range (not the old bandpass design's narrower 80Hz-8kHz "Formant Freq"), matching a
    // traditional synth filter's own cutoff span. Default (8000Hz) is deliberately bright/near-open
    // - see StrikeLoopFilter.h's own comment for why. A reasoned starting range, not measured
    // against Strike's own loop, to be confirmed by listening - the safety proof is completely
    // independent of this value (see StrikeLoopFilter.h), so this is a purely musical choice, an
    // absolute Hz value, not tracking the note's own pitch.
    juce::NormalisableRange<float> filterCutoffRange(20.0f, 18000.0f);
    filterCutoffRange.setSkewForCentre(1500.0f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{filterCutoffParamID, 1},
        "Filter Cutoff",
        filterCutoffRange,
        8000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction(
            [](float value, int)
            {
                return value >= 1000.0f ? juce::String(value / 1000.0f, 1) + " kHz"
                                         : juce::String(juce::roundToInt(value)) + " Hz";
            })));

    // Bipolar - see StrikeLoopFilter.h's own comment for the sweep direction convention. Defaults
    // to 0% (bit-exact no-op: Cutoff stays fixed regardless of the still-running Attack/Decay
    // envelope underneath it), matching every other new-control convention in this project.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{filterEnvAmountParamID, 1},
        "Filter Envelope",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Filter envelope's own Attack/Decay times - has no audible effect at Filter Envelope=0 (the
    // envelope itself still runs every note, but contributes zero octaves of sweep - see
    // StrikeLoopFilter.h's own comment). Defaults (1ms attack, 200ms decay) give an immediate-
    // opening, moderately-paced closing sweep once Filter Envelope is turned up - a reasoned
    // starting point, not measured, pending listening.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{filterAttackParamID, 1},
        "Filter Attack",
        juce::NormalisableRange<float>(1.0f, 1000.0f, 1.0f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value)) + " ms"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{filterDecayParamID, 1},
        "Filter Decay",
        juce::NormalisableRange<float>(5.0f, 5000.0f, 1.0f),
        200.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value)) + " ms"; })));

    return { params.begin(), params.end() };
}

void StrikeAudioProcessor::prepareToPlay(double sampleRate, int)
{
    for (auto& v : voices)
        v.prepare(sampleRate);
    voiceAllocator.reset();
    monoNoteStack.reset();
    previousMonoMode = monoParam->load() >= 0.5f;
    previousTopology = (int) topologyParam->load();

    // Starts at 1.0 (as if a single voice were already active) - matches the real state right
    // after prepareToPlay() (no voices active, so the very first note struck should ramp toward
    // headroomGain=1.0 anyway, not up from some stale/arbitrary starting point).
    headroomSmoothed.reset(sampleRate, smoothingRampSeconds);
    headroomSmoothed.setCurrentAndTargetValue(1.0f);

    dampingSmoothed.reset(sampleRate, smoothingRampSeconds);
    dampingSmoothed.setCurrentAndTargetValue(dampingParam->load());

    outputLevelSmoothed.reset(sampleRate, smoothingRampSeconds);
    outputLevelSmoothed.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(outputLevelParam->load()));

    bowAmountSmoothed.reset(sampleRate, smoothingRampSeconds);
    bowAmountSmoothed.setCurrentAndTargetValue(bowAmountParam->load());

    bowForceSmoothed.reset(sampleRate, smoothingRampSeconds);
    bowForceSmoothed.setCurrentAndTargetValue(bowForceParam->load());

    structureSmoothed.reset(sampleRate, smoothingRampSeconds);
    structureSmoothed.setCurrentAndTargetValue(structureParam->load());

    positionSmoothed.reset(sampleRate, smoothingRampSeconds);
    positionSmoothed.setCurrentAndTargetValue(positionParam->load());

    waveshapeSmoothed.reset(sampleRate, smoothingRampSeconds);
    waveshapeSmoothed.setCurrentAndTargetValue(waveshapeParam->load());

    ringModAmountSmoothed.reset(sampleRate, smoothingRampSeconds);
    ringModAmountSmoothed.setCurrentAndTargetValue(ringModAmountParam->load());

    ringModFrequencySmoothed.reset(sampleRate, smoothingRampSeconds);
    ringModFrequencySmoothed.setCurrentAndTargetValue(ringModFrequencyParam->load());

    crossCoupleSmoothed.reset(sampleRate, smoothingRampSeconds);
    crossCoupleSmoothed.setCurrentAndTargetValue(crossCoupleParam->load());

    coupleDelaySmoothed.reset(sampleRate, smoothingRampSeconds);
    coupleDelaySmoothed.setCurrentAndTargetValue(coupleDelayParam->load());

    resonanceSmoothed.reset(sampleRate, smoothingRampSeconds);
    resonanceSmoothed.setCurrentAndTargetValue(resonanceParam->load());

    filterCutoffSmoothed.reset(sampleRate, smoothingRampSeconds);
    filterCutoffSmoothed.setCurrentAndTargetValue(filterCutoffParam->load());

    filterEnvAmountSmoothed.reset(sampleRate, smoothingRampSeconds);
    filterEnvAmountSmoothed.setCurrentAndTargetValue(filterEnvAmountParam->load());

    filterAttackSmoothed.reset(sampleRate, smoothingRampSeconds);
    filterAttackSmoothed.setCurrentAndTargetValue(filterAttackParam->load() * 0.001f);

    filterDecaySmoothed.reset(sampleRate, smoothingRampSeconds);
    filterDecaySmoothed.setCurrentAndTargetValue(filterDecayParam->load() * 0.001f);
}

void StrikeAudioProcessor::releaseResources() {}

bool StrikeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void StrikeAudioProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
    {
        const auto note = message.getNoteNumber();
        const auto velocity = message.getFloatVelocity();

        if (previousMonoMode)
        {
            // Mono always retriggers with whatever note the stack says should now sound - on a
            // plain note-on that's just this note itself (see StrikeMonoNoteStack::noteOn()).
            const auto event = monoNoteStack.noteOn(note, velocity);
            voices[0].setBrightness(brightnessParam->load());
            voices[0].setDetuneAmount(detuneParam->load());
            voices[0].noteOn(event.note, event.velocity01);
        }
        else
        {
            std::array<bool, numVoices> isActive{};
            for (int i = 0; i < numVoices; ++i)
                isActive[(size_t) i] = voices[(size_t) i].isActive();

            const auto voiceIndex = voiceAllocator.allocateVoiceForNoteOn(note, isActive);

            voices[(size_t) voiceIndex].setBrightness(brightnessParam->load());
            voices[(size_t) voiceIndex].setDetuneAmount(detuneParam->load());
            voices[(size_t) voiceIndex].noteOn(note, velocity);
        }
    }
    else if (message.isNoteOff())
    {
        if (previousMonoMode)
        {
            // The core last-note-priority behavior (see StrikeMonoNoteStack.h's own comment):
            // releasing the currently-sounding note falls back to retriggering whichever
            // still-held note is now on top, rather than just letting it ring out - only release
            // the voice once nothing at all remains held. Releasing a note that ISN'T the one
            // currently sounding (a lower held note, or a note not held at all) changes nothing -
            // see NoteOffResult::Action's own comment for the real retrigger bug this distinction
            // fixes.
            using Action = StrikeMonoNoteStack<16>::NoteOffResult::Action;
            const auto result = monoNoteStack.noteOff(message.getNoteNumber());
            if (result.action == Action::Retrigger)
            {
                voices[0].setBrightness(brightnessParam->load());
                voices[0].setDetuneAmount(detuneParam->load());
                voices[0].noteOn(result.event.note, result.event.velocity01);
            }
            else if (result.action == Action::Release)
            {
                voices[0].noteOff();
            }
        }
        else
        {
            const auto voiceIndex = voiceAllocator.findVoiceForNoteOff(message.getNoteNumber());
            if (voiceIndex >= 0)
                voices[(size_t) voiceIndex].noteOff();
        }
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        for (auto& v : voices)
            v.reset();
        voiceAllocator.reset();
        monoNoteStack.reset();
    }
}

void StrikeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    dampingSmoothed.setTargetValue(dampingParam->load());
    outputLevelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(outputLevelParam->load()));
    bowAmountSmoothed.setTargetValue(bowAmountParam->load());
    bowForceSmoothed.setTargetValue(bowForceParam->load());
    structureSmoothed.setTargetValue(structureParam->load());
    positionSmoothed.setTargetValue(positionParam->load());
    waveshapeSmoothed.setTargetValue(waveshapeParam->load());
    ringModAmountSmoothed.setTargetValue(ringModAmountParam->load());
    ringModFrequencySmoothed.setTargetValue(ringModFrequencyParam->load());
    crossCoupleSmoothed.setTargetValue(crossCoupleParam->load());
    coupleDelaySmoothed.setTargetValue(coupleDelayParam->load());
    resonanceSmoothed.setTargetValue(resonanceParam->load());
    filterCutoffSmoothed.setTargetValue(filterCutoffParam->load());
    filterEnvAmountSmoothed.setTargetValue(filterEnvAmountParam->load());
    filterAttackSmoothed.setTargetValue(filterAttackParam->load() * 0.001f);
    filterDecaySmoothed.setTargetValue(filterDecayParam->load() * 0.001f);

    // Poly/Mono is a discrete mode switch, not a live-sweepable control - deliberately not
    // smoothed, and checked once per block rather than every sample. Toggling it while notes are
    // held would otherwise leave voiceAllocator's tags or monoNoteStack's held notes stale and
    // inconsistent with whichever mechanism is now in charge (e.g. a note still tagged in the
    // allocator from Poly mode that Mono's note stack knows nothing about) - treating a mode
    // change as an implicit all-notes-off, the same way a real hardware Poly/Mono switch would,
    // sidesteps that entirely rather than trying to reconcile two different bookkeeping schemes.
    const auto mono = monoParam->load() >= 0.5f;
    if (mono != previousMonoMode)
    {
        for (auto& v : voices)
            v.reset();
        voiceAllocator.reset();
        monoNoteStack.reset();
        previousMonoMode = mono;
    }

    // Same treatment as the Mono/Poly switch above, for the same reason: Topology changes which
    // internal state each Voice's two lines hold (Single never renders lineB at all; Dual reads
    // and writes both), so a mid-note switch can't be reconciled - it's treated as an implicit
    // all-notes-off instead. Kept as its own separate check (not folded into the Mono check above)
    // since the two are fully orthogonal axes - Mono mode still just drives voices[0] exclusively,
    // which internally may run 1 or 2 lines regardless of Poly/Mono.
    const auto topology = (int) topologyParam->load();
    if (topology != previousTopology)
    {
        for (auto& v : voices)
            v.reset();
        voiceAllocator.reset();
        monoNoteStack.reset();
        previousTopology = topology;
    }

    // Waveshaper Type is a discrete choice like Mono, but - unlike Mono - has no cross-referencing
    // bookkeeping (voiceAllocator/monoNoteStack) that could go stale on a mid-note switch; both
    // concrete waveshapers are stateless, so reading this fresh every block and letting it change
    // mid-note is completely safe.
    const auto waveshaperType = (int) waveshaperTypeParam->load();

    // Distortion Position is the same kind of discrete choice as Waveshaper Type, for the same
    // reason - it only reorders two already-stateless output stages relative to each other, so a
    // mid-note switch is completely safe.
    const auto distortionPosition = (int) distortionPositionParam->load();

    // Loop Filter Type is the same kind of discrete choice as Waveshaper Type, for the same reason:
    // both concrete filters are always constructed/prepared/kept current (via setDamping()'s own
    // fan-out - see StrikeVoice.h), so a mid-note switch just leaves the UNSELECTED filter's own
    // history momentarily stale until reselected - no implicit all-notes-off needed, unlike Mono/
    // Topology, which have real cross-referencing bookkeeping that would otherwise go stale.
    const auto loopFilterType = (int) loopFilterTypeParam->load();

    // Noise Color is the same kind of discrete choice as Waveshaper Type/Loop Filter Type, for the
    // same reason: StrikeExcitation always keeps every color's own filter state around (cheap
    // scalar stores), so a mid-note switch just leaves the unselected colors' history momentarily
    // stale until reselected - no implicit all-notes-off needed.
    const auto noiseColor = (int) noiseColorParam->load();

    auto midiIterator = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        while (midiIterator != midiEnd && (*midiIterator).samplePosition == sample)
        {
            handleMidiMessage((*midiIterator).getMessage());
            ++midiIterator;
        }

        // Adaptive headroom: scaled by how many voices are ACTUALLY sounding this sample, not a
        // fixed worst-case-chord assumption - a real, measured bug fix (reported as "Mono is ~9dB
        // louder than Poly" and, independently, "the default patch sits ~9dB too quiet"): the old
        // scheme applied a flat 1/sqrt(8) reduction to every Poly note regardless of how many
        // voices were actually active, so a single Poly note was reserving headroom for a
        // hypothetical 8-note chord that wasn't playing - quieter than the same single note in
        // Mono (which skipped the reduction entirely) for no audible reason. Mono only ever drives
        // voices[0], so it naturally always measures activeCount<=1 here too - no special-casing
        // needed any more, unlike the old mono-vs-poly branch this replaces. Smoothed (not a bare
        // per-sample jump) so a chord being struck/released doesn't snap the gain audibly.
        int activeVoiceCount = 0;
        for (auto& v : voices)
            if (v.isActive())
                ++activeVoiceCount;
        headroomSmoothed.setTargetValue(1.0f / std::sqrt((float) std::max(1, activeVoiceCount)));
        const auto headroomGain = headroomSmoothed.getNextValue();

        const auto damping = dampingSmoothed.getNextValue();
        const auto bowAmount = bowAmountSmoothed.getNextValue();
        const auto bowForce = bowForceSmoothed.getNextValue();
        const auto structure = structureSmoothed.getNextValue();
        const auto position = positionSmoothed.getNextValue();
        const auto waveshape = waveshapeSmoothed.getNextValue();
        const auto ringModAmount = ringModAmountSmoothed.getNextValue();
        const auto ringModFrequency = ringModFrequencySmoothed.getNextValue();
        const auto crossCouple = crossCoupleSmoothed.getNextValue();
        const auto coupleDelay = coupleDelaySmoothed.getNextValue();
        const auto resonance = resonanceSmoothed.getNextValue();
        const auto filterCutoff = filterCutoffSmoothed.getNextValue();
        const auto filterEnvAmount = filterEnvAmountSmoothed.getNextValue();
        const auto filterAttack = filterAttackSmoothed.getNextValue();
        const auto filterDecay = filterDecaySmoothed.getNextValue();

        float mixedSample = 0.0f;
        for (auto& v : voices)
        {
            v.setDamping(damping);
            v.setBowAmount(bowAmount);
            v.setBowForce(bowForce);
            v.setNoiseColor(noiseColor);
            v.setStructure(structure);
            v.setPosition(position);
            v.setWaveshapeAmount(waveshape);
            v.setWaveshaperType(waveshaperType);
            v.setDistortionPosition(distortionPosition);
            v.setRingModAmount(ringModAmount);
            v.setRingModFrequency(ringModFrequency);
            v.setTopology(topology);
            v.setCoupleDelay(coupleDelay);
            v.setCrossCoupleAmount(crossCouple);
            v.setLoopFilterType(loopFilterType);
            v.setResonance(resonance);
            v.setFilterCutoff(filterCutoff);
            v.setFilterEnvAmount(filterEnvAmount);
            v.setFilterAttack(filterAttack);
            v.setFilterDecay(filterDecay);
            mixedSample += v.renderNextSample();
        }

        const auto out = mixedSample * headroomGain * masterPreGain * outputLevelSmoothed.getNextValue();
        for (int channel = 0; channel < numChannels; ++channel)
            buffer.setSample(channel, sample, out);
    }
}

bool StrikeAudioProcessor::hasEditor() const { return true; }

const juce::String StrikeAudioProcessor::getName() const { return JucePlugin_Name; }

bool StrikeAudioProcessor::acceptsMidi() const { return true; }
bool StrikeAudioProcessor::producesMidi() const { return false; }
bool StrikeAudioProcessor::isMidiEffect() const { return false; }
double StrikeAudioProcessor::getTailLengthSeconds() const { return 8.0; }

int StrikeAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int StrikeAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void StrikeAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String StrikeAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }

void StrikeAudioProcessor::changeProgramName(int, const juce::String&) {}

void StrikeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void StrikeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StrikeAudioProcessor();
}
