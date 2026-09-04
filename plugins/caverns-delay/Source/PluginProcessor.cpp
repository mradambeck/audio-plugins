#include "PluginProcessor.h"

#include <cmath>
#include <iterator>

namespace
{
    // Note subdivisions offered for tempo sync, longest to shortest, each expressed as a
    // multiple of a quarter note's duration so they scale directly with host BPM.
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

    constexpr float minDelayMs = 1.0f;
    constexpr float maxDelayMs = 2000.0f;

    // Fixed cutoff for the darkening filter inside the feedback loop - this is what makes each
    // successive repeat duller than the last, since every pass around the loop goes through it
    // again. Not user-adjustable; it's the BBD chip's own character, not a tone control.
    constexpr float feedbackDarkenerHz = 4200.0f;

    // How hard each pass through the feedback loop is driven into soft saturation. tanh() keeps
    // this self-limiting even at high feedback, so it warms up the repeats without the loop ever
    // actually running away.
    constexpr float feedbackDriveAmount = 1.6f;

    // How long a delay-time change takes to glide to its new value, rather than jumping (and
    // clicking) instantly.
    constexpr float delayTimeGlideMs = 40.0f;

    // Delay-time modulation (vibrato on the repeats). The depth parameter only runs 0-20 (not
    // 0-100) - beyond that the wobble gets unusably extreme - so this ceiling is never fully
    // reached; full-scale depth (param = 20) works out to 20% of it. depth = 0 multiplies the LFO
    // down to exactly zero offset - no modulation, regardless of speed.
    constexpr float maxModDepthMs = 12.0f;

    // Ceiling for the second-stage "extra crunch" waveshaper's own drive amount, at full Degrade.
    // Cascaded on top of the fixed-drive stage below rather than summed into it - piling extra
    // drive directly into that first stage raises the feedback loop's near-silence gain, which is
    // what used to stretch out how long repeats take to decay regardless of the Feedback knob.
    // This second stage is built so its own small-signal gain is always exactly 1 (see
    // processBlock), so cranking it changes how hard loud/mid-level repeats get squashed without
    // ever touching decay time - that holds for any value here, so this can go as high as it needs
    // to sound obviously distorted rather than staying conservative. Pushed hard toward a fuzzy,
    // near-square-wave clip at full Degrade rather than a mild tilt.
    constexpr float degradeExtraDriveRange = 150.0f;

    // A second, independent saturation stage applied to the tap that's actually heard, not just
    // what loops back - the feedback-path stage above only colors repeats from the 2nd one onward
    // (the first repeat you hear is always a clean echo of the input), so at low Feedback there
    // isn't enough audible signal cycling through it to notice. This stage touches every repeat
    // immediately, at any Feedback setting, and - because it never feeds back into the delay line -
    // has no bearing on decay time, so unlike the feedback-path stage it's free to add real makeup
    // gain rather than just reshaping.
    constexpr float wetDegradeDriveRange = 20.0f;
    constexpr float wetDegradeMakeupRange = 3.0f;

    // How asymmetric the degrade waveshapers' clipping is between the positive and negative halves
    // of the waveform - a symmetric tanh sounds smooth; unequal drive on each side is what gives
    // fuzz pedals their lopsided, splattery character (odd+even harmonics instead of just odd).
    // Both stages still have slope 1 at u=0 from either side regardless of this value, so it has no
    // bearing on decay time either. 1.0 would be symmetric; well below 1 exaggerates the lopsidedness.
    constexpr float degradeAsymmetry = 0.5f;

    // How much the degrade drive itself wavers over time (riding the existing degrade-wobble LFO,
    // just a faster multiple of it, rather than a separate oscillator), instead of sitting at a
    // fixed intensity. This is what gave the loop its unstable, breathing/splattery quality back
    // when Degrade also extended feedback time - modulating the drive amount reproduces that same
    // instability in the distortion's texture without touching decay time, since slope-at-zero is 1
    // for whatever the instantaneous drive value happens to be at any given moment.
    constexpr float degradeDriveWobbleDepth = 0.5f;

    // Fixed rate for the degrade-wobble LFO (see degradeWobbleDelayL/R in the header) - not
    // user-adjustable, it's part of the "aging chip" character rather than a tempo-relative effect
    // like Mod Speed.
    constexpr float degradeWobbleRateHz = 0.85f;

    // Ceiling for how far the degrade-wobble flutter can push the feedback-path delay, at full
    // Degrade - dialed back down from an earlier, wobble-heavy tuning so Degrade leans on drive
    // instead.
    constexpr float maxDegradeWobbleMs = 4.0f;

