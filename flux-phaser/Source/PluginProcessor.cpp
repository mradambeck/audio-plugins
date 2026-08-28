#include "PluginProcessor.h"

#include <cmath>
#include <iterator>

namespace
{
    // Note subdivisions offered for LFO tempo sync, longest to shortest, each expressed as a
    // multiple of a quarter note's duration so they scale directly with host BPM. Same table/
    // convention as Caverns' delay-time sync, just driving an LFO period instead of a delay time.
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

    constexpr int defaultDivisionIndex = 2; // "1/2" - a slow, two-beat sweep by default

    // Stage-count choices. All even, per the first-order-allpass-stage phaser convention (see
    // FluxAllpassStage in the header) - N cascaded stages produce N/2 notches.
    constexpr int stageCounts[] = { 2, 4, 6, 8, 12, 24, 36 };
    constexpr int defaultStageChoiceIndex = 1; // "4" - the classic Phase-90-style stage count

    // The allpass sweep centre/range, in Hz, before Offset/Depth are applied - see Offset/Depth
    // mapping below.
    constexpr float nominalCenterHz = 700.0f;
    constexpr float offsetOctaveRange = 2.0f;   // Offset = +/-100% shifts the centre by this many octaves
    constexpr float depthOctaveRange = 2.5f;    // Depth = 100% sweeps this many octaves each side of centre
    constexpr float minAllpassHz = 40.0f;
    constexpr float maxAllpassHz = 15000.0f;

    // Subtle stereo widening (not independently user-controllable): the L/R sweep frequencies
    // diverge by a small amount that tracks the LFO itself, not a separate oscillator. Half-wave
    // rectified (only the positive part of lfoValue engages it) so the width only builds through
    // the *upper* register of each LFO cycle and fully relaxes to mono through the lower half,
    // rather than being constantly present.
    constexpr float stereoWidthMaxOctaves = 0.05f;

    // Fixed safety saturator on the feedback tap only (not user-adjustable, unlike Grit below) -
    // tanh(k*x)/k is self-limiting no matter how high Feedback is pushed, the same trick Caverns
    // uses on its own feedback loop, so the loop can never actually run away regardless of the
    // Feedback knob's position.
    constexpr float feedbackSafetyDrive = 1.5f;

    // Ceiling for Grit's own drive amount at 100%.
    constexpr float gritDriveRange = 30.0f;

    // tanh(k*x)/k (see gritK below) keeps slope exactly 1 at x=0 for any k, which is what makes
    // Grit an exact passthrough at 0% - but that same /k is what makes the *saturated* portion of
    // the signal quieter as k grows (tanh saturates to +/-1, so the ceiling is +/-1/k), so without
    // this, cranking Grit toward 100% made the effect progressively weaker and mushier instead of
    // more aggressive. This makeup gain (1x at 0%, growing independently of gritDriveRange) is what
    // keeps the clipped signal loud - and, pushed hard enough at high Grit, squashed - instead of
    // just shrinking away.
    constexpr float gritMakeupRange = 6.0f;

    // A straight output trim riding on Grit, on top of gritMakeup above - 0dB at 0%, -5dB at
    // 100%, so the makeup gain's loudness boost doesn't just keep climbing unchecked as Grit is
    // pushed harder; the signal still gets more aggressively clipped, just not louder and louder.
    constexpr float gritOutputTrimMaxDb = -5.0f;

    constexpr float brightnessMinHz = 500.0f;
    constexpr float brightnessMaxHz = 18000.0f;

    // log2() of the three constants directly above - fixed for the life of the process (not just
    // the block), but std::log2() isn't constexpr in C++17, so these are computed once here via
    // static initialization instead of being recomputed from scratch every block.
    const float log2NominalCenterHz = std::log2(nominalCenterHz);
    const float log2BrightnessMinHz = std::log2(brightnessMinHz);
    const float log2BrightnessMaxHz = std::log2(brightnessMaxHz);

    // A peaking cut riding on the Grit knob, not a separate control - 0dB (no effect) at Grit =
    // 0%, deepening to gritEqMaxGainDb as Grit reaches 100%.
    constexpr float gritEqHz = 110.0f;
    constexpr float gritEqQ = 0.60f;
    constexpr float gritEqMaxGainDb = -3.3f;

