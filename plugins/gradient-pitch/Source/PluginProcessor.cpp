#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <iterator>

namespace
{
    // Note subdivisions offered for tempo sync, longest to shortest, each expressed as a multiple
    // of a quarter note's duration so they scale directly with host BPM - copied directly from
    // Caverns (the same table, same convention), since this is a generic musical construct, not
    // anything Gradient-specific.
    struct Subdivision
    {
        const char* label;
        float quarterNoteMultiple;
    };

    constexpr Subdivision subdivisions[] = {
        { "1/1", 4.0f },
        { "1/2.", 3.0f },
        { "1/2", 2.0f },
        { "1/2T", 4.0f / 3.0f },
        { "1/4.", 1.5f },
        { "1/4", 1.0f },
        { "1/4T", 2.0f / 3.0f },
        { "1/8.", 0.75f },
        { "1/8", 0.5f },
        { "1/8T", 1.0f / 3.0f },
        { "1/16.", 0.375f },
        { "1/16", 0.25f },
        { "1/16T", 1.0f / 6.0f },
        { "1/32", 0.125f },
    };

    constexpr int defaultSubdivisionIndex = 5; // "1/4"

    // Matches delayTimeMsA/B's own parameter range (0-1000ms) - the sync-derived time gets
    // clamped to this same range before being handed to the engine.
    constexpr float minDelayMs = 0.0f;
    constexpr float maxDelayMs = 1000.0f;