    // How far the feedback darkener's cutoff is allowed to drop below feedbackDarkenerHz at full
    // Degrade. Kept modest on purpose - this is meant to read as a slight, gentle extra warmth on
    // top of the drive/wobble above, not another dramatic effect.
    constexpr float degradeDarkenerRangeHz = 1800.0f;

    // Factory presets: raw parameter values (the same values setValueNotifyingHost() takes after
    // normalising, not display percentages) applied in one shot when the preset is selected.
    const std::vector<wildjag::FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<wildjag::FactoryPreset> presets = {
            { "Carousel", {
                { CavernsAudioProcessor::bypassParamID, 0.0f },
                { CavernsAudioProcessor::degradeParamID, 14.40000057220459f },
                { CavernsAudioProcessor::dryParamID, 93.30000305175781f },
                { CavernsAudioProcessor::feedbackParamID, 24.70000076293945f },
                { CavernsAudioProcessor::highCutParamID, 2360.0f },
                { CavernsAudioProcessor::leftSubdivisionParamID, 12.0f },
                { CavernsAudioProcessor::leftTimeParamID, 83.30000305175781f },
                { CavernsAudioProcessor::linkParamID, 0.0f },
                { CavernsAudioProcessor::lowCutParamID, 467.0f },
                { CavernsAudioProcessor::modDepthParamID, 19.79999923706055f },
                { CavernsAudioProcessor::modSpeedParamID, 3.0f },
                { CavernsAudioProcessor::rightSubdivisionParamID, 12.0f },
                { CavernsAudioProcessor::rightTimeParamID, 101.5f },
                { CavernsAudioProcessor::syncParamID, 0.0f },
                { CavernsAudioProcessor::wetParamID, 48.79999923706055f },
            } },
            { "Into Space", {
                { CavernsAudioProcessor::bypassParamID, 0.0f },
                { CavernsAudioProcessor::degradeParamID, 9.0f },
                { CavernsAudioProcessor::dryParamID, 93.30000305175781f },
                { CavernsAudioProcessor::feedbackParamID, 43.5f },
                { CavernsAudioProcessor::highCutParamID, 7948.0f },
                { CavernsAudioProcessor::leftSubdivisionParamID, 12.0f },
                { CavernsAudioProcessor::leftTimeParamID, 83.30000305175781f },
                { CavernsAudioProcessor::linkParamID, 0.0f },
                { CavernsAudioProcessor::lowCutParamID, 134.0f },
                { CavernsAudioProcessor::modDepthParamID, 2.799999952316284f },
                { CavernsAudioProcessor::modSpeedParamID, 0.6899999976158142f },
                { CavernsAudioProcessor::rightSubdivisionParamID, 12.0f },
                { CavernsAudioProcessor::rightTimeParamID, 129.5f },
                { CavernsAudioProcessor::syncParamID, 0.0f },
                { CavernsAudioProcessor::wetParamID, 53.79999923706055f },
            } },
            { "Look a Ghost", {
                { CavernsAudioProcessor::bypassParamID, 0.0f },
                { CavernsAudioProcessor::degradeParamID, 9.600000381469727f },
                { CavernsAudioProcessor::dryParamID, 81.9000015258789f },
                { CavernsAudioProcessor::feedbackParamID, 19.10000038146973f },
                { CavernsAudioProcessor::highCutParamID, 4280.0f },
                { CavernsAudioProcessor::leftSubdivisionParamID, 8.0f },
                { CavernsAudioProcessor::leftTimeParamID, 149.3000030517578f },
                { CavernsAudioProcessor::linkParamID, 0.0f },
                { CavernsAudioProcessor::lowCutParamID, 749.0f },
                { CavernsAudioProcessor::modDepthParamID, 12.51999950408936f },
                { CavernsAudioProcessor::modSpeedParamID, 3.0f },
                { CavernsAudioProcessor::rightSubdivisionParamID, 8.0f },
                { CavernsAudioProcessor::rightTimeParamID, 298.5f },
                { CavernsAudioProcessor::syncParamID, 0.0f },
                { CavernsAudioProcessor::wetParamID, 52.60000228881836f },
            } },
            { "Outrun", {
                { CavernsAudioProcessor::bypassParamID, 0.0f },
                { CavernsAudioProcessor::degradeParamID, 5.700000286102295f },
                { CavernsAudioProcessor::dryParamID, 88.5999984741211f },
                { CavernsAudioProcessor::feedbackParamID, 5.599999904632568f },
                { CavernsAudioProcessor::highCutParamID, 8565.0f },
                { CavernsAudioProcessor::leftSubdivisionParamID, 9.0f },
                { CavernsAudioProcessor::leftTimeParamID, 83.30000305175781f },
                { CavernsAudioProcessor::linkParamID, 0.0f },
                { CavernsAudioProcessor::lowCutParamID, 382.0f },
                { CavernsAudioProcessor::modDepthParamID, 0.0f },
                { CavernsAudioProcessor::modSpeedParamID, 0.6499999761581421f },
                { CavernsAudioProcessor::rightSubdivisionParamID, 6.0f },
                { CavernsAudioProcessor::rightTimeParamID, 129.5f },
                { CavernsAudioProcessor::syncParamID, 1.0f },
                { CavernsAudioProcessor::wetParamID, 46.10000228881836f },
            } },
            { "Pins and Needles", {
                { CavernsAudioProcessor::bypassParamID, 0.0f },
                { CavernsAudioProcessor::degradeParamID, 8.40000057220459f },
                { CavernsAudioProcessor::dryParamID, 90.4000015258789f },
                { CavernsAudioProcessor::feedbackParamID, 33.20000076293945f },
                { CavernsAudioProcessor::highCutParamID, 5739.0f },
                { CavernsAudioProcessor::leftSubdivisionParamID, 8.0f },
                { CavernsAudioProcessor::leftTimeParamID, 83.30000305175781f },
                { CavernsAudioProcessor::linkParamID, 1.0f },
                { CavernsAudioProcessor::lowCutParamID, 553.0f },
                { CavernsAudioProcessor::modDepthParamID, 10.05999946594238f },
                { CavernsAudioProcessor::modSpeedParamID, 0.07999999821186066f },
                { CavernsAudioProcessor::rightSubdivisionParamID, 10.0f },
                { CavernsAudioProcessor::rightTimeParamID, 129.5f },
                { CavernsAudioProcessor::syncParamID, 1.0f },
                { CavernsAudioProcessor::wetParamID, 62.5f },
            } },
            { "Return Function", {
                { CavernsAudioProcessor::bypassParamID, 0.0f },
                { CavernsAudioProcessor::degradeParamID, 25.0f },
                { CavernsAudioProcessor::dryParamID, 100.0f },
                { CavernsAudioProcessor::feedbackParamID, 43.5f },
                { CavernsAudioProcessor::highCutParamID, 12816.0f },
                { CavernsAudioProcessor::leftSubdivisionParamID, 12.0f },
                { CavernsAudioProcessor::leftTimeParamID, 250.0f },
                { CavernsAudioProcessor::linkParamID, 0.0f },
                { CavernsAudioProcessor::lowCutParamID, 801.0f },
                { CavernsAudioProcessor::modDepthParamID, 6.799999713897705f },
                { CavernsAudioProcessor::modSpeedParamID, 0.07999999821186066f },
                { CavernsAudioProcessor::rightSubdivisionParamID, 12.0f },
                { CavernsAudioProcessor::rightTimeParamID, 250.0f },
                { CavernsAudioProcessor::syncParamID, 0.0f },
                { CavernsAudioProcessor::wetParamID, 47.5f },
            } },
            { "Surround Me", {
                { CavernsAudioProcessor::bypassParamID, 0.0f },
                { CavernsAudioProcessor::degradeParamID, 11.0f },
                { CavernsAudioProcessor::dryParamID, 85.5999984741211f },
                { CavernsAudioProcessor::feedbackParamID, 16.30000114440918f },
                { CavernsAudioProcessor::highCutParamID, 4675.0f },
                { CavernsAudioProcessor::leftSubdivisionParamID, 7.0f },
                { CavernsAudioProcessor::leftTimeParamID, 83.30000305175781f },
                { CavernsAudioProcessor::linkParamID, 0.0f },
                { CavernsAudioProcessor::lowCutParamID, 424.0f },
                { CavernsAudioProcessor::modDepthParamID, 1.279999971389771f },
                { CavernsAudioProcessor::modSpeedParamID, 0.6499999761581421f },
                { CavernsAudioProcessor::rightSubdivisionParamID, 5.0f },
                { CavernsAudioProcessor::rightTimeParamID, 129.5f },
                { CavernsAudioProcessor::syncParamID, 1.0f },
                { CavernsAudioProcessor::wetParamID, 76.5f },
            } },
            { "Talk to Me", {
                { CavernsAudioProcessor::bypassParamID, 0.0f },
                { CavernsAudioProcessor::degradeParamID, 25.0f },
                { CavernsAudioProcessor::dryParamID, 90.4000015258789f },
                { CavernsAudioProcessor::feedbackParamID, 30.10000038146973f },
                { CavernsAudioProcessor::highCutParamID, 1890.0f },
                { CavernsAudioProcessor::leftSubdivisionParamID, 11.0f },
                { CavernsAudioProcessor::leftTimeParamID, 83.30000305175781f },
                { CavernsAudioProcessor::linkParamID, 1.0f },
                { CavernsAudioProcessor::lowCutParamID, 119.0f },
                { CavernsAudioProcessor::modDepthParamID, 0.0f },
                { CavernsAudioProcessor::modSpeedParamID, 0.01999999955296516f },
                { CavernsAudioProcessor::rightSubdivisionParamID, 10.0f },
                { CavernsAudioProcessor::rightTimeParamID, 129.5f },
                { CavernsAudioProcessor::syncParamID, 1.0f },
                { CavernsAudioProcessor::wetParamID, 40.90000152587891f },
            } },
        };

