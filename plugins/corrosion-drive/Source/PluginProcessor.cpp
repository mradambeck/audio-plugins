#include "PluginProcessor.h"

namespace
{
    constexpr float dcBlockerFrequency = 20.0f;

    constexpr float colorDipFrequency = 286.0f;
    constexpr float colorDipQ = 0.71f;
    constexpr float colorDipMaxCutDb = -7.0f;

    constexpr float colorPresenceFrequency = 5300.0f;
    constexpr float colorPresenceQ = 0.57f;
    constexpr float colorPresenceMaxBoostDb = 2.8f;

    constexpr float colorMidPeakFrequency = 4320.0f;
    constexpr float colorMidPeakQ = 0.41f;
    constexpr float colorMidPeakMaxBoostDb = 3.0f;

    constexpr float colorLowDipFrequency = 186.0f;
    constexpr float colorLowDipQ = 0.6f;
    constexpr float colorLowDipMaxCutDb = -3.0f;

    constexpr float colorHighShelfFrequency = 4000.0f;
    constexpr float colorHighShelfQ = 0.4f;
    constexpr float colorHighShelfMaxBoostDb = 6.5f;

    // Broadband makeup gain tied to Color, on top of the peaking bands above -- Color's leftmost
    // (darkest) position loses a lot of perceived loudness to the lowpass cutting the highs, so
    // this compensates with up to +7dB there, fading to 0dB (no effect) at the rightmost.
    constexpr float colorMakeupMaxBoostDb = 7.0f;

    // Rectification (half/full-wave) reads as noticeably quieter than the pre-Rect signal at
    // the same peak level, so turning Rect Blend up feels like a volume drop rather than a
    // timbre change. Makeup gain applied to the rectified side only, before the blend, so 0%
    // (fully pre-Rect) is untouched and only the rectified portion gets louder.
    constexpr float rectMakeupDb = 4.0f;

    // Classic slapback territory (a single short repeat, no feedback) -- kept fixed rather than
    // user-controllable since this is meant to ride along with Color, not be a separate delay.
    constexpr float slapbackDelayMs = 120.0f;
    constexpr float slapbackMaxMix = 0.198f;  // subtle -- ~10% quieter than 0.22 at Color's leftmost

    // Character blends the waveshaper between plain tanh (soft) and this sharper-kneed variant
    // (hard) -- tanh(driven * hardness) still saturates to the same +/-1 ceiling as plain tanh,
    // but reaches it over a narrower input range, so more of the waveform sits near-flat. That's
    // a cheap stand-in for the harder knee a silicon/LED diode clipper has versus a soft one,
    // without modelling an actual diode I-V curve.
    constexpr float characterHardnessMultiplier = 3.5f;

    // Textbook general-purpose compressor settings (not a bespoke curation) -- the Comp knob's
    // own 0-100% blend is what controls how much of this is actually heard, so the compressor
    // itself just needs to be a sensible, universally-applicable default.
    constexpr float dryCompThresholdDb = -20.0f;
    constexpr float dryCompRatio = 4.0f;
    constexpr float dryCompAttackMs = 10.0f;
    constexpr float dryCompReleaseMs = 100.0f;