    // First-order allpass coefficient for a given break frequency (where phase shift crosses 90
    // degrees) - see FluxAllpassStage::processSample() in the header for how this is used.
    // Smoothly spans (-1, 1) as freqHz sweeps 0 -> Nyquist, which is what keeps the stage stable.
    float coefficientForFrequency(float freqHz, double sampleRate)
    {
        const auto clampedHz = juce::jlimit(1.0f, (float) (sampleRate * 0.49), freqHz);
        const auto t = std::tan(juce::MathConstants<float>::pi * clampedHz / (float) sampleRate);
        return (t - 1.0f) / (t + 1.0f);
    }

    // The three LFO shapes, all phase-aligned so their zero-crossings/transitions land at the
    // same points (p = 0 and p = 0.5) - without that alignment, sweeping Shape from one waveform
    // to another would jump rather than blend smoothly.
    float squareWave(float p) noexcept { return p < 0.5f ? 1.0f : -1.0f; }

    float triangleWave(float p) noexcept
    {
        if (p < 0.25f) return 4.0f * p;
        if (p < 0.75f) return 2.0f - 4.0f * p;
        return -4.0f + 4.0f * p;
    }

    float sineWave(float p) noexcept
    {
        return std::sin(juce::MathConstants<float>::twoPi * p);
    }

    // Where the knob reaches pure triangle - not the knob's literal midpoint (0.5), since a plain
    // 50/50 split made the square portion of the turn feel oversized relative to how quickly it
    // reads as "square" by ear. 30% less than that half (0.5 * 0.7) hands the freed-up travel to
    // the triangle->sine half instead.
    constexpr float shapeBreakpoint = 0.35f;

    // shapeAmount: 0 = square, shapeBreakpoint = triangle, 1 = sine, continuously blended in
    // between. lowerShapeRegime (shapeAmount <= shapeBreakpoint) is block-constant - the caller
    // hoists it once per block so this only computes the two waveforms the blend actually needs,
    // instead of always computing all three and discarding one.
    float lfoValueForPhase(float p, float shapeAmount, bool lowerShapeRegime) noexcept
    {
        const auto tri = triangleWave(p);
        if (lowerShapeRegime)
            return juce::jmap(shapeAmount, 0.0f, shapeBreakpoint, squareWave(p), tri);
        return juce::jmap(shapeAmount, shapeBreakpoint, 1.0f, tri, sineWave(p));
    }

