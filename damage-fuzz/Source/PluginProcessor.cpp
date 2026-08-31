#include "PluginProcessor.h"

namespace
{
    // A hard two-level comparator: whichever side of thresholdOffset the driven signal falls
    // on becomes a full +1/-1 rail, so the output is a pure pulse/square wave. Since the
    // threshold sits off-centre rather than at zero, the two rails are held for unequal
    // portions of the cycle - that's the duty cycle (pulse width) control.
    float squareFuzz(float driven, float thresholdOffset)
    {
        return driven > thresholdOffset ? 1.0f : -1.0f;
    }

    // A pure comparator alone sounds like a synth, not a fuzz pedal: it throws away every trace
    // of the input's envelope and harmonic content above the fundamental. Blending in a softly
    // saturated copy of the same driven signal keeps some of that pick attack and body underneath
    // the pulse, while the comparator still supplies the reedy, hollow PWM character on top.
    float pulseFuzz(float driven, float thresholdOffset)
    {
        constexpr auto pulseMix = 0.6f;
        constexpr auto bodyMix = 0.4f;

        const auto pulse = squareFuzz(driven, thresholdOffset);
        const auto body = std::tanh(driven);

        return pulse * pulseMix + body * bodyMix;
    }

    constexpr float dcBlockerFrequency = 20.0f;

    constexpr float highPassDipFrequency = 286.0f;
    constexpr float highPassDipQ = 0.25f;
    constexpr float highPassDipMaxCutDb = -3.0f;

    // Shared with createParameterLayout()'s Drive parameter below, and reused in processBlock to
    // normalise the current Drive value for scaling the dry chorus's depth.
    const juce::NormalisableRange<float> driveRange {1.0f, 60.0f, 0.01f, 0.4f};

    // Ballistics for the subtle chorus blended into the dry tap (see dryChorus in
    // PluginProcessor.h). A slow, medium-tempo rate and light depth keep it a gentle sway rather
    // than a seasick warble; mix is scaled live by the Drive knob, up to this ceiling at maximum
    // Drive.
    constexpr float dryChorusRateHz = 1.5f;
    constexpr float dryChorusDepth = 0.25f;
    constexpr float dryChorusCentreDelayMs = 7.0f;
    constexpr float dryChorusMaxMix = 0.15f;

    // Fixed pad applied ahead of the user's Wet trim. The fuzz stage rails hard to +-1, so the
    // wet signal runs far hotter than the dry signal at the same nominal gain - this brings the
    // Wet knob's effective range down to something that better matches Dry loudness.
    constexpr float wetPadDb = -15.0f;

    // How far the oscillator's "off" half-cycle actually pulls the signal down, as a fraction of
    // full level. At 1.0 (a true 0/1 gate) the chop swings the full range, so at audio-rate
    // oscillator frequencies that swing is itself a full-amplitude square wave - loud, and heard
    // as its own note rather than a texture on the input. Leaving a floor here shrinks that swing,
    // so the oscillator's own frequency content stays underneath the original signal instead of
    // replacing it.
    constexpr float oscChopFloor = 0.55f;

    // FM: the oscillator's own frequency is pushed around its "Osc Freq" centre by the input's
    // playing dynamics, rather than sitting at one fixed pitch. This sets how far a fully-driven
    // signal can swing it, as a multiple of the centre frequency.
    constexpr float fmModulationIndex = 4.0f;

    // Gap between the level that opens the gate and the (lower) level that closes it again -
    // without this, an envelope hovering right at a single threshold flickers the gate open
    // and closed at audio rate, which is heard as a buzz.
    constexpr float gateHysteresisDb = 3.0f;

    // The envelope follower's own ballistics: quick to rise so transients aren't missed, slow
    // to fall so the gate doesn't re-close mid-decay.
    constexpr float gateEnvelopeAttackMs = 2.0f;
    constexpr float gateEnvelopeReleaseMs = 150.0f;

    // The gate's applied gain is smoothed separately from the envelope so opening/closing is a
    // fade rather than a step - a hard step re-introduces the same buzz the hysteresis avoids.
    constexpr float gateGainAttackMs = 5.0f;
    constexpr float gateGainReleaseMs = 120.0f;