    // Factory presets: raw parameter values (the same values setValueNotifyingHost() takes after
    // normalising, not display percentages) applied in one shot when the preset is selected.
    const std::vector<wildjag::FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<wildjag::FactoryPreset> presets = {
            { "Extra Padding", {
                { GradientAudioProcessor::bypassParamID, 0.0f },
                { GradientAudioProcessor::crossFeedbackEnabledParamID, 0.0f },
                { GradientAudioProcessor::crossfadeLengthMsAParamID, 11.0f },
                { GradientAudioProcessor::crossfadeLengthMsBParamID, 10.80000019073486f },
                { GradientAudioProcessor::delaySubdivisionAParamID, 13.0f },
                { GradientAudioProcessor::delaySubdivisionBParamID, 5.0f },
                { GradientAudioProcessor::delaySyncEnabledAParamID, 0.0f },
                { GradientAudioProcessor::delaySyncEnabledBParamID, 0.0f },
                { GradientAudioProcessor::delayTimeMsAParamID, 8.199999809265137f },
                { GradientAudioProcessor::delayTimeMsBParamID, 0.800000011920929f },
                { GradientAudioProcessor::driftAmountAParamID, 48.5f },
                { GradientAudioProcessor::driftAmountBParamID, 90.30000305175781f },
                { GradientAudioProcessor::dualModeEnabledParamID, 1.0f },
                { GradientAudioProcessor::feedbackPercentAParamID, 33.20000076293945f },
                { GradientAudioProcessor::feedbackPercentBParamID, 61.60000228881836f },
                { GradientAudioProcessor::linkDelayIntervalMsParamID, 132.9000244140625f },
                { GradientAudioProcessor::linkEnabledParamID, 1.0f },
                { GradientAudioProcessor::linkPitchIntervalSemitonesParamID, 3.999999284744263f },
                { GradientAudioProcessor::mixPercentAParamID, 48.60000228881836f },
                { GradientAudioProcessor::mixPercentBParamID, 35.5f },
                { GradientAudioProcessor::outputTrimDbAParamID, 2.000000476837158f },
                { GradientAudioProcessor::outputTrimDbBParamID, 2.000000476837158f },
                { GradientAudioProcessor::pitchFineCentsAParamID, 7.450580596923828e-07f },
                { GradientAudioProcessor::pitchFineCentsBParamID, 50.0f },
                { GradientAudioProcessor::pitchSemitonesAParamID, -4.000000476837158f },
                { GradientAudioProcessor::pitchSemitonesBParamID, -5.364418029785156e-07f },
                { GradientAudioProcessor::spliceModeAParamID, 2.0f },
                { GradientAudioProcessor::spliceModeBParamID, 2.0f },
                { GradientAudioProcessor::widthPercentParamID, 66.0999984741211f },
            } },
            { "Get Away From Me", {
                { GradientAudioProcessor::bypassParamID, 0.0f },
                { GradientAudioProcessor::crossFeedbackEnabledParamID, 0.0f },
                { GradientAudioProcessor::crossfadeLengthMsAParamID, 11.80000019073486f },
                { GradientAudioProcessor::crossfadeLengthMsBParamID, 10.90000057220459f },
                { GradientAudioProcessor::delaySubdivisionAParamID, 13.0f },
                { GradientAudioProcessor::delaySubdivisionBParamID, 5.0f },
                { GradientAudioProcessor::delaySyncEnabledAParamID, 0.0f },
                { GradientAudioProcessor::delaySyncEnabledBParamID, 0.0f },
                { GradientAudioProcessor::delayTimeMsAParamID, 59.60000228881836f },
                { GradientAudioProcessor::delayTimeMsBParamID, 64.5999984741211f },
                { GradientAudioProcessor::driftAmountAParamID, 9.699999809265137f },
                { GradientAudioProcessor::driftAmountBParamID, 9.40000057220459f },
                { GradientAudioProcessor::dualModeEnabledParamID, 1.0f },
                { GradientAudioProcessor::feedbackPercentAParamID, 77.5f },
                { GradientAudioProcessor::feedbackPercentBParamID, 84.70000457763672f },
                { GradientAudioProcessor::linkDelayIntervalMsParamID, 1.490116119384766e-05f },
                { GradientAudioProcessor::linkEnabledParamID, 0.0f },
                { GradientAudioProcessor::linkPitchIntervalSemitonesParamID, -5.364418029785156e-07f },
                { GradientAudioProcessor::mixPercentAParamID, 50.70000076293945f },
                { GradientAudioProcessor::mixPercentBParamID, 50.0f },
                { GradientAudioProcessor::outputTrimDbAParamID, 3.576278686523438e-07f },
                { GradientAudioProcessor::outputTrimDbBParamID, -0.09999964386224747f },
                { GradientAudioProcessor::pitchFineCentsAParamID, 7.450580596923828e-07f },
                { GradientAudioProcessor::pitchFineCentsBParamID, 7.450580596923828e-07f },
                { GradientAudioProcessor::pitchSemitonesAParamID, -5.090000629425049f },
                { GradientAudioProcessor::pitchSemitonesBParamID, 7.089999198913574f },
                { GradientAudioProcessor::spliceModeAParamID, 2.0f },
                { GradientAudioProcessor::spliceModeBParamID, 2.0f },
                { GradientAudioProcessor::widthPercentParamID, 85.4000015258789f },
            } },
            { "Marked as Safe", {
                { GradientAudioProcessor::bypassParamID, 0.0f },
                { GradientAudioProcessor::crossFeedbackEnabledParamID, 0.0f },
                { GradientAudioProcessor::crossfadeLengthMsAParamID, 11.0f },
                { GradientAudioProcessor::crossfadeLengthMsBParamID, 10.80000019073486f },
                { GradientAudioProcessor::delaySubdivisionAParamID, 13.0f },
                { GradientAudioProcessor::delaySubdivisionBParamID, 5.0f },
                { GradientAudioProcessor::delaySyncEnabledAParamID, 0.0f },
                { GradientAudioProcessor::delaySyncEnabledBParamID, 0.0f },
                { GradientAudioProcessor::delayTimeMsAParamID, 0.9000000357627869f },
                { GradientAudioProcessor::delayTimeMsBParamID, 0.800000011920929f },
                { GradientAudioProcessor::driftAmountAParamID, 23.70000076293945f },
                { GradientAudioProcessor::driftAmountBParamID, 45.0f },
                { GradientAudioProcessor::dualModeEnabledParamID, 1.0f },
                { GradientAudioProcessor::feedbackPercentAParamID, 17.5f },
                { GradientAudioProcessor::feedbackPercentBParamID, 9.300000190734863f },
                { GradientAudioProcessor::linkDelayIntervalMsParamID, 1.490116119384766e-05f },
                { GradientAudioProcessor::linkEnabledParamID, 0.0f },
                { GradientAudioProcessor::linkPitchIntervalSemitonesParamID, -5.364418029785156e-07f },
                { GradientAudioProcessor::mixPercentAParamID, 40.79999923706055f },
                { GradientAudioProcessor::mixPercentBParamID, 38.90000152587891f },
                { GradientAudioProcessor::outputTrimDbAParamID, 2.000000476837158f },
                { GradientAudioProcessor::outputTrimDbBParamID, 1.700000405311584f },
                { GradientAudioProcessor::pitchFineCentsAParamID, -14.79999923706055f },
                { GradientAudioProcessor::pitchFineCentsBParamID, 15.90000057220459f },
                { GradientAudioProcessor::pitchSemitonesAParamID, -5.364418029785156e-07f },
                { GradientAudioProcessor::pitchSemitonesBParamID, -5.364418029785156e-07f },
                { GradientAudioProcessor::spliceModeAParamID, 2.0f },
                { GradientAudioProcessor::spliceModeBParamID, 2.0f },
                { GradientAudioProcessor::widthPercentParamID, 87.9000015258789f },
            } },
            { "Oh I get it", {
                { GradientAudioProcessor::bypassParamID, 0.0f },
                { GradientAudioProcessor::crossFeedbackEnabledParamID, 0.0f },
                { GradientAudioProcessor::crossfadeLengthMsAParamID, 11.0f },
                { GradientAudioProcessor::crossfadeLengthMsBParamID, 10.80000019073486f },
                { GradientAudioProcessor::delaySubdivisionAParamID, 13.0f },
                { GradientAudioProcessor::delaySubdivisionBParamID, 5.0f },
                { GradientAudioProcessor::delaySyncEnabledAParamID, 0.0f },
                { GradientAudioProcessor::delaySyncEnabledBParamID, 0.0f },
                { GradientAudioProcessor::delayTimeMsAParamID, 20.70000076293945f },
                { GradientAudioProcessor::delayTimeMsBParamID, 0.800000011920929f },
                { GradientAudioProcessor::driftAmountAParamID, 48.5f },
                { GradientAudioProcessor::driftAmountBParamID, 65.9000015258789f },
                { GradientAudioProcessor::dualModeEnabledParamID, 1.0f },
                { GradientAudioProcessor::feedbackPercentAParamID, 18.20000076293945f },
                { GradientAudioProcessor::feedbackPercentBParamID, 93.70000457763672f },
                { GradientAudioProcessor::linkDelayIntervalMsParamID, 363.6000061035156f },
                { GradientAudioProcessor::linkEnabledParamID, 1.0f },
                { GradientAudioProcessor::linkPitchIntervalSemitonesParamID, 0.2899994552135468f },
                { GradientAudioProcessor::mixPercentAParamID, 48.60000228881836f },
                { GradientAudioProcessor::mixPercentBParamID, 57.40000152587891f },
                { GradientAudioProcessor::outputTrimDbAParamID, 2.000000476837158f },
                { GradientAudioProcessor::outputTrimDbBParamID, 2.000000476837158f },
                { GradientAudioProcessor::pitchFineCentsAParamID, 7.450580596923828e-07f },
                { GradientAudioProcessor::pitchFineCentsBParamID, 50.0f },
                { GradientAudioProcessor::pitchSemitonesAParamID, 4.999999523162842f },
                { GradientAudioProcessor::pitchSemitonesBParamID, -5.364418029785156e-07f },
                { GradientAudioProcessor::spliceModeAParamID, 2.0f },
                { GradientAudioProcessor::spliceModeBParamID, 2.0f },
                { GradientAudioProcessor::widthPercentParamID, 59.0f },
            } },
            { "Skitter Scatter", {
                { GradientAudioProcessor::bypassParamID, 0.0f },
                { GradientAudioProcessor::crossFeedbackEnabledParamID, 1.0f },
                { GradientAudioProcessor::crossfadeLengthMsAParamID, 11.80000019073486f },
                { GradientAudioProcessor::crossfadeLengthMsBParamID, 10.90000057220459f },
                { GradientAudioProcessor::delaySubdivisionAParamID, 13.0f },
                { GradientAudioProcessor::delaySubdivisionBParamID, 5.0f },
                { GradientAudioProcessor::delaySyncEnabledAParamID, 0.0f },
                { GradientAudioProcessor::delaySyncEnabledBParamID, 0.0f },
                { GradientAudioProcessor::delayTimeMsAParamID, 59.60000228881836f },
                { GradientAudioProcessor::delayTimeMsBParamID, 64.5999984741211f },
                { GradientAudioProcessor::driftAmountAParamID, 74.30000305175781f },
                { GradientAudioProcessor::driftAmountBParamID, 80.80000305175781f },
                { GradientAudioProcessor::dualModeEnabledParamID, 1.0f },
                { GradientAudioProcessor::feedbackPercentAParamID, 27.0f },
                { GradientAudioProcessor::feedbackPercentBParamID, 50.40000152587891f },
                { GradientAudioProcessor::linkDelayIntervalMsParamID, 1.490116119384766e-05f },
                { GradientAudioProcessor::linkEnabledParamID, 0.0f },
                { GradientAudioProcessor::linkPitchIntervalSemitonesParamID, -5.364418029785156e-07f },
                { GradientAudioProcessor::mixPercentAParamID, 37.90000152587891f },
                { GradientAudioProcessor::mixPercentBParamID, 37.60000228881836f },
                { GradientAudioProcessor::outputTrimDbAParamID, 3.400000333786011f },
                { GradientAudioProcessor::outputTrimDbBParamID, 3.600000381469727f },
                { GradientAudioProcessor::pitchFineCentsAParamID, 7.450580596923828e-07f },
                { GradientAudioProcessor::pitchFineCentsBParamID, 7.450580596923828e-07f },
                { GradientAudioProcessor::pitchSemitonesAParamID, 2.999999284744263f },
                { GradientAudioProcessor::pitchSemitonesBParamID, 7.089999198913574f },
                { GradientAudioProcessor::spliceModeAParamID, 2.0f },
                { GradientAudioProcessor::spliceModeBParamID, 2.0f },
                { GradientAudioProcessor::widthPercentParamID, 65.0f },
            } },
            { "The Other Side", {
                { GradientAudioProcessor::bypassParamID, 0.0f },
                { GradientAudioProcessor::crossFeedbackEnabledParamID, 0.0f },
                { GradientAudioProcessor::crossfadeLengthMsAParamID, 11.80000019073486f },
                { GradientAudioProcessor::crossfadeLengthMsBParamID, 8.0f },
                { GradientAudioProcessor::delaySubdivisionAParamID, 13.0f },
                { GradientAudioProcessor::delaySubdivisionBParamID, 5.0f },
                { GradientAudioProcessor::delaySyncEnabledAParamID, 1.0f },
                { GradientAudioProcessor::delaySyncEnabledBParamID, 0.0f },
                { GradientAudioProcessor::delayTimeMsAParamID, 59.60000228881836f },
                { GradientAudioProcessor::delayTimeMsBParamID, 0.0f },
                { GradientAudioProcessor::driftAmountAParamID, 62.40000152587891f },
                { GradientAudioProcessor::driftAmountBParamID, 9.100000381469727f },
                { GradientAudioProcessor::dualModeEnabledParamID, 1.0f },
                { GradientAudioProcessor::feedbackPercentAParamID, 118.0999984741211f },
                { GradientAudioProcessor::feedbackPercentBParamID, 130.5f },
                { GradientAudioProcessor::linkDelayIntervalMsParamID, 1.490116119384766e-05f },
                { GradientAudioProcessor::linkEnabledParamID, 1.0f },
                { GradientAudioProcessor::linkPitchIntervalSemitonesParamID, -5.364418029785156e-07f },
                { GradientAudioProcessor::mixPercentAParamID, 42.90000152587891f },
                { GradientAudioProcessor::mixPercentBParamID, 38.0f },
                { GradientAudioProcessor::outputTrimDbAParamID, 3.576278686523438e-07f },
                { GradientAudioProcessor::outputTrimDbBParamID, 3.576278686523438e-07f },
                { GradientAudioProcessor::pitchFineCentsAParamID, 7.450580596923828e-07f },
                { GradientAudioProcessor::pitchFineCentsBParamID, 7.450580596923828e-07f },
                { GradientAudioProcessor::pitchSemitonesAParamID, 2.999999284744263f },
                { GradientAudioProcessor::pitchSemitonesBParamID, -5.364418029785156e-07f },
                { GradientAudioProcessor::spliceModeAParamID, 0.0f },
                { GradientAudioProcessor::spliceModeBParamID, 0.0f },
                { GradientAudioProcessor::widthPercentParamID, 100.0f },
            } },
        };