    // Factory presets: raw parameter values (the same values setValueNotifyingHost() takes after
    // normalising, not display percentages) applied in one shot when the preset is selected.
    struct FactoryPreset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    const std::vector<FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<FactoryPreset> presets = {
            { "Chug Chug Chug", {
                { FluxAudioProcessor::blendParamID, 54.29999923706055f },
                { FluxAudioProcessor::brightnessParamID, 21.89999961853027f },
                { FluxAudioProcessor::bypassParamID, 0.0f },
                { FluxAudioProcessor::depthParamID, 52.29999923706055f },
                { FluxAudioProcessor::divisionParamID, 11.0f },
                { FluxAudioProcessor::feedbackParamID, 57.10000228881836f },
                { FluxAudioProcessor::gritParamID, 100.0f },
                { FluxAudioProcessor::offsetParamID, -50.20000076293945f },
                { FluxAudioProcessor::rateParamID, 0.1799999922513962f },
                { FluxAudioProcessor::shapeParamID, 76.9000015258789f },
                { FluxAudioProcessor::stagesParamID, 1.0f },
                { FluxAudioProcessor::syncParamID, 0.0f },
            } },
            { "Did I Stutter", {
                { FluxAudioProcessor::blendParamID, 60.0f },
                { FluxAudioProcessor::brightnessParamID, 54.10000228881836f },
                { FluxAudioProcessor::bypassParamID, 0.0f },
                { FluxAudioProcessor::depthParamID, 34.20000076293945f },
                { FluxAudioProcessor::divisionParamID, 8.0f },
                { FluxAudioProcessor::feedbackParamID, 74.5f },
                { FluxAudioProcessor::gritParamID, 70.0f },
                { FluxAudioProcessor::offsetParamID, -62.20000076293945f },
                { FluxAudioProcessor::rateParamID, 0.119999997317791f },
                { FluxAudioProcessor::shapeParamID, 0.0f },
                { FluxAudioProcessor::stagesParamID, 6.0f },
                { FluxAudioProcessor::syncParamID, 1.0f },
            } },
            { "Personal Space", {
                { FluxAudioProcessor::blendParamID, 13.30000019073486f },
                { FluxAudioProcessor::brightnessParamID, 81.4000015258789f },
                { FluxAudioProcessor::bypassParamID, 0.0f },
                { FluxAudioProcessor::depthParamID, 18.70000076293945f },
                { FluxAudioProcessor::divisionParamID, 11.0f },
                { FluxAudioProcessor::feedbackParamID, 20.60000038146973f },
                { FluxAudioProcessor::gritParamID, 0.0f },
                { FluxAudioProcessor::offsetParamID, 80.20000457763672f },
                { FluxAudioProcessor::rateParamID, 4.0f },
                { FluxAudioProcessor::shapeParamID, 56.29999923706055f },
                { FluxAudioProcessor::stagesParamID, 0.0f },
                { FluxAudioProcessor::syncParamID, 1.0f },
            } },
            { "Spun Out", {
                { FluxAudioProcessor::blendParamID, 23.10000038146973f },
                { FluxAudioProcessor::brightnessParamID, 47.90000152587891f },
                { FluxAudioProcessor::bypassParamID, 0.0f },
                { FluxAudioProcessor::depthParamID, 52.29999923706055f },
                { FluxAudioProcessor::divisionParamID, 11.0f },
                { FluxAudioProcessor::feedbackParamID, 12.10000038146973f },
                { FluxAudioProcessor::gritParamID, 15.40000057220459f },
                { FluxAudioProcessor::offsetParamID, -18.49999809265137f },
                { FluxAudioProcessor::rateParamID, 0.1799999922513962f },
                { FluxAudioProcessor::shapeParamID, 76.9000015258789f },
                { FluxAudioProcessor::stagesParamID, 0.0f },
                { FluxAudioProcessor::syncParamID, 0.0f },
            } },
            { "We Have Liftoff", {
                { FluxAudioProcessor::blendParamID, 40.70000076293945f },
                { FluxAudioProcessor::brightnessParamID, 36.5f },
                { FluxAudioProcessor::bypassParamID, 0.0f },
                { FluxAudioProcessor::depthParamID, 74.30000305175781f },
                { FluxAudioProcessor::divisionParamID, 11.0f },
                { FluxAudioProcessor::feedbackParamID, 18.20000076293945f },
                { FluxAudioProcessor::gritParamID, 6.300000190734863f },
                { FluxAudioProcessor::offsetParamID, 2.600001573562622f },
                { FluxAudioProcessor::rateParamID, 0.2199999988079071f },
                { FluxAudioProcessor::shapeParamID, 100.0f },
                { FluxAudioProcessor::stagesParamID, 3.0f },
                { FluxAudioProcessor::syncParamID, 0.0f },
            } },
            { "What's Wrong", {
                { FluxAudioProcessor::blendParamID, 82.9000015258789f },
                { FluxAudioProcessor::brightnessParamID, 83.5f },
                { FluxAudioProcessor::bypassParamID, 0.0f },
                { FluxAudioProcessor::depthParamID, 61.5f },
                { FluxAudioProcessor::divisionParamID, 5.0f },
                { FluxAudioProcessor::feedbackParamID, 42.5f },
                { FluxAudioProcessor::gritParamID, 9.90000057220459f },
                { FluxAudioProcessor::offsetParamID, -29.69999885559082f },
                { FluxAudioProcessor::rateParamID, 0.3999999761581421f },
                { FluxAudioProcessor::shapeParamID, 78.80000305175781f },
                { FluxAudioProcessor::stagesParamID, 5.0f },
                { FluxAudioProcessor::syncParamID, 0.0f },
            } },
        };

        return presets;
    }
}