        return presets;
    }
}

CavernsAudioProcessor::CavernsAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets())
{
    bypassParam = apvts.getRawParameterValue(bypassParamID);
    syncParam = apvts.getRawParameterValue(syncParamID);
    linkParam = apvts.getRawParameterValue(linkParamID);
    leftSubdivisionParam = apvts.getRawParameterValue(leftSubdivisionParamID);
    rightSubdivisionParam = apvts.getRawParameterValue(rightSubdivisionParamID);
    leftTimeParam = apvts.getRawParameterValue(leftTimeParamID);
    rightTimeParam = apvts.getRawParameterValue(rightTimeParamID);
    feedbackParam = apvts.getRawParameterValue(feedbackParamID);
    dryParam = apvts.getRawParameterValue(dryParamID);
    wetParam = apvts.getRawParameterValue(wetParamID);
    lowCutParam = apvts.getRawParameterValue(lowCutParamID);
    highCutParam = apvts.getRawParameterValue(highCutParamID);
    modSpeedParam = apvts.getRawParameterValue(modSpeedParamID);
    modDepthParam = apvts.getRawParameterValue(modDepthParamID);
    degradeParam = apvts.getRawParameterValue(degradeParamID);
}

CavernsAudioProcessor::~CavernsAudioProcessor() = default;