    // Alternate release ballistics used instead of the pair above when the gate's "Slow Release"
    // button is on, for a long, gentle fade-out rather than the default musical decay.
    constexpr float gateEnvelopeReleaseMsSlow = 600.0f;
    constexpr float gateGainReleaseMsSlow = 500.0f;

    // The oscillator's own gate uses the same hysteresis and attack ballistics as the main gate
    // above, but a much shorter release on both the envelope and the applied gain, so it closes
    // well ahead of the main gate's musical fade-out instead of visibly outlasting the note.
    constexpr float oscGateEnvelopeReleaseMs = 18.0f;
    constexpr float oscGateGainReleaseMs = 10.0f;

    float onePoleCoeff(float timeMs, double sampleRate)
    {
        if (timeMs <= 0.0f || sampleRate <= 0.0)
            return 0.0f;

        return std::exp(-1.0f / (0.001f * timeMs * static_cast<float>(sampleRate)));
    }

    // Factory presets: raw parameter values (the same values setValueNotifyingHost() takes after
    // normalising, not display percentages) applied in one shot when the preset is selected.
    const std::vector<wildjag::FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<wildjag::FactoryPreset> presets = {
            { "Always Leave a Note", {
                { DamageAudioProcessor::bypassParamID, 0.0f },
                { DamageAudioProcessor::driveParamID, 52.14999771118164f },
                { DamageAudioProcessor::dryParamID, 83.5999984741211f },
                { DamageAudioProcessor::gateParamID, -47.0f },
                { DamageAudioProcessor::highPassParamID, 240.0f },
                { DamageAudioProcessor::lowPassParamID, 1381.0f },
                { DamageAudioProcessor::oscFreqParamID, 166.3699951171875f },
                { DamageAudioProcessor::oscillateParamID, 1.0f },
                { DamageAudioProcessor::slowReleaseParamID, 0.0f },
                { DamageAudioProcessor::squareParamID, 0.0f },
                { DamageAudioProcessor::wetParamID, 49.0f },
                { DamageAudioProcessor::widthParamID, 29.39999961853027f },
            } },
            { "Breaking Up Is Hard To Do", {
                { DamageAudioProcessor::bypassParamID, 0.0f },
                { DamageAudioProcessor::driveParamID, 9.139999389648438f },
                { DamageAudioProcessor::dryParamID, 100.0f },
                { DamageAudioProcessor::gateParamID, -34.20000076293945f },
                { DamageAudioProcessor::highPassParamID, 1109.0f },
                { DamageAudioProcessor::lowPassParamID, 5142.0f },
                { DamageAudioProcessor::oscFreqParamID, 125.0199966430664f },
                { DamageAudioProcessor::oscillateParamID, 1.0f },
                { DamageAudioProcessor::slowReleaseParamID, 1.0f },
                { DamageAudioProcessor::squareParamID, 1.0f },
                { DamageAudioProcessor::wetParamID, 14.40000057220459f },
                { DamageAudioProcessor::widthParamID, 63.29999923706055f },
            } },
            { "Old Dogs New Tricks", {
                { DamageAudioProcessor::bypassParamID, 0.0f },
                { DamageAudioProcessor::driveParamID, 21.09000015258789f },
                { DamageAudioProcessor::dryParamID, 58.29999923706055f },
                { DamageAudioProcessor::gateParamID, -47.0f },
                { DamageAudioProcessor::highPassParamID, 28.0f },
                { DamageAudioProcessor::lowPassParamID, 883.0f },
                { DamageAudioProcessor::oscFreqParamID, 2.379999876022339f },
                { DamageAudioProcessor::oscillateParamID, 1.0f },
                { DamageAudioProcessor::slowReleaseParamID, 0.0f },
                { DamageAudioProcessor::squareParamID, 1.0f },
                { DamageAudioProcessor::wetParamID, 27.89999961853027f },
                { DamageAudioProcessor::widthParamID, 29.39999961853027f },
            } },
            { "Oops Didn't Mean To", {
                { DamageAudioProcessor::bypassParamID, 0.0f },
                { DamageAudioProcessor::driveParamID, 3.220000028610229f },
                { DamageAudioProcessor::dryParamID, 72.0999984741211f },
                { DamageAudioProcessor::gateParamID, -43.70000076293945f },
                { DamageAudioProcessor::highPassParamID, 265.0f },
                { DamageAudioProcessor::lowPassParamID, 2475.0f },
                { DamageAudioProcessor::oscFreqParamID, 125.0199966430664f },
                { DamageAudioProcessor::oscillateParamID, 1.0f },
                { DamageAudioProcessor::slowReleaseParamID, 0.0f },
                { DamageAudioProcessor::squareParamID, 0.0f },
                { DamageAudioProcessor::wetParamID, 40.40000152587891f },
                { DamageAudioProcessor::widthParamID, 33.70000076293945f },
            } },
            { "What's Wrong", {
                { DamageAudioProcessor::bypassParamID, 0.0f },
                { DamageAudioProcessor::driveParamID, 2.230000019073486f },
                { DamageAudioProcessor::dryParamID, 45.5f },
                { DamageAudioProcessor::gateParamID, -33.70000076293945f },
                { DamageAudioProcessor::highPassParamID, 115.0f },
                { DamageAudioProcessor::lowPassParamID, 1009.0f },
                { DamageAudioProcessor::oscFreqParamID, 21.17000007629395f },
                { DamageAudioProcessor::oscillateParamID, 1.0f },
                { DamageAudioProcessor::slowReleaseParamID, 0.0f },
                { DamageAudioProcessor::squareParamID, 0.0f },
                { DamageAudioProcessor::wetParamID, 142.6000061035156f },
                { DamageAudioProcessor::widthParamID, 46.79999923706055f },
            } },
        };