    // Factory presets: raw parameter values (the same values setValueNotifyingHost() takes after
    // normalising, not display percentages) applied in one shot when the preset is selected.
    const std::vector<wildjag::FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<wildjag::FactoryPreset> presets = {
            { "Color Me Radd", {
                { CorrosionAudioProcessor::biasParamID, -0.3459999561309814f },
                { CorrosionAudioProcessor::bypassParamID, 0.0f },
                { CorrosionAudioProcessor::characterParamID, 0.6760000586509705f },
                { CorrosionAudioProcessor::compParamID, 0.203000009059906f },
                { CorrosionAudioProcessor::driveParamID, 2.920000076293945f },
                { CorrosionAudioProcessor::dryParamID, -2.235174179077148e-06f },
                { CorrosionAudioProcessor::outputParamID, -15.07000160217285f },
                { CorrosionAudioProcessor::rectBlendParamID, 0.7500000596046448f },
                { CorrosionAudioProcessor::rectMixParamID, 0.07700000703334808f },
                { CorrosionAudioProcessor::toneParamID, 2648.0f },
            } },
            { "Deja Vu", {
                { CorrosionAudioProcessor::biasParamID, -0.2349999696016312f },
                { CorrosionAudioProcessor::bypassParamID, 0.0f },
                { CorrosionAudioProcessor::characterParamID, 0.8830000162124634f },
                { CorrosionAudioProcessor::compParamID, 0.9780000448226929f },
                { CorrosionAudioProcessor::driveParamID, 48.27000045776367f },
                { CorrosionAudioProcessor::dryParamID, -12.5200023651123f },
                { CorrosionAudioProcessor::outputParamID, -26.29000091552734f },
                { CorrosionAudioProcessor::rectBlendParamID, 0.8730000257492065f },
                { CorrosionAudioProcessor::rectMixParamID, 0.6690000295639038f },
                { CorrosionAudioProcessor::toneParamID, 5579.0f },
            } },
            { "How So", {
                { CorrosionAudioProcessor::biasParamID, -0.2409999668598175f },
                { CorrosionAudioProcessor::bypassParamID, 0.0f },
                { CorrosionAudioProcessor::characterParamID, 0.331000030040741f },
                { CorrosionAudioProcessor::compParamID, 0.7440000176429749f },
                { CorrosionAudioProcessor::driveParamID, 27.96999931335449f },
                { CorrosionAudioProcessor::dryParamID, -6.210001945495605f },
                { CorrosionAudioProcessor::outputParamID, -27.17000198364258f },
                { CorrosionAudioProcessor::rectBlendParamID, 0.6440000534057617f },
                { CorrosionAudioProcessor::rectMixParamID, 0.8560000658035278f },
                { CorrosionAudioProcessor::toneParamID, 5248.0f },
            } },
            { "LoJack", {
                { CorrosionAudioProcessor::biasParamID, -0.2379999607801437f },
                { CorrosionAudioProcessor::bypassParamID, 0.0f },
                { CorrosionAudioProcessor::characterParamID, 0.0f },
                { CorrosionAudioProcessor::compParamID, 0.7240000367164612f },
                { CorrosionAudioProcessor::driveParamID, 11.47999954223633f },
                { CorrosionAudioProcessor::dryParamID, -2.235174179077148e-06f },
                { CorrosionAudioProcessor::outputParamID, -22.38000106811523f },
                { CorrosionAudioProcessor::rectBlendParamID, 0.8570000529289246f },
                { CorrosionAudioProcessor::rectMixParamID, 0.906000018119812f },
                { CorrosionAudioProcessor::toneParamID, 921.0f },
            } },
            { "Vintage Modern", {
                { CorrosionAudioProcessor::biasParamID, -0.6419999599456787f },
                { CorrosionAudioProcessor::bypassParamID, 0.0f },
                { CorrosionAudioProcessor::characterParamID, 0.690000057220459f },
                { CorrosionAudioProcessor::compParamID, 0.1830000132322311f },
                { CorrosionAudioProcessor::driveParamID, 26.85999870300293f },
                { CorrosionAudioProcessor::dryParamID, -2.235174179077148e-06f },
                { CorrosionAudioProcessor::outputParamID, -28.10000228881836f },
                { CorrosionAudioProcessor::rectBlendParamID, 0.6530000567436218f },
                { CorrosionAudioProcessor::rectMixParamID, 0.3190000057220459f },
                { CorrosionAudioProcessor::toneParamID, 3733.0f },
            } },
            { "We're Doomed", {
                { CorrosionAudioProcessor::biasParamID, -0.4439999759197235f },
                { CorrosionAudioProcessor::bypassParamID, 0.0f },
                { CorrosionAudioProcessor::characterParamID, 1.0f },
                { CorrosionAudioProcessor::compParamID, 0.2900000214576721f },
                { CorrosionAudioProcessor::driveParamID, 19.43000030517578f },
                { CorrosionAudioProcessor::dryParamID, -13.11000156402588f },
                { CorrosionAudioProcessor::outputParamID, -21.92000198364258f },
                { CorrosionAudioProcessor::rectBlendParamID, 0.3800000250339508f },
                { CorrosionAudioProcessor::rectMixParamID, 0.9190000295639038f },
                { CorrosionAudioProcessor::toneParamID, 6596.0f },
            } },
        };

        return presets;
    }
}