const juce::StringArray& CavernsAudioProcessor::getSubdivisionChoices()
{
    static const juce::StringArray choices = [] {
        juce::StringArray result;
        for (auto& subdivision : subdivisions)
            result.add(subdivision.label);
        return result;
    }();

    return choices;
}

juce::AudioProcessorValueTreeState::ParameterLayout CavernsAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1},
        "Bypass",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{syncParamID, 1},
        "Sync",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{linkParamID, 1},
        "Link",
        false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{leftSubdivisionParamID, 1},
        "L Division",
        getSubdivisionChoices(),
        defaultSubdivisionIndex));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{rightSubdivisionParamID, 1},
        "R Division",
        getSubdivisionChoices(),
        defaultSubdivisionIndex));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{leftTimeParamID, 1},
        "L Time",
        juce::NormalisableRange<float>(minDelayMs, maxDelayMs, 0.1f, 0.3f),
        350.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{rightTimeParamID, 1},
        "R Time",
        juce::NormalisableRange<float>(minDelayMs, maxDelayMs, 0.1f, 0.3f),
        350.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

    {
        // Real ceiling is 65% feedback, not 100 - past that the repeats run away rather than
        // just building. The knob still reads 0-100% of its own travel though, so "all the way
        // right" always means 100% to the user even though the actual feedback amount tops out
        // lower.
        const juce::NormalisableRange<float> feedbackRange(0.0f, 65.0f, 0.1f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{feedbackParamID, 1},
            "Feedback",
            feedbackRange,
            35.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("%")
                .withStringFromValueFunction([feedbackRange](float v, int) {
                    return juce::String(feedbackRange.convertTo0to1(v) * 100.0f, 1) + "%";
                })
                .withValueFromStringFunction([feedbackRange](const juce::String& text) {
                    return feedbackRange.convertFrom0to1(text.getFloatValue() * 0.01f);
                })));
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{dryParamID, 1},
        "Dry",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{wetParamID, 1},
        "Wet",
        // Goes past 100% (unity) up to 200%, so the wet signal can be pushed louder than the
        // dry tap itself rather than topping out at matching it.
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        35.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{lowCutParamID, 1},
        "Low Cut",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.4f),
        80.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 0) + " Hz"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{highCutParamID, 1},
        "High Cut",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f, 0.3f),
        8000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 0) + " Hz"; })));

    {
        // Real range stays 0.02-8 Hz (unchanged) - only the on-screen readout changes, to a plain
        // 0-100% of the knob's travel rather than the raw Hz value underneath.
        const juce::NormalisableRange<float> modSpeedRange(0.02f, 8.0f, 0.01f, 0.3f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{modSpeedParamID, 1},
            "Mod Speed",
            modSpeedRange,
            0.6f,
            juce::AudioParameterFloatAttributes()
                .withLabel("%")
                .withStringFromValueFunction([modSpeedRange](float v, int) {
                    return juce::String(modSpeedRange.convertTo0to1(v) * 100.0f, 1) + "%";
                })
                .withValueFromStringFunction([modSpeedRange](const juce::String& text) {
                    return modSpeedRange.convertFrom0to1(text.getFloatValue() * 0.01f);
                })));
    }

    {
        // Real range stays 0-20 (unchanged, see maxModDepthMs above) - same readout treatment as
        // Mod Speed: displayed as 0-100% of travel rather than the raw number underneath.
        const juce::NormalisableRange<float> modDepthRange(0.0f, 20.0f, 0.02f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{modDepthParamID, 1},
            "Mod Depth",
            modDepthRange,
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("%")
                .withStringFromValueFunction([modDepthRange](float v, int) {
                    return juce::String(modDepthRange.convertTo0to1(v) * 100.0f, 1) + "%";
                })
                .withValueFromStringFunction([modDepthRange](const juce::String& text) {
                    return modDepthRange.convertFrom0to1(text.getFloatValue() * 0.01f);
                })));
    }

    {
        // Real ceiling is 25 (not 100) - full right now reaches what used to sit at the knob's
        // 25% mark, since the old full-right setting was judged too extreme. The knob still reads
        // 0-100% of its own travel though, same treatment as Feedback above.
        const juce::NormalisableRange<float> degradeRange(0.0f, 25.0f, 0.1f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{degradeParamID, 1},
            "Degrade",
            // How much extra drive and flutter the feedback loop picks up per pass - see
            // degradeExtraDriveRange/degradeWobbleRateHz/maxDegradeWobbleMs above. 0% reproduces
            // the exact same character as before this control existed.
            degradeRange,
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("%")
                .withStringFromValueFunction([degradeRange](float v, int) {
                    return juce::String(degradeRange.convertTo0to1(v) * 100.0f, 1) + "%";
                })
                .withValueFromStringFunction([degradeRange](const juce::String& text) {
                    return degradeRange.convertFrom0to1(text.getFloatValue() * 0.01f);
                })));
    }

    return {params.begin(), params.end()};
}

void CavernsAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 1;

    const auto maxDelaySamples = static_cast<int>(2.5 * sampleRate) + 1;
    maxDelaySamplesLimit = static_cast<float>(maxDelaySamples - 1);
    modPhase = 0.0;

    // Force an unconditional recompute of all three cached filter coefficients on the first block
    // after this call - see the members' own comment for why a cached Hz value can't survive a
    // sample-rate change.
    lastDegradeDarkenerHz = lastLowCutHz = lastHighCutHz = -1.0f;

    delayLineL.setMaximumDelayInSamples(maxDelaySamples);
    delayLineL.prepare(spec);
    delayLineL.reset();

    delayLineR.setMaximumDelayInSamples(maxDelaySamples);
    delayLineR.prepare(spec);
    delayLineR.reset();

    feedbackDarkenerL.prepare(spec);
    feedbackDarkenerL.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, feedbackDarkenerHz);
    feedbackDarkenerL.reset();

    feedbackDarkenerR.prepare(spec);
    feedbackDarkenerR.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, feedbackDarkenerHz);
    feedbackDarkenerR.reset();

    const auto maxDegradeWobbleSamplesInt = static_cast<int>(maxDegradeWobbleMs * 0.001 * sampleRate) + 1;
    maxDegradeWobbleSamples = static_cast<float>(maxDegradeWobbleSamplesInt - 1);
    degradeWobblePhase = 0.0;

    degradeWobbleDelayL.setMaximumDelayInSamples(maxDegradeWobbleSamplesInt);
    degradeWobbleDelayL.prepare(spec);
    degradeWobbleDelayL.reset();

    degradeWobbleDelayR.setMaximumDelayInSamples(maxDegradeWobbleSamplesInt);
    degradeWobbleDelayR.prepare(spec);
    degradeWobbleDelayR.reset();

    lowCutFilterL.prepare(spec);
    lowCutFilterR.prepare(spec);
    highCutFilterL.prepare(spec);
    highCutFilterR.prepare(spec);
    lowCutFilterL.reset();
    lowCutFilterR.reset();
    highCutFilterL.reset();
    highCutFilterR.reset();

    leftDelaySamplesSmoothed.reset(sampleRate, delayTimeGlideMs * 0.001);
    rightDelaySamplesSmoothed.reset(sampleRate, delayTimeGlideMs * 0.001);
    leftDelaySamplesSmoothed.setCurrentAndTargetValue(static_cast<float>(0.001 * leftTimeParam->load() * sampleRate));
    rightDelaySamplesSmoothed.setCurrentAndTargetValue(static_cast<float>(0.001 * rightTimeParam->load() * sampleRate));
}