        return presets;
    }
}

DamageAudioProcessor::DamageAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets())
{
    gateParam = apvts.getRawParameterValue(gateParamID);
    driveParam = apvts.getRawParameterValue(driveParamID);
    widthParam = apvts.getRawParameterValue(widthParamID);
    highPassParam = apvts.getRawParameterValue(highPassParamID);
    highPassRangedParam = apvts.getParameter(highPassParamID);
    lowPassParam = apvts.getRawParameterValue(lowPassParamID);
    dryParam = apvts.getRawParameterValue(dryParamID);
    bypassParam = apvts.getRawParameterValue(bypassParamID);
    squareParam = apvts.getRawParameterValue(squareParamID);
    oscillateParam = apvts.getRawParameterValue(oscillateParamID);
    oscFreqParam = apvts.getRawParameterValue(oscFreqParamID);
    wetParam = apvts.getRawParameterValue(wetParamID);
    slowReleaseParam = apvts.getRawParameterValue(slowReleaseParamID);
}

DamageAudioProcessor::~DamageAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout DamageAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{gateParamID, 1},
        "Gate",
        juce::NormalisableRange<float>(-80.0f, 0.0f, 0.1f),
        -80.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{driveParamID, 1},
        "Drive",
        driveRange,
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("x")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{widthParamID, 1},
        "Pulse Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{highPassParamID, 1},
        "Hi Pass",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.3f),
        20.0f,   // fully open (no cut) by default
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{lowPassParamID, 1},
        "Lo Pass",
        juce::NormalisableRange<float>(200.0f, 20000.0f, 1.0f, 0.3f),
        6000.0f,   // matches the old single Tone control's default
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{dryParamID, 1},
        "Dry",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1},
        "Bypass",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{squareParamID, 1},
        "Boost",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{oscillateParamID, 1},
        "FM",
        false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{oscFreqParamID, 1},
        "FM Freq",
        juce::NormalisableRange<float>(0.5f, 5000.0f, 0.01f, 0.25f),
        30.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{wetParamID, 1},
        "Wet",
        // Goes past 100% (unity) up to 200%, so the wet signal can be pushed louder than the
        // dry tap itself rather than topping out at matching it.
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{slowReleaseParamID, 1},
        "Slow Release",
        false));

    return {params.begin(), params.end()};
}

void DamageAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    highPassFilter.prepare(spec);
    lowPassFilter.prepare(spec);
    highPassDipFilter.prepare(spec);
    updateFilters();

    dcBlocker.prepare(spec);
    *dcBlocker.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, dcBlockerFrequency);

    dryChorus.prepare(spec);
    dryChorus.setRate(dryChorusRateHz);
    dryChorus.setDepth(dryChorusDepth);
    dryChorus.setCentreDelay(dryChorusCentreDelayMs);
    dryChorus.setFeedback(0.0f);

    gateEnvelopeAttackCoeff = onePoleCoeff(gateEnvelopeAttackMs, sampleRate);
    gateEnvelopeReleaseCoeff = onePoleCoeff(gateEnvelopeReleaseMs, sampleRate);
    gateGainAttackCoeff = onePoleCoeff(gateGainAttackMs, sampleRate);
    gateGainReleaseCoeff = onePoleCoeff(gateGainReleaseMs, sampleRate);

    gateEnvelopeReleaseCoeffSlow = onePoleCoeff(gateEnvelopeReleaseMsSlow, sampleRate);
    gateGainReleaseCoeffSlow = onePoleCoeff(gateGainReleaseMsSlow, sampleRate);

    gateEnvelope = 0.0f;
    gateGain = 1.0f;
    gateOpen = true;

    gateGainBuffer.realloc(static_cast<size_t>(samplesPerBlock));

    oscGateEnvelopeAttackCoeff = onePoleCoeff(gateEnvelopeAttackMs, sampleRate);
    oscGateEnvelopeReleaseCoeff = onePoleCoeff(oscGateEnvelopeReleaseMs, sampleRate);
    oscGateGainAttackCoeff = onePoleCoeff(gateGainAttackMs, sampleRate);
    oscGateGainReleaseCoeff = onePoleCoeff(oscGateGainReleaseMs, sampleRate);

    oscGateEnvelope = 0.0f;
    oscGateGain = 1.0f;
    oscGateOpen = true;

    oscGateBuffer.realloc(static_cast<size_t>(samplesPerBlock));

    oscPhase = 0.0f;
    oscBuffer.realloc(static_cast<size_t>(samplesPerBlock));

    dryBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock, false, false, true);
}

void DamageAudioProcessor::releaseResources() {}

void DamageAudioProcessor::updateFilters()
{
    const auto sampleRate = getSampleRate();
    if (sampleRate <= 0.0)
        return;

    const auto nyquist = static_cast<float>(sampleRate * 0.49);
    const auto highPassCutoff = juce::jmin(highPassParam->load(), nyquist);
    const auto lowPassCutoff = juce::jmin(lowPassParam->load(), nyquist);

    *highPassFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, highPassCutoff);
    *lowPassFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, lowPassCutoff);

    // Hi Pass's knob rotation (0 = fully left/20Hz, 1 = fully right/2000Hz), read through its own
    // NormalisableRange so this tracks the knob's actual skewed position rather than a plain
    // linear reading of the underlying Hz value. Drives the companion EQ dip's gain: full -3dB
    // cut at the leftmost position, fading linearly to 0dB (no effect) at the rightmost.
    const auto highPassProportion = highPassRangedParam->getNormalisableRange().convertTo0to1(highPassParam->load());
    const auto highPassDipGainDb = juce::jmap(highPassProportion, 0.0f, 1.0f, highPassDipMaxCutDb, 0.0f);

    *highPassDipFilter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, highPassDipFrequency, highPassDipQ, juce::Decibels::decibelsToGain(highPassDipGainDb));
}