CorrosionAudioProcessor::CorrosionAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets())
{
    driveParam = apvts.getRawParameterValue(driveParamID);
    toneParam = apvts.getRawParameterValue(toneParamID);
    toneRangedParam = apvts.getParameter(toneParamID);
    biasParam = apvts.getRawParameterValue(biasParamID);
    characterParam = apvts.getRawParameterValue(characterParamID);
    outputParam = apvts.getRawParameterValue(outputParamID);
    dryParam = apvts.getRawParameterValue(dryParamID);
    compParam = apvts.getRawParameterValue(compParamID);
    bypassParam = apvts.getRawParameterValue(bypassParamID);
    rectBlendParam = apvts.getRawParameterValue(rectBlendParamID);
    rectMixParam = apvts.getRawParameterValue(rectMixParamID);
}

CorrosionAudioProcessor::~CorrosionAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout CorrosionAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{driveParamID, 1},
        "Drive",
        juce::NormalisableRange<float>(1.0f, 75.0f, 0.01f, 0.4f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("x")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{toneParamID, 1},
        "Tone",
        juce::NormalisableRange<float>(200.0f, 20000.0f, 1.0f, 0.3f),
        9000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{biasParamID, 1},
        "Bias",
        // A small asymmetric offset added after the drive gain, before waveshaping. It skews
        // the transfer curve so positive and negative half-cycles clip differently, generating
        // even-order harmonics (2nd harmonic especially) for a warmer, more tube-like character
        // than symmetric clipping alone. The resulting DC offset is removed by dcBlocker.
        // Range doubled (was -0.5/0.5, default 0.12) so the same knob rotation produces twice
        // the asymmetry -- the effect was too subtle to hear across the original range.
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f),
        0.24f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{characterParamID, 1},
        "Character",
        // 0 = plain tanh (soft), 1 = the sharper-kneed tanh variant (hard) -- see
        // characterHardnessMultiplier's comment for why this stands in for a diode-pair clipper
        // without modelling one directly. Defaults to 0 so existing presets/behaviour don't change.
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{outputParamID, 1},
        "Wet",
        // Skew > 1 biases resolution toward the top of the range (JUCE's
        // convertFrom0to1 is start + range * proportion^(1/skew), so skew=3 means most of
        // the knob's rotation is spent between -50dB and +12dB, the audible range, while
        // -100 to -50dB -- inaudible anyway -- is compressed into a small arc near the start.
        juce::NormalisableRange<float>(-100.0f, 12.0f, 0.01f, 3.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{dryParamID, 1},
        "Dry",
        // See the Wet parameter above for why skew is 3.0, not <1.
        juce::NormalisableRange<float>(-100.0f, 0.0f, 0.01f, 3.0f),
        -100.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{compParamID, 1},
        "Comp",
        // 0 = fully uncompressed dry signal, 1 = fully compressed -- only ever blends the
        // parallel copy the Dry knob feeds back in, never the copy that continues into the
        // drive stage. See dryCompressor's comment in PluginProcessor.h and the fixed settings
        // above prepareToPlay() in this file.
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1},
        "Bypass",
        false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{rectBlendParamID, 1},
        "Rect Blend",
        // 0 = half-wave rectification (max(x, 0)), 1 = full-wave (|x|) -- see rectDcBlocker
        // comment in PluginProcessor.h for why this stage needs its own DC blocker.
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{rectMixParamID, 1},
        "Rect Mix",
        // How much of the rectified signal is blended back against the pre-Rect signal -- 0 is
        // fully pre-Rect (Rect inaudible), 1 is fully rectified. This is the sole on/off control
        // for the Rect stage now (there's no separate enable button), so it defaults to 0.
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    return {params.begin(), params.end()};
}

void CorrosionAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    toneFilter.prepare(spec);
    colorDipFilter.prepare(spec);
    colorPresenceFilter.prepare(spec);
    colorMidPeakFilter.prepare(spec);
    colorLowDipFilter.prepare(spec);
    colorHighShelfFilter.prepare(spec);
    updateToneFilter();

    dcBlocker.prepare(spec);
    *dcBlocker.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, dcBlockerFrequency);

    rectDcBlocker.prepare(spec);
    *rectDcBlocker.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, dcBlockerFrequency);

    dryBuffer.setSize(static_cast<int>(spec.numChannels), samplesPerBlock);
    rectDryBuffer.setSize(static_cast<int>(spec.numChannels), samplesPerBlock);
    compDryBuffer.setSize(static_cast<int>(spec.numChannels), samplesPerBlock);

    dryCompressor.prepare(spec);
    dryCompressor.setThreshold(dryCompThresholdDb);
    dryCompressor.setRatio(dryCompRatio);
    dryCompressor.setAttack(dryCompAttackMs);
    dryCompressor.setRelease(dryCompReleaseMs);

    // Fixed delay time, but sized with headroom above it rather than exactly to it, since an
    // exact-fit circular buffer leaves no slack for the read/write pointers' rounding.
    const auto maxDelaySamples = (int) std::ceil(0.25 * sampleRate);
    slapbackDelay.setMaximumDelayInSamples(maxDelaySamples);
    slapbackDelay.prepare(spec);
    slapbackDelay.setDelay((float) (slapbackDelayMs * 0.001 * sampleRate));
    slapbackDelay.reset();
}