void CavernsAudioProcessor::releaseResources() {}

double CavernsAudioProcessor::getCurrentBpm() const
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

bool CavernsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void CavernsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    const auto numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2)
        return;

    const auto syncOn = syncParam->load() > 0.5f;
    const auto linkOn = linkParam->load() > 0.5f;

    // Work out this block's target delay times. When synced, the subdivision choices and host
    // tempo decide the time; otherwise the raw ms parameters do. Link makes the right channel
    // follow the left regardless of its own controls.
    float targetLeftMs;
    float targetRightMs;

    if (syncOn)
    {
        const auto quarterNoteMs = static_cast<float>(60000.0 / getCurrentBpm());
        const auto leftIndex = juce::jlimit(0, (int) std::size(subdivisions) - 1,
                                             static_cast<int>(leftSubdivisionParam->load()));
        const auto rightIndex = juce::jlimit(0, (int) std::size(subdivisions) - 1,
                                              static_cast<int>(rightSubdivisionParam->load()));

        targetLeftMs = quarterNoteMs * subdivisions[(size_t) leftIndex].quarterNoteMultiple;
        targetRightMs = linkOn ? targetLeftMs
                                : quarterNoteMs * subdivisions[(size_t) rightIndex].quarterNoteMultiple;
    }
    else
    {
        targetLeftMs = leftTimeParam->load();
        targetRightMs = linkOn ? targetLeftMs : rightTimeParam->load();
    }

    targetLeftMs = juce::jlimit(minDelayMs, maxDelayMs, targetLeftMs);
    targetRightMs = juce::jlimit(minDelayMs, maxDelayMs, targetRightMs);

    currentLeftDelayMs.store(targetLeftMs, std::memory_order_relaxed);
    currentRightDelayMs.store(targetRightMs, std::memory_order_relaxed);

    leftDelaySamplesSmoothed.setTargetValue(static_cast<float>(0.001 * targetLeftMs * sampleRateHz));
    rightDelaySamplesSmoothed.setTargetValue(static_cast<float>(0.001 * targetRightMs * sampleRateHz));

    const auto feedbackAmount = feedbackParam->load() * 0.01f;
    const auto dryGain = dryParam->load() * 0.01f;
    const auto wetGain = wetParam->load() * 0.01f;

    const auto modDepthMs = modDepthParam->load() * 0.01f * maxModDepthMs;
    const auto modPhaseIncrement = static_cast<double>(juce::MathConstants<float>::twoPi)
                                    * modSpeedParam->load() / sampleRateHz;

    const auto degradeAmount = degradeParam->load() * 0.01f;
    const auto degradeExtraDrive = degradeAmount * degradeExtraDriveRange;
    const auto wetDegradeDrive = degradeAmount * wetDegradeDriveRange;
    const auto wetDegradeMakeup = 1.0f + degradeAmount * wetDegradeMakeupRange;
    const auto degradeWobbleCeilingSamples = degradeAmount * maxDegradeWobbleSamples;
    const auto degradeWobblePhaseIncrement = static_cast<double>(juce::MathConstants<float>::twoPi)
                                              * degradeWobbleRateHz / sampleRateHz;

    // Degrade also gently pulls the feedback darkener's cutoff down from its fixed baseline, on
    // top of the drive/wobble above - a slight extra warmth that deepens with every pass, rather
    // than another dramatic effect.
    const auto degradeDarkenerHz = feedbackDarkenerHz - degradeAmount * degradeDarkenerRangeHz;

    // IIR::Coefficients::makeLowPass()/makeHighPass() each do a genuine std::tan() plus a heap
    // allocation (a new reference-counted Coefficients object) - skip recompute when the Hz value
    // hasn't changed since the last block. See lastDegradeDarkenerHz/lastLowCutHz/lastHighCutHz's
    // own member comment for why prepareToPlay() resets these sentinels.
    if (std::abs(degradeDarkenerHz - lastDegradeDarkenerHz) > 0.0f)
    {
        lastDegradeDarkenerHz = degradeDarkenerHz;
        feedbackDarkenerL.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRateHz, degradeDarkenerHz);
        feedbackDarkenerR.coefficients = feedbackDarkenerL.coefficients;
    }

    const auto nyquist = static_cast<float>(sampleRateHz * 0.49);
    const auto lowCutHz = juce::jmin(lowCutParam->load(), nyquist);
    const auto highCutHz = juce::jmin(highCutParam->load(), nyquist);

    if (std::abs(lowCutHz - lastLowCutHz) > 0.0f)
    {
        lastLowCutHz = lowCutHz;
        lowCutFilterL.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRateHz, lowCutHz);
        lowCutFilterR.coefficients = lowCutFilterL.coefficients;
    }
    if (std::abs(highCutHz - lastHighCutHz) > 0.0f)
    {
        lastHighCutHz = highCutHz;
        highCutFilterL.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRateHz, highCutHz);
        highCutFilterR.coefficients = highCutFilterL.coefficients;
    }

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    // modDepthMs is block-constant; when it's exactly 0 (the default), modOffsetSamples is exactly
    // 0 regardless of sin(modPhase)'s value (sin() is always finite, and finite * 0.0f == 0.0f
    // exactly) - skip the sin() call itself in that case. modPhase keeps advancing every sample
    // either way (see its own member comment) so there's no discontinuity if depth is raised
    // mid-playback.
    const auto modActive = modDepthMs > 0.0f;

    // degradeExtraDrive/wetDegradeDrive/degradeWobbleCeilingSamples are all block-constant and
    // exactly 0 when Degrade is at its default - multiplied against degradeDriveWobble (finite,
    // bounded [0.5,1.5]) or (0.5+0.5*sin(x)) (bounded [0,1]), the resulting degradeK/wetDegradeK/
    // degradeWobbleSamples are exactly 0 regardless of the sin() terms' actual values (already
    // proven irrelevant by the existing degradeK > 0.0f / wetDegradeK > 0.0f guards below) - skip
    // both sin() evaluations in that case. Phase increments stay unconditional, same reasoning as
    // modPhase above.
    const auto degradeActive = degradeAmount > 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto modOffsetSamples = modActive
                                           ? static_cast<float>(std::sin(modPhase)) * modDepthMs * 0.001f * static_cast<float>(sampleRateHz)
                                           : 0.0f;
        modPhase += modPhaseIncrement;
        if (modPhase >= juce::MathConstants<double>::twoPi)
            modPhase -= juce::MathConstants<double>::twoPi;

        delayLineL.setDelay(juce::jlimit(0.0f, maxDelaySamplesLimit,
                                          leftDelaySamplesSmoothed.getNextValue() + modOffsetSamples));
        delayLineR.setDelay(juce::jlimit(0.0f, maxDelaySamplesLimit,
                                          rightDelaySamplesSmoothed.getNextValue() + modOffsetSamples));

        const auto inL = left[sample];
        const auto inR = right[sample];

        const auto tapL = delayLineL.popSample(0);
        const auto tapR = delayLineR.popSample(0);

        // Shared by both degrade waveshapers below (feedback-path and wet-tap) so their splatter
        // moves together rather than independently. Riding degradeWobblePhase at 3x its own rate
        // gives a faster, more chattery wobble than the (slow, ~1 Hz) delay-flutter use of the same
        // phase elsewhere in this loop.
        const auto degradeDriveWobble = degradeActive
                                             ? 1.0f + degradeDriveWobbleDepth * static_cast<float>(std::sin(degradeWobblePhase * 3.0))
                                             : 1.0f;

        // Only the feedback path - not the tap that's actually heard - passes through the darkening
        // filter and saturator, so the character compounds one repeat at a time. Stage 1 always uses
        // the fixed base drive - never boosted by Degrade - so the quiet tail of a decaying repeat
        // (where decay time is actually decided) is untouched by Degrade at any setting; only
        // Feedback governs decay time. Stage 2 layers Degrade's extra crunch on top: tanh(k*u)/k has
        // slope exactly 1 at u=0 for any k > 0 - regardless of wobble or the pos/neg asymmetry below -
        // so it can never change the loop's near-silence gain no matter how hard Degrade drives it,
        // it only reshapes louder/mid-level signal. degradeExtraDrive == 0 (Degrade == 0) is guarded
        // to an exact passthrough, since tanh(k*u)/k is a 0/0 indeterminate form as k -> 0 - this is
        // what keeps 0% Degrade bit-identical to the pre-Degrade sound.
        // Filter output must still be computed every sample regardless (its internal state has to
        // keep advancing) - only the tanh() call itself is skipped on exact silence. tanh(0)==0
        // exactly per IEEE-754, so this is bit-identical, not an approximation.
        const auto stage1PreDriveL = feedbackDarkenerL.processSample(tapL) * feedbackDriveAmount;
        const auto stage1PreDriveR = feedbackDarkenerR.processSample(tapR) * feedbackDriveAmount;
        const auto stage1L = std::abs(stage1PreDriveL) <= 0.0f ? stage1PreDriveL : std::tanh(stage1PreDriveL);
        const auto stage1R = std::abs(stage1PreDriveR) <= 0.0f ? stage1PreDriveR : std::tanh(stage1PreDriveR);

        const auto degradeK = degradeExtraDrive * degradeDriveWobble;
        const auto degradeKNeg = degradeK * degradeAsymmetry;

        const auto stage2L = degradeK > 0.0f
                                  ? (stage1L >= 0.0f ? std::tanh(stage1L * degradeK) / degradeK
                                                      : std::tanh(stage1L * degradeKNeg) / degradeKNeg)
                                  : stage1L;
        const auto stage2R = degradeK > 0.0f
                                  ? (stage1R >= 0.0f ? std::tanh(stage1R * degradeK) / degradeK
                                                      : std::tanh(stage1R * degradeKNeg) / degradeKNeg)
                                  : stage1R;

        const auto drivenL = stage2L * feedbackAmount;
        const auto drivenR = stage2R * feedbackAmount;

        // Degrade-driven flutter: a tiny modulated delay sitting only in the feedback return path,
        // so it too compounds with every additional loop rather than applying evenly to everything.
        // Silent (0-sample, exact passthrough) at Degrade = 0.
        const auto degradeWobbleSamples = degradeActive
                                               ? degradeWobbleCeilingSamples * (0.5f + 0.5f * static_cast<float>(std::sin(degradeWobblePhase)))
                                               : 0.0f;
        degradeWobblePhase += degradeWobblePhaseIncrement;
        if (degradeWobblePhase >= juce::MathConstants<double>::twoPi)
            degradeWobblePhase -= juce::MathConstants<double>::twoPi;

        degradeWobbleDelayL.setDelay(degradeWobbleSamples);
        degradeWobbleDelayR.setDelay(degradeWobbleSamples);
        degradeWobbleDelayL.pushSample(0, drivenL);
        degradeWobbleDelayR.pushSample(0, drivenR);
        const auto feedbackL = degradeWobbleDelayL.popSample(0);
        const auto feedbackR = degradeWobbleDelayR.popSample(0);

        delayLineL.pushSample(0, inL + feedbackL);
        delayLineR.pushSample(0, inR + feedbackR);

        // Separate from the feedback-path stages above: saturates the tap itself, so Degrade is
        // audible on every repeat immediately rather than only on later, feedback-sustained ones.
        // Free to include real makeup gain here since this never re-enters the delay line. Shares
        // the same wobble and asymmetry as the feedback-path stage above so both move together.
        const auto wetDegradeK = wetDegradeDrive * degradeDriveWobble;
        const auto wetDegradeKNeg = wetDegradeK * degradeAsymmetry;

        const auto heardTapL = wetDegradeK > 0.0f
                                    ? (tapL >= 0.0f ? std::tanh(tapL * wetDegradeK) / wetDegradeK
                                                     : std::tanh(tapL * wetDegradeKNeg) / wetDegradeKNeg) * wetDegradeMakeup
                                    : tapL;
        const auto heardTapR = wetDegradeK > 0.0f
                                    ? (tapR >= 0.0f ? std::tanh(tapR * wetDegradeK) / wetDegradeK
                                                     : std::tanh(tapR * wetDegradeKNeg) / wetDegradeKNeg) * wetDegradeMakeup
                                    : tapR;

        auto wetL = lowCutFilterL.processSample(heardTapL);
        wetL = highCutFilterL.processSample(wetL);

        auto wetR = lowCutFilterR.processSample(heardTapR);
        wetR = highCutFilterR.processSample(wetR);

        left[sample] = inL * dryGain + wetL * wetGain;
        right[sample] = inR * dryGain + wetR * wetGain;
    }
}

// createEditor() lives in PluginEditor.cpp (not here) specifically so this file has no
// PluginEditor.h/GUI dependency - CavernsTests links only this file plus juce_audio_processors_headless.
bool CavernsAudioProcessor::hasEditor() const { return true; }

const juce::String CavernsAudioProcessor::getName() const { return JucePlugin_Name; }

bool CavernsAudioProcessor::acceptsMidi() const { return false; }
bool CavernsAudioProcessor::producesMidi() const { return false; }
bool CavernsAudioProcessor::isMidiEffect() const { return false; }
double CavernsAudioProcessor::getTailLengthSeconds() const { return 4.0; }

int CavernsAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int CavernsAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void CavernsAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String CavernsAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }

void CavernsAudioProcessor::changeProgramName(int, const juce::String&) {}

void CavernsAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void CavernsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CavernsAudioProcessor();
}