bool DamageAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void DamageAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    const auto drive = driveParam->load();
    const auto square = squareParam->load() > 0.5f;
    const auto oscillate = oscillateParam->load() > 0.5f;

    const auto sampleRate = getSampleRate();
    const auto oscBaseFreq = oscFreqParam->load();
    const auto fmDepthHz = oscBaseFreq * fmModulationIndex;

    // Drive narrows Pulse Width's usable range as it's turned down: at minimum Drive, Width is
    // locked to 60% regardless of the knob's raw position (a lower-drive signal doesn't have the
    // level to make an off-centre duty cycle read as anything but a plain pulse anyway); at
    // maximum Drive, Width's full current 0-100% range is restored unchanged. In between, the
    // raw knob reading is rescaled into a range that widens from [60,60] to [0,100].
    const auto driveNormalised = driveRange.convertTo0to1(drive);

    // Square root, not linear or squared -- squaring (the original fix for the comparator getting
    // pinned to one side, then DC-blocked to silence, at moderately off-centre widths before Drive
    // was pushing enough gain) read as too subtle even after splitting the difference with linear,
    // so this goes past linear the other way: square root opens the range faster than linear does
    // (e.g. driveNormalised 0.25 already yields shape 0.5, versus linear's own 0.25), giving more
    // usable Width range earlier in Drive's travel than either previous version did. Still exactly
    // 0 at Drive's true minimum and 1 at its true maximum, so the width lock at minimum Drive and
    // the full range at maximum are unchanged.
    const auto widthRangeShape = std::sqrt(driveNormalised);
    const auto widthRangeMin = juce::jmap(widthRangeShape, 0.0f, 1.0f, 60.0f, 0.0f);
    const auto widthRangeMax = juce::jmap(widthRangeShape, 0.0f, 1.0f, 60.0f, 100.0f);
    const auto effectiveWidth = juce::jmap(widthParam->load(), 0.0f, 100.0f, widthRangeMin, widthRangeMax);

    // Width is a 0-100% duty-cycle control: 50% centres the comparator threshold at zero
    // (an even pulse), while moving towards either end skews the threshold off-centre so
    // one polarity's rail dominates the cycle, narrowing or widening the pulse.
    const auto thresholdOffset = (effectiveWidth * 0.01f - 0.5f) * 1.6f;

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();

    for (int channel = 0; channel < numChannels; ++channel)
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, numSamples);

    // Chorus is applied to the dry tap alone, with its mix tied to the Drive knob: silent at
    // minimum Drive, growing to dryChorusMaxMix at maximum Drive. Chorus::process blends its own
    // wet/dry internally per its mix parameter, so this is the only place that blend happens.
    // Reuses driveNormalised computed above for the Width/Drive relationship.
    dryChorus.setMix(driveNormalised * dryChorusMaxMix);
    auto dryBlock = juce::dsp::AudioBlock<float>(dryBuffer).getSubBlock(0, static_cast<size_t>(numSamples));
    juce::dsp::ProcessContextReplacing<float> dryContext(dryBlock);
    dryChorus.process(dryContext);

    // Detect gate level from the loudest channel so stereo material gates together rather than
    // each channel independently, which would smear the image as one side closes before the other.
    const auto gateThresholdDb = gateParam->load();
    const auto gateOpenThreshold = juce::Decibels::decibelsToGain(gateThresholdDb);
    const auto gateCloseThreshold = juce::Decibels::decibelsToGain(gateThresholdDb - gateHysteresisDb);

    const auto slowRelease = slowReleaseParam->load() > 0.5f;
    const auto activeGateEnvelopeReleaseCoeff = slowRelease ? gateEnvelopeReleaseCoeffSlow : gateEnvelopeReleaseCoeff;
    const auto activeGateGainReleaseCoeff = slowRelease ? gateGainReleaseCoeffSlow : gateGainReleaseCoeff;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto level = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            level = juce::jmax(level, std::abs(buffer.getReadPointer(channel)[sample]));

        const auto envelopeCoeff = level > gateEnvelope ? gateEnvelopeAttackCoeff : activeGateEnvelopeReleaseCoeff;
        gateEnvelope = envelopeCoeff * gateEnvelope + (1.0f - envelopeCoeff) * level;

        if (gateOpen)
        {
            if (gateEnvelope < gateCloseThreshold)
                gateOpen = false;
        }
        else
        {
            if (gateEnvelope > gateOpenThreshold)
                gateOpen = true;
        }

        const auto targetGain = gateOpen ? 1.0f : 0.0f;
        const auto gainCoeff = targetGain > gateGain ? gateGainAttackCoeff : activeGateGainReleaseCoeff;
        gateGain = gainCoeff * gateGain + (1.0f - gainCoeff) * targetGain;

        gateGainBuffer[sample] = gateGain;

        const auto oscEnvelopeCoeff = level > oscGateEnvelope ? oscGateEnvelopeAttackCoeff : oscGateEnvelopeReleaseCoeff;
        oscGateEnvelope = oscEnvelopeCoeff * oscGateEnvelope + (1.0f - oscEnvelopeCoeff) * level;

        if (oscGateOpen)
        {
            if (oscGateEnvelope < gateCloseThreshold)
                oscGateOpen = false;
        }
        else
        {
            if (oscGateEnvelope > gateOpenThreshold)
                oscGateOpen = true;
        }

        const auto oscTargetGain = oscGateOpen ? 1.0f : 0.0f;
        const auto oscGainCoeff = oscTargetGain > oscGateGain ? oscGateGainAttackCoeff : oscGateGainReleaseCoeff;
        oscGateGain = oscGainCoeff * oscGateGain + (1.0f - oscGainCoeff) * oscTargetGain;

        oscGateBuffer[sample] = oscGateGain;

        // The modulator: a 0-1 envelope of how hard the input is driving the fuzz right now,
        // reusing the same raw level the gate tracks. Bending the carrier's frequency with this
        // - rather than running it at a fixed rate - is what makes it FM instead of a plain LFO.
        const auto modulator = std::tanh(level * drive);
        const auto instantaneousFreq = juce::jmax(0.0f, oscBaseFreq + fmDepthHz * modulator);
        const auto oscPhaseIncrement = sampleRate > 0.0
                                            ? static_cast<float>(instantaneousFreq / sampleRate)
                                            : 0.0f;

        oscBuffer[sample] = oscPhase < 0.5f ? 1.0f : 0.0f;
        oscPhase += oscPhaseIncrement;
        if (oscPhase >= 1.0f)
            oscPhase -= 1.0f;
    }

    gateLevelDb.store(juce::Decibels::gainToDecibels(gateEnvelope, -100.0f), std::memory_order_relaxed);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto driven = channelData[sample] * gateGainBuffer[sample] * drive;
            const auto fuzzed = square ? squareFuzz(driven, thresholdOffset) : pulseFuzz(driven, thresholdOffset);

            // oscBuffer is 0/1; remap it to [oscChopFloor, 1] so the chop dips rather than mutes.
            const auto oscGain = oscChopFloor + (1.0f - oscChopFloor) * oscBuffer[sample];

            // The comparator still rails to +-1 even when the gated signal has decayed to
            // nothing, so without this the oscillator would keep audibly chopping that rail
            // into a tone of its own after the input has died away. oscGateBuffer (not the main
            // gate) closes this quickly behind the note rather than trailing its musical release.
            channelData[sample] = oscillate ? fuzzed * oscGain * oscGateBuffer[sample] : fuzzed;
        }
    }

    updateFilters();

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    dcBlocker.process(context);
    highPassFilter.process(context);
    lowPassFilter.process(context);
    highPassDipFilter.process(context);

    // Dry and Wet are independent gains, not a crossfade: Dry is the untouched input captured
    // before the gate/fuzz/filter chain, Wet is the fully processed signal above, and the two just
    // sum - so both can be present together, or either can be silenced on its own, rather than
    // one necessarily coming at the other's expense. wetPadDb is folded into wetGain rather than
    // applied to the buffer on its own, since it only ever makes sense scaled by the user's own
    // Wet trim, not as a separate always-on stage.
    const auto dryGain = dryParam->load() * 0.01f;
    const auto wetGain = wetParam->load() * 0.01f * juce::Decibels::decibelsToGain(wetPadDb);
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* wet = buffer.getWritePointer(channel);
        const auto* dry = dryBuffer.getReadPointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
            wet[sample] = dry[sample] * dryGain + wet[sample] * wetGain;
    }
}

// createEditor() lives in PluginEditor.cpp (not here) specifically so this file has no
// PluginEditor.h/GUI dependency - DamageTests links only this file plus juce_audio_processors/juce_dsp.
bool DamageAudioProcessor::hasEditor() const { return true; }

const juce::String DamageAudioProcessor::getName() const { return JucePlugin_Name; }

bool DamageAudioProcessor::acceptsMidi() const { return false; }
bool DamageAudioProcessor::producesMidi() const { return false; }
bool DamageAudioProcessor::isMidiEffect() const { return false; }
double DamageAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int DamageAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int DamageAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void DamageAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String DamageAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }

void DamageAudioProcessor::changeProgramName(int, const juce::String&) {}

void DamageAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void DamageAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DamageAudioProcessor();
}