FluxAudioProcessor::FluxAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    bypassParam = apvts.getRawParameterValue(bypassParamID);
    rateParam = apvts.getRawParameterValue(rateParamID);
    syncParam = apvts.getRawParameterValue(syncParamID);
    divisionParam = apvts.getRawParameterValue(divisionParamID);
    depthParam = apvts.getRawParameterValue(depthParamID);
    shapeParam = apvts.getRawParameterValue(shapeParamID);
    stagesParam = apvts.getRawParameterValue(stagesParamID);
    offsetParam = apvts.getRawParameterValue(offsetParamID);
    feedbackParam = apvts.getRawParameterValue(feedbackParamID);
    brightnessParam = apvts.getRawParameterValue(brightnessParamID);
    gritParam = apvts.getRawParameterValue(gritParamID);
    blendParam = apvts.getRawParameterValue(blendParamID);
}

FluxAudioProcessor::~FluxAudioProcessor() = default;

const juce::StringArray& FluxAudioProcessor::getDivisionChoices()
{
    static const juce::StringArray choices = [] {
        juce::StringArray result;
        for (auto& subdivision : subdivisions)
            result.add(subdivision.label);
        return result;
    }();

    return choices;
}

const juce::StringArray& FluxAudioProcessor::getStageChoices()
{
    static const juce::StringArray choices = [] {
        juce::StringArray result;
        for (auto count : stageCounts)
            result.add(juce::String(count));
        return result;
    }();

    return choices;
}

int FluxAudioProcessor::getStageCountForChoiceIndex(int index)
{
    const auto clamped = juce::jlimit(0, (int) std::size(stageCounts) - 1, index);
    return stageCounts[(size_t) clamped];
}

juce::AudioProcessorValueTreeState::ParameterLayout FluxAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1},
        "Bypass",
        false));

    {
        const juce::NormalisableRange<float> rateRange(0.02f, 10.0f, 0.01f, 0.3f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{rateParamID, 1},
            "Rate",
            rateRange,
            0.5f,
            juce::AudioParameterFloatAttributes()
                .withLabel("Hz")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " Hz"; })));
    }

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{syncParamID, 1},
        "Sync",
        false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{divisionParamID, 1},
        "Division",
        getDivisionChoices(),
        defaultDivisionIndex));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{depthParamID, 1},
        "Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        60.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{shapeParamID, 1},
        "Shape",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        // A plain percentage, not "Square"/"Triangle"/"Sine" category labels - the LFO shape is a
        // continuous crossfade between the three (see lfoValueForPhase() below), and showing wide
        // flat bands of the same word (e.g. "Triangle" for the entire 25-75% range) made the knob
        // read as a 3-way selector even though the underlying waveform blends the whole way through.
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{stagesParamID, 1},
        "Stages",
        getStageChoices(),
        defaultStageChoiceIndex));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{offsetParamID, 1},
        "Offset",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{feedbackParamID, 1},
        "Feedback",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        30.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{brightnessParamID, 1},
        "Brightness",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{gritParamID, 1},
        "Grit",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    // A single crossfade knob rather than independent Dry/Wet gains: 0% is fully dry, 100% is
    // fully wet. Defaults to 35%, not the 50% centre - unlike a delay or reverb, an allpass
    // filter alone doesn't change magnitude response at all, so the notches that make a phaser
    // audible only appear where the wet (phase-shifted) signal cancels against the dry signal;
    // leaning dry out of the box keeps that cancellation clearly audible rather than washing it
    // out under a too-strong wet signal.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{blendParamID, 1},
        "Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        35.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    return {params.begin(), params.end()};
}

void FluxAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 1;

    for (auto& stage : allpassL) stage.reset();
    for (auto& stage : allpassR) stage.reset();

    feedbackStateL = feedbackStateR = 0.0f;
    lfoPhase = 0.0;
    lastGritAmount = lastBrightnessAmount = -1.0f;

    brightnessFilterL.prepare(spec);
    brightnessFilterR.prepare(spec);
    brightnessFilterL.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, brightnessMaxHz);
    brightnessFilterR.coefficients = brightnessFilterL.coefficients;
    brightnessFilterL.reset();
    brightnessFilterR.reset();

    gritEqFilterL.prepare(spec);
    gritEqFilterR.prepare(spec);
    gritEqFilterL.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, gritEqHz, gritEqQ, 1.0f);
    gritEqFilterR.coefficients = gritEqFilterL.coefficients;
    gritEqFilterL.reset();
    gritEqFilterR.reset();
}

void FluxAudioProcessor::releaseResources() {}

double FluxAudioProcessor::getCurrentBpm() const
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