void CorrosionAudioProcessor::releaseResources() {}

void CorrosionAudioProcessor::updateToneFilter()
{
    const auto sampleRate = getSampleRate();
    if (sampleRate <= 0.0)
        return;

    const auto nyquist = static_cast<float>(sampleRate * 0.49);
    const auto toneValue = toneParam->load();
    const auto cutoff = juce::jmin(toneValue, nyquist);

    *toneFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, cutoff);

    // Color's knob rotation (0 = fully left/darkest, 1 = fully right/brightest), read through its
    // own NormalisableRange so this tracks the knob's actual skewed position rather than a plain
    // linear reading of the underlying Hz value. Drives the companion EQ dip's gain: full -7dB
    // cut at the leftmost position, fading linearly to 0dB (no effect) at the rightmost.
    const auto toneProportion = toneRangedParam->getNormalisableRange().convertTo0to1(toneValue);
    const auto dipGainDb = juce::jmap(toneProportion, 0.0f, 1.0f, colorDipMaxCutDb, 0.0f);

    *colorDipFilter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, colorDipFrequency, colorDipQ, juce::Decibels::decibelsToGain(dipGainDb));

    // Same knob reading, opposite direction: 0dB (no effect) at the leftmost, rising to +2.8dB
    // of presence boost at the rightmost.
    const auto presenceGainDb = juce::jmap(toneProportion, 0.0f, 1.0f, 0.0f, colorPresenceMaxBoostDb);

    *colorPresenceFilter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, colorPresenceFrequency, colorPresenceQ, juce::Decibels::decibelsToGain(presenceGainDb));

    // Same direction as colorDipFilter this time: full +3dB boost at the leftmost, fading to 0dB
    // (no effect) at the rightmost.
    const auto midPeakGainDb = juce::jmap(toneProportion, 0.0f, 1.0f, colorMidPeakMaxBoostDb, 0.0f);

    *colorMidPeakFilter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, colorMidPeakFrequency, colorMidPeakQ, juce::Decibels::decibelsToGain(midPeakGainDb));

    // Same direction as colorDipFilter and colorMidPeakFilter: full -3dB cut at the leftmost,
    // fading to 0dB (no effect) at the rightmost.
    const auto lowDipGainDb = juce::jmap(toneProportion, 0.0f, 1.0f, colorLowDipMaxCutDb, 0.0f);

    *colorLowDipFilter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, colorLowDipFrequency, colorLowDipQ, juce::Decibels::decibelsToGain(lowDipGainDb));

    // A high shelf, not a peak like the four bands above -- same direction as colorDipFilter:
    // full +6.5dB boost (of everything above ~4kHz) at the leftmost, fading to 0dB at the
    // rightmost.
    const auto highShelfGainDb = juce::jmap(toneProportion, 0.0f, 1.0f, colorHighShelfMaxBoostDb, 0.0f);

    *colorHighShelfFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, colorHighShelfFrequency, colorHighShelfQ, juce::Decibels::decibelsToGain(highShelfGainDb));
}