        return presets;
    }
}

GradientAudioProcessor::GradientAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets())
{
    pitchSemitonesAParam = apvts.getRawParameterValue(pitchSemitonesAParamID);
    pitchFineCentsAParam = apvts.getRawParameterValue(pitchFineCentsAParamID);
    delayTimeMsAParam = apvts.getRawParameterValue(delayTimeMsAParamID);
    delaySyncEnabledAParam = apvts.getRawParameterValue(delaySyncEnabledAParamID);
    delaySubdivisionAParam = apvts.getRawParameterValue(delaySubdivisionAParamID);
    feedbackPercentAParam = apvts.getRawParameterValue(feedbackPercentAParamID);
    spliceModeAParam = apvts.getRawParameterValue(spliceModeAParamID);
    crossfadeLengthMsAParam = apvts.getRawParameterValue(crossfadeLengthMsAParamID);
    driftAmountAParam = apvts.getRawParameterValue(driftAmountAParamID);
    mixPercentAParam = apvts.getRawParameterValue(mixPercentAParamID);
    outputTrimDbAParam = apvts.getRawParameterValue(outputTrimDbAParamID);

    pitchSemitonesBParam = apvts.getRawParameterValue(pitchSemitonesBParamID);
    pitchFineCentsBParam = apvts.getRawParameterValue(pitchFineCentsBParamID);
    delayTimeMsBParam = apvts.getRawParameterValue(delayTimeMsBParamID);
    delaySyncEnabledBParam = apvts.getRawParameterValue(delaySyncEnabledBParamID);
    delaySubdivisionBParam = apvts.getRawParameterValue(delaySubdivisionBParamID);
    feedbackPercentBParam = apvts.getRawParameterValue(feedbackPercentBParamID);
    spliceModeBParam = apvts.getRawParameterValue(spliceModeBParamID);
    crossfadeLengthMsBParam = apvts.getRawParameterValue(crossfadeLengthMsBParamID);
    driftAmountBParam = apvts.getRawParameterValue(driftAmountBParamID);
    mixPercentBParam = apvts.getRawParameterValue(mixPercentBParamID);
    outputTrimDbBParam = apvts.getRawParameterValue(outputTrimDbBParamID);

    dualModeEnabledParam = apvts.getRawParameterValue(dualModeEnabledParamID);
    widthPercentParam = apvts.getRawParameterValue(widthPercentParamID);

    linkEnabledParam = apvts.getRawParameterValue(linkEnabledParamID);
    linkPitchIntervalSemitonesParam = apvts.getRawParameterValue(linkPitchIntervalSemitonesParamID);
    linkDelayIntervalMsParam = apvts.getRawParameterValue(linkDelayIntervalMsParamID);

    crossFeedbackEnabledParam = apvts.getRawParameterValue(crossFeedbackEnabledParamID);

    bypassParam = apvts.getRawParameterValue(bypassParamID);
}