bool FluxAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void FluxAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    const auto numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2)
        return;

    const auto syncOn = syncParam->load() > 0.5f;

    double periodSeconds;
    if (syncOn)
    {
        const auto index = juce::jlimit(0, (int) std::size(subdivisions) - 1, (int) divisionParam->load());
        const auto quarterNoteSeconds = 60.0 / getCurrentBpm();
        periodSeconds = quarterNoteSeconds * subdivisions[(size_t) index].quarterNoteMultiple;
    }
    else
    {
        const auto rateHz = (double) rateParam->load();
        periodSeconds = rateHz > 0.0 ? 1.0 / rateHz : 1.0;
    }
    const auto lfoHz = periodSeconds > 0.0 ? 1.0 / periodSeconds : 0.0;
    const auto phaseIncrement = lfoHz / sampleRateHz;
    currentLfoRateHz.store((float) lfoHz, std::memory_order_relaxed);

    const auto shapeAmount = shapeParam->load() * 0.01f;

    const auto offsetAmount = offsetParam->load() * 0.01f;
    const auto depthAmount = depthParam->load() * 0.01f;
    const auto centerLog2 = log2NominalCenterHz + offsetAmount * offsetOctaveRange;
    const auto depthOctaves = depthAmount * depthOctaveRange;

    const auto activeStages = juce::jlimit(0, maxStages,
        getStageCountForChoiceIndex((int) stagesParam->load()));
    for (int s = activeStages; s < maxStages; ++s)
    {
        allpassL[(size_t) s].reset();
        allpassR[(size_t) s].reset();
    }

    const auto feedbackAmount = feedbackParam->load() * 0.01f;
    const auto feedbackActive = feedbackAmount > 0.0f;

    const auto gritAmount = gritParam->load() * 0.01f;
    if (std::abs(gritAmount - lastGritAmount) > 0.0f)
    {
        lastGritAmount = gritAmount;
        gritK = gritAmount * gritDriveRange;
        gritMakeup = 1.0f + gritAmount * gritMakeupRange;
        gritOutputTrim = juce::Decibels::decibelsToGain(gritAmount * gritOutputTrimMaxDb);

        const auto gritEqGainDb = gritAmount * gritEqMaxGainDb;
        const auto gritEqGainLinear = juce::Decibels::decibelsToGain(gritEqGainDb);
        gritEqFilterL.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRateHz, gritEqHz, gritEqQ, gritEqGainLinear);
        gritEqFilterR.coefficients = gritEqFilterL.coefficients;
    }

    const auto brightnessAmount = brightnessParam->load() * 0.01f;
    if (std::abs(brightnessAmount - lastBrightnessAmount) > 0.0f)
    {
        lastBrightnessAmount = brightnessAmount;
        const auto brightnessLog = juce::jmap(brightnessAmount, 0.0f, 1.0f,
                                               log2BrightnessMinHz, log2BrightnessMaxHz);
        const auto brightnessHz = juce::jmin(std::pow(2.0f, brightnessLog), (float) (sampleRateHz * 0.49));
        brightnessFilterL.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRateHz, brightnessHz);
        brightnessFilterR.coefficients = brightnessFilterL.coefficients;
    }

    // Equal-power crossfade (sin/cos, not a plain linear 1-x/x split) so the blend doesn't dip in
    // perceived loudness at the centre - dry and wet gain both sit at ~0.707 (not 0.5) when Blend
    // is at 50%, which is what keeps that midpoint sounding as loud as either extreme.
    const auto blendRadians = blendParam->load() * 0.01f * juce::MathConstants<float>::halfPi;
    const auto dryGain = std::cos(blendRadians);
    const auto wetGain = std::sin(blendRadians);

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    const auto lowerShapeRegime = shapeAmount <= shapeBreakpoint;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto lfoValue = lfoValueForPhase((float) lfoPhase, shapeAmount, lowerShapeRegime);
        lfoPhase += phaseIncrement;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;

        const auto sweepLog2 = centerLog2 + depthOctaves * lfoValue;

        // Widening only engages through the upper half of the LFO cycle (lfoValue > 0) - see
        // stereoWidthMaxOctaves above.
        const auto stereoSpread = juce::jmax(0.0f, lfoValue) * stereoWidthMaxOctaves;

        const auto freqHzL = juce::jlimit(minAllpassHz, maxAllpassHz, std::pow(2.0f, sweepLog2 + stereoSpread));
        const auto freqHzR = juce::jlimit(minAllpassHz, maxAllpassHz, std::pow(2.0f, sweepLog2 - stereoSpread));
        const auto coeffL = coefficientForFrequency(freqHzL, sampleRateHz);
        // stereoSpread == 0.0f (roughly half of every LFO cycle) makes freqHzR bit-identical to
        // freqHzL (adding/subtracting exact zero never changes a finite float), so coeffR would
        // come out bit-identical too - skip the redundant tan()-bearing call in that case.
        const auto coeffR = stereoSpread > 0.0f ? coefficientForFrequency(freqHzR, sampleRateHz) : coeffL;

        for (int s = 0; s < activeStages; ++s)
        {
            allpassL[(size_t) s].setCoefficient(coeffL);
            allpassR[(size_t) s].setCoefficient(coeffR);
        }

        const auto inL = left[sample];
        const auto inR = right[sample];

        // Feedback tap: the allpass chain's own previous output, safety-saturated so the loop
        // can never actually run away regardless of the Feedback knob (see feedbackSafetyDrive).
        // At feedbackAmount == 0 the tanh() result is multiplied by exact zero below regardless of
        // its value, so it's skipped entirely rather than computed and discarded.
        const auto feedbackTapL = feedbackActive ? std::tanh(feedbackStateL * feedbackSafetyDrive) / feedbackSafetyDrive : 0.0f;
        const auto feedbackTapR = feedbackActive ? std::tanh(feedbackStateR * feedbackSafetyDrive) / feedbackSafetyDrive : 0.0f;

        auto wetL = inL + feedbackAmount * feedbackTapL;
        auto wetR = inR + feedbackAmount * feedbackTapR;

        for (int s = 0; s < activeStages; ++s)
        {
            wetL = allpassL[(size_t) s].processSample(wetL);
            wetR = allpassR[(size_t) s].processSample(wetR);
        }

        // Feedback is tapped here, from the clean phaser core - not after Grit/Brightness below -
        // so those cosmetic tone controls can't compound into the resonant loop itself.
        feedbackStateL = wetL;
        feedbackStateR = wetR;

        wetL = gritK > 0.0f ? (std::tanh(wetL * gritK) / gritK) * gritMakeup : wetL;
        wetR = gritK > 0.0f ? (std::tanh(wetR * gritK) / gritK) * gritMakeup : wetR;

        wetL *= gritOutputTrim;
        wetR *= gritOutputTrim;

        wetL = gritEqFilterL.processSample(wetL);
        wetR = gritEqFilterR.processSample(wetR);

        wetL = brightnessFilterL.processSample(wetL);
        wetR = brightnessFilterR.processSample(wetR);

        left[sample] = inL * dryGain + wetL * wetGain;
        right[sample] = inR * dryGain + wetR * wetGain;
    }
}