bool CorrosionAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void CorrosionAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    const auto drive = driveParam->load();
    const auto bias = biasParam->load();
    const auto character = characterParam->load();
    const auto wetGain = juce::Decibels::decibelsToGain(outputParam->load());
    const auto dryGain = juce::Decibels::decibelsToGain(dryParam->load());
    const auto compBlend = compParam->load();
    const auto rectBlend = rectBlendParam->load();
    const auto rectMix = rectMixParam->load();

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();

    for (int channel = 0; channel < numChannels; ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, numSamples);

    // Comp blends dryBuffer between its plain and compressed forms -- `buffer`, which continues
    // into the drive stage below, is completely untouched by any of this.
    for (int channel = 0; channel < numChannels; ++channel)
        compDryBuffer.copyFrom(channel, 0, dryBuffer, channel, 0, numSamples);

    {
        auto dryBlock = juce::dsp::AudioBlock<float>(dryBuffer).getSubBlock(0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> dryContext(dryBlock);
        dryCompressor.process(dryContext);
    }

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* dryData = dryBuffer.getWritePointer(channel);
        auto* preCompData = compDryBuffer.getWritePointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
            dryData[sample] = preCompData[sample] + (dryData[sample] - preCompData[sample]) * compBlend;
    }

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto driven = channelData[sample] * drive + bias;
            const auto soft = std::tanh(driven);
            const auto hard = std::tanh(driven * characterHardnessMultiplier);
            channelData[sample] = soft + (hard - soft) * character;
        }
    }

    updateToneFilter();

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    dcBlocker.process(context);
    toneFilter.process(context);
    colorDipFilter.process(context);
    colorPresenceFilter.process(context);
    colorMidPeakFilter.process(context);
    colorLowDipFilter.process(context);
    colorHighShelfFilter.process(context);

    // Same knob-rotation reading used throughout the Color-tied stages above and below.
    const auto toneProportion = toneRangedParam->getNormalisableRange().convertTo0to1(toneParam->load());

    // Broadband makeup gain, on top of the peaking bands above -- full +7dB at Color's leftmost,
    // fading to 0dB (no effect) at its rightmost.
    const auto colorMakeupDb = juce::jmap(toneProportion, 0.0f, 1.0f, colorMakeupMaxBoostDb, 0.0f);
    buffer.applyGain(juce::Decibels::decibelsToGain(colorMakeupDb));

    // Color-driven slapback: full (but subtle) mix at Color's leftmost/darkest position, fading
    // to none at its rightmost.
    const auto slapbackMix = juce::jmap(toneProportion, 0.0f, 1.0f, slapbackMaxMix, 0.0f);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto input = channelData[sample];
            // Always fed the raw input, never the echoed/mixed output -- a repeat of a repeat
            // would turn this into a feedback trail instead of a single slap.
            slapbackDelay.pushSample(channel, input);
            const auto delayed = slapbackDelay.popSample(channel);
            channelData[sample] = input + delayed * slapbackMix;
        }
    }

    // Rect has no separate enable button -- Rect Mix (0 = inaudible, 1 = fully rectified) is
    // itself the on/off control, so this stage always runs and lets that knob gate it.
    for (int channel = 0; channel < numChannels; ++channel)
        rectDryBuffer.copyFrom(channel, 0, buffer, channel, 0, numSamples);

    // Blends continuously between half-wave (max(x, 0)) and full-wave (|x|) rectification --
    // 0 is fully half-wave, 1 is fully full-wave. Both shapes push the signal's average well
    // above zero, so this stage needs its own DC blocker, separate from the drive stage's.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto x = channelData[sample];
            const auto halfWave = juce::jmax(x, 0.0f);
            const auto fullWave = std::abs(x);
            channelData[sample] = halfWave + (fullWave - halfWave) * rectBlend;
        }
    }

    rectDcBlocker.process(context);
    buffer.applyGain(juce::Decibels::decibelsToGain(rectMakeupDb));

    // Rect Mix blends the rectified result back against the signal from just before this
    // stage -- independent of the Dry/Wet knobs in Output, which blend against the signal
    // from before the drive stage, much earlier in the chain.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        auto* preRectData = rectDryBuffer.getWritePointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
            channelData[sample] = preRectData[sample] + (channelData[sample] - preRectData[sample]) * rectMix;
    }

    buffer.applyGain(wetGain);

    for (int channel = 0; channel < numChannels; ++channel)
        buffer.addFrom(channel, 0, dryBuffer, channel, 0, numSamples, dryGain);
}

// createEditor() lives in PluginEditor.cpp (not here) specifically so this file has no
// PluginEditor.h/GUI dependency - CorrosionTests links only this file plus juce_audio_processors/juce_dsp.

bool CorrosionAudioProcessor::hasEditor() const { return true; }

const juce::String CorrosionAudioProcessor::getName() const { return JucePlugin_Name; }

bool CorrosionAudioProcessor::acceptsMidi() const { return false; }
bool CorrosionAudioProcessor::producesMidi() const { return false; }
bool CorrosionAudioProcessor::isMidiEffect() const { return false; }
double CorrosionAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int CorrosionAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int CorrosionAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void CorrosionAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String CorrosionAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }

void CorrosionAudioProcessor::changeProgramName(int, const juce::String&) {}

void CorrosionAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void CorrosionAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CorrosionAudioProcessor();
}