GradientAudioProcessor::~GradientAudioProcessor() = default;

const juce::StringArray& GradientAudioProcessor::getSubdivisionChoices()
{
    static const juce::StringArray choices = [] {
        juce::StringArray result;
        for (auto& subdivision : subdivisions)
            result.add(subdivision.label);
        return result;
    }();

    return choices;
}

juce::AudioProcessorValueTreeState::ParameterLayout GradientAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Explicit "+" for positive values on bipolar (can-go-negative) parameters, matching the
    // approved mockup's hardware-style readout convention (e.g. "+7.00 st") - juce::String doesn't
    // add a sign for positive numbers on its own, only a "-" for negative ones.
    auto signedString = [](float v, int decimals, const char* unit)
    {
        return (v > 0.0f ? juce::String("+") : juce::String()) + juce::String(v, decimals) + " " + unit;
    };

    // A and B's parameter sets are structurally identical (same ranges/defaults/labels) - built via
    // this helper for both rather than duplicating each block, since unit B (Milestone 6) needs the
    // exact same nine parameters unit A already has, just under a "B" id suffix and "2" name suffix.
    auto addUnitParams = [&params, &signedString](const juce::String& idSuffix, const juce::String& nameSuffix)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"pitchSemitones" + idSuffix, 1},
            "Pitch" + nameSuffix,
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("st")
                .withStringFromValueFunction([signedString](float v, int) { return signedString(v, 2, "st"); })));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"pitchFineCents" + idSuffix, 1},
            "Fine" + nameSuffix,
            juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("ct")
                .withStringFromValueFunction([signedString](float v, int) { return signedString(v, 1, "ct"); })));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"delayTimeMs" + idSuffix, 1},
            "Delay" + nameSuffix,
            juce::NormalisableRange<float>(0.0f, 1000.0f, 0.1f, 0.4f),
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("ms")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

        // Tempo sync - when on, the Delay knob above is unused (dimmed in the editor) and this
        // unit's delay time instead tracks the host tempo times the chosen note subdivision,
        // recomputed every block. Same pattern as Caverns' own Sync/Division.
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"delaySyncEnabled" + idSuffix, 1}, "Delay Sync" + nameSuffix, false));

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"delaySubdivision" + idSuffix, 1},
            "Delay Subdivision" + nameSuffix,
            getSubdivisionChoices(),
            defaultSubdivisionIndex));

        // Ceiling is 350%, not "just past unity" as first assumed: any active pitch shift costs real
        // energy at every splice (see GradientPitchShiftEngine.h's class comment and GradientFeedbackTests),
        // and overcoming that loss to reach genuine self-oscillation needs roughly 150-300% depending on
        // pitch amount and splice mode (the most extreme case, 24 semitones, needs the most margin since
        // repeated 24-semitone shifts alias past Nyquist quickly) - confirmed empirically, not just at
        // the old 115% ceiling which only ever self-oscillated for 0-semitone (bypass, no splice loss)
        // feedback. A skewed range keeps low-feedback (delay-pedal-style) settings easy to dial in
        // despite the wide ceiling.
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"feedbackPercent" + idSuffix, 1},
            "Feedback" + nameSuffix,
            juce::NormalisableRange<float>(0.0f, 350.0f, 0.1f, 0.4f),
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("%")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"spliceMode" + idSuffix, 1},
            "Splice Mode" + nameSuffix,
            juce::StringArray{"Normal", "Soft", "Smart"},
            0));

        // Ceiling matches GradientPitchShiftEngine's internal clamp (just under half the 30ms ramp
        // window, ~14.7ms - see getEffectiveCrossfadeSamples()): anything requested above that is
        // silently clamped anyway, so exposing a wider range here would give the knob a large dead
        // zone where turning it further does nothing audible.
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"crossfadeLengthMs" + idSuffix, 1},
            "Crossfade Length" + nameSuffix,
            juce::NormalisableRange<float>(1.0f, 14.0f, 0.1f),
            8.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("ms")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

        // Default 0%: Drift must be an opt-in character, not an always-on instability (per the
        // spec's "default off/very low").
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"driftAmount" + idSuffix, 1},
            "Drift" + nameSuffix,
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("%")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"mixPercent" + idSuffix, 1},
            "Mix" + nameSuffix,
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
            50.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("%")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"outputTrimDb" + idSuffix, 1},
            "Output" + nameSuffix,
            juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("dB")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " dB"; })));
    };

    addUnitParams("A", "");
    addUnitParams("B", " 2");

    // 6a: dual mode on/off. engineB is always constructed but only processed when this is on (see
    // the class comment) - mono duplicates engineA's output to both channels, exactly as before.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{dualModeEnabledParamID, 1}, "Dual Mode", false));

    // 6c: stereo width - simple output-stage mid/side blend on the two engines' final outputs (no
    // engine involvement, per the plan). 100% (default) preserves 6a's full independent-channel
    // behaviour (A purely left, B purely right); turning it down blends toward mono so the two
    // channels aren't locked to hard-left/hard-right. Has no audible effect in mono mode, where L
    // and R are already identical.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{widthPercentParamID, 1},
        "Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    // 6b: Link - when on, B's live Pitch/Delay are derived from A's values plus these intervals
    // each block, overriding B's own Pitch/Delay 2 knobs for that block (B's other parameters stay
    // independent regardless). Default 0 on both intervals: Link on with untouched intervals starts
    // as a unison double, matching what a user turning Link on without further adjustment would
    // expect, rather than an unexplained offset appearing.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{linkEnabledParamID, 1}, "Link", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{linkPitchIntervalSemitonesParamID, 1},
        "Link Pitch Interval",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("st")
            .withStringFromValueFunction([signedString](float v, int) { return signedString(v, 2, "st"); })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{linkDelayIntervalMsParamID, 1},
        "Link Delay Interval",
        juce::NormalisableRange<float>(-1000.0f, 1000.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([signedString](float v, int) { return signedString(v, 1, "ms"); })));

    // 6d: cross-feedback - A's output feeds B's feedback path and vice versa. Off by default: a
    // two-node coupled feedback loop's stability margin isn't just "the same as one engine, twice"
    // (see the plan's Milestone 6d note), so this should be an intentional choice, not a surprise.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{crossFeedbackEnabledParamID, 1}, "Cross-feedback", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1}, "Bypass", false));

    return {params.begin(), params.end()};
}

void GradientAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    sampleRateHz = sampleRate;
    engineA.prepare(sampleRate);
    engineB.prepare(sampleRate);
}

void GradientAudioProcessor::releaseResources() {}

double GradientAudioProcessor::getCurrentBpm() const
{
    constexpr double fallbackBpm = 120.0;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                if (*bpm > 0.0)
                    return *bpm;
        }
    }

    return fallbackBpm;
}

bool GradientAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void GradientAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    if (buffer.getNumChannels() < 2)
        return;

    // Tempo sync: when a unit's Delay Sync is on, its delay time tracks host tempo x the chosen
    // note subdivision instead of the raw Delay knob (which the editor dims while synced) -
    // recomputed every block, same pattern as Caverns' Sync/Division. getCurrentBpm() falls back
    // to 120 when nothing provides a playhead/tempo (e.g. the Standalone app with no transport).
    const auto quarterNoteMs = static_cast<float>(60000.0 / getCurrentBpm());
    auto computeEffectiveDelayMs = [quarterNoteMs](bool syncOn, int subdivisionIndex, float rawMs) -> float
    {
        if (!syncOn)
            return juce::jlimit(minDelayMs, maxDelayMs, rawMs);
        const auto idx = juce::jlimit(0, (int) std::size(subdivisions) - 1, subdivisionIndex);
        return juce::jlimit(minDelayMs, maxDelayMs, quarterNoteMs * subdivisions[(size_t) idx].quarterNoteMultiple);
    };

    engineA.setPitchSemitones(pitchSemitonesAParam->load(), pitchFineCentsAParam->load());
    const auto effectiveDelayMsA = computeEffectiveDelayMs(delaySyncEnabledAParam->load() > 0.5f,
                                                             (int) delaySubdivisionAParam->load(),
                                                             delayTimeMsAParam->load());
    engineA.setDelayTimeMs(effectiveDelayMsA);
    currentDelayMsA.store(effectiveDelayMsA, std::memory_order_relaxed);
    engineA.setFeedback(feedbackPercentAParam->load());
    engineA.setSpliceMode(static_cast<GradientPitchShiftEngine::SpliceMode>((int) spliceModeAParam->load()));
    engineA.setCrossfadeLengthMs(crossfadeLengthMsAParam->load());
    engineA.setDrift(driftAmountAParam->load());
    engineA.setMix(mixPercentAParam->load());
    engineA.setOutputTrimDb(outputTrimDbAParam->load());

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    const bool dualMode = dualModeEnabledParam->load() > 0.5f;

    if (!dualMode)
    {
        // Mono: both channels summed into the one engine, duplicated back out to L/R - unchanged
        // from Milestones 2-5.
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto monoIn = (left[sample] + right[sample]) * 0.5f;
            const auto out = engineA.process(monoIn);
            left[sample] = out;
            right[sample] = out;
        }
        return;
    }

    // 6a: dual mode - engineB gets its own independent parameters and processes R on its own,
    // engineA processes L on its own (not summed to mono). No Cross-feedback yet (6d).
    //
    // 6b: Link - when on, B's live Pitch/Delay are derived from A's just-set values plus a fixed
    // interval, overriding B's own Pitch 2/Delay 2 knobs for this block entirely (structurally the
    // same override pattern as Flux's Sync/Rate - B's manual knobs still hold their values, they're
    // just not read while Link is on). B's other parameters (Feedback, Splice mode, Drift, Mix,
    // Output) are read from B's own knobs either way.
    const bool linkOn = linkEnabledParam->load() > 0.5f;
    if (linkOn)
    {
        const auto linkedTotalSemitones = pitchSemitonesAParam->load() + pitchFineCentsAParam->load() * 0.01f
                                         + linkPitchIntervalSemitonesParam->load();
        engineB.setPitchSemitones(linkedTotalSemitones, 0.0f);

        // Derived from A's EFFECTIVE delay (sync-aware, already computed above), so Link correctly
        // tracks a synced A rather than A's unused raw knob value.
        const auto linkedDelayMs = std::max(0.0f, effectiveDelayMsA + linkDelayIntervalMsParam->load());
        engineB.setDelayTimeMs(linkedDelayMs);
        currentDelayMsB.store(linkedDelayMs, std::memory_order_relaxed);
    }
    else
    {
        engineB.setPitchSemitones(pitchSemitonesBParam->load(), pitchFineCentsBParam->load());
        const auto effectiveDelayMsB = computeEffectiveDelayMs(delaySyncEnabledBParam->load() > 0.5f,
                                                                 (int) delaySubdivisionBParam->load(),
                                                                 delayTimeMsBParam->load());
        engineB.setDelayTimeMs(effectiveDelayMsB);
        currentDelayMsB.store(effectiveDelayMsB, std::memory_order_relaxed);
    }

    engineB.setFeedback(feedbackPercentBParam->load());
    engineB.setSpliceMode(static_cast<GradientPitchShiftEngine::SpliceMode>((int) spliceModeBParam->load()));
    engineB.setCrossfadeLengthMs(crossfadeLengthMsBParam->load());
    engineB.setDrift(driftAmountBParam->load());
    engineB.setMix(mixPercentBParam->load());
    engineB.setOutputTrimDb(outputTrimDbBParam->load());

    // 6c: stereo width - a standard mid/side blend applied to the two engines' final outputs, not
    // inside either engine. 100% reproduces A-purely-left/B-purely-right exactly; 0% collapses to
    // mono (both channels get the mid signal).
    const auto widthAmount = widthPercentParam->load() * 0.01f;

    // 6d: cross-feedback - A's output feeds B's feedback path and vice versa, via each engine's
    // externalFeedbackSample parameter (unused until now). Both engines' outputs are computed FIRST
    // from the previous sample's lastOutputA/B, and only THEN are lastOutputA/B updated - critically
    // not interleaved (see the class comment for why that would create an asymmetric loop).
    const bool crossFeedbackOn = crossFeedbackEnabledParam->load() > 0.5f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto externalFeedbackForA = crossFeedbackOn ? lastOutputB : 0.0f;
        const auto externalFeedbackForB = crossFeedbackOn ? lastOutputA : 0.0f;

        const auto outA = engineA.process(left[sample], externalFeedbackForA);
        const auto outB = engineB.process(right[sample], externalFeedbackForB);

        lastOutputA = engineA.getLastWetSample();
        lastOutputB = engineB.getLastWetSample();

        const auto mid = (outA + outB) * 0.5f;
        const auto side = (outA - outB) * 0.5f * widthAmount;

        left[sample] = mid + side;
        right[sample] = mid - side;
    }
}

juce::AudioProcessorEditor* GradientAudioProcessor::createEditor()
{
    return new GradientAudioProcessorEditor(*this);
}

bool GradientAudioProcessor::hasEditor() const { return true; }

const juce::String GradientAudioProcessor::getName() const { return JucePlugin_Name; }

bool GradientAudioProcessor::acceptsMidi() const { return false; }
bool GradientAudioProcessor::producesMidi() const { return false; }
bool GradientAudioProcessor::isMidiEffect() const { return false; }
double GradientAudioProcessor::getTailLengthSeconds() const { return 2.0; }

int GradientAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int GradientAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void GradientAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String GradientAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }

void GradientAudioProcessor::changeProgramName(int, const juce::String&) {}

void GradientAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void GradientAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GradientAudioProcessor();
}