// createEditor() lives in PluginEditor.cpp (not here) specifically so this file has no
// PluginEditor.h/GUI dependency - FluxTests links only this file plus juce_audio_processors/juce_dsp.

bool FluxAudioProcessor::hasEditor() const { return true; }

const juce::String FluxAudioProcessor::getName() const { return JucePlugin_Name; }

bool FluxAudioProcessor::acceptsMidi() const { return false; }
bool FluxAudioProcessor::producesMidi() const { return false; }
bool FluxAudioProcessor::isMidiEffect() const { return false; }
double FluxAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int FluxAudioProcessor::getNumPrograms() { return (int) getFactoryPresets().size(); }
int FluxAudioProcessor::getCurrentProgram() { return currentProgramIndex; }

void FluxAudioProcessor::setCurrentProgram(int index)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow(index, (int) presets.size()))
        return;

    currentProgramIndex = index;

    for (auto& [paramID, value] : presets[(size_t) index].values)
        if (auto* param = apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(value));
}

const juce::String FluxAudioProcessor::getProgramName(int index)
{
    const auto& presets = getFactoryPresets();
    return juce::isPositiveAndBelow(index, (int) presets.size()) ? presets[(size_t) index].name : juce::String();
}

void FluxAudioProcessor::changeProgramName(int, const juce::String&) {}

void FluxAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void FluxAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FluxAudioProcessor();
}
