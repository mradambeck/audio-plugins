#include "PluginProcessor.h"

namespace
{
    // Ramp time for the two live-smoothed parameters (damping, output level) - short enough to
    // feel immediate, long enough to eliminate zipper noise/clicks when moved during playback.
    constexpr double smoothingRampSeconds = 0.02;
}

KarplunkAudioProcessor::KarplunkAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    dampingParam = apvts.getRawParameterValue(dampingParamID);
    outputLevelParam = apvts.getRawParameterValue(outputLevelParamID);
    brightnessParam = apvts.getRawParameterValue(brightnessParamID);
    bowAmountParam = apvts.getRawParameterValue(bowAmountParamID);
    bowForceParam = apvts.getRawParameterValue(bowForceParamID);
    structureParam = apvts.getRawParameterValue(structureParamID);
    positionParam = apvts.getRawParameterValue(positionParamID);
    monoParam = apvts.getRawParameterValue(monoParamID);
    glideTimeParam = apvts.getRawParameterValue(glideTimeParamID);
    waveshapeParam = apvts.getRawParameterValue(waveshapeParamID);
    waveshaperTypeParam = apvts.getRawParameterValue(waveshaperTypeParamID);
    ringModAmountParam = apvts.getRawParameterValue(ringModAmountParamID);
    ringModFrequencyParam = apvts.getRawParameterValue(ringModFrequencyParamID);
    topologyParam = apvts.getRawParameterValue(topologyParamID);
    crossCoupleParam = apvts.getRawParameterValue(crossCoupleParamID);
    coupleDelayParam = apvts.getRawParameterValue(coupleDelayParamID);
    detuneParam = apvts.getRawParameterValue(detuneParamID);
    loopFilterTypeParam = apvts.getRawParameterValue(loopFilterTypeParamID);
    resonanceParam = apvts.getRawParameterValue(resonanceParamID);
    formantFrequencyParam = apvts.getRawParameterValue(formantFrequencyParamID);
}

KarplunkAudioProcessor::~KarplunkAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout KarplunkAudioProcessor::createParameterLayout()
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
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{brightnessParamID, 1},
        "Pluck Brightness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{bowAmountParamID, 1},
        "Pluck / Bow",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Bow-side only (no effect at Pluck/Bow=0%) - the friction bow model's own "Bow Pressure"
    // control (see KarplunkExcitation::setBowForce()'s own comment). Defaults to 50%, STK's own
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

    // Defaults to 50% (the string's midpoint) - unlike Structure, Position has no neutral/bypass
    // value (every setting mixes an alternate string tap into the output - see KarplunkVoice.h's
    // renderNextSample()), so 50% was chosen as a deliberate, musically reasonable default rather
    // than a "no effect" one.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{positionParamID, 1},
        "Position",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Defaults to off (polyphonic) - preserves existing behavior for anyone who saved a preset
    // before this control existed, matching this project's convention of non-breaking defaults.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{monoParamID, 1}, "Mono", false));

    // Mono-only (see handleMidiMessage()'s mono branch) - a legato retrigger between two held
    // notes glides the pitch over this time instead of jumping instantly; a fresh note struck
    // from silence is unaffected regardless of this setting (nothing to glide from). Defaults to
    // 0ms (off) - preserves the exact instant-retrigger behavior Mono already shipped with.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{glideTimeParamID, 1},
        "Glide Time",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // The Waveshaper seam (see KarplunkWaveshaper.h) - defaults to 0% (bit-exact no-op, the
    // Waveshaper is never even called at this value - see KarplunkStringLineChannel::
    // renderChannelSample()), matching every other new-control convention in this project.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{waveshapeParamID, 1},
        "Waveshape",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // Runtime choice between the Waveshaper seam's four concrete implementations (see
    // KarplunkWaveshaper.h's own comment for why this one seam is a runtime dropdown rather than
    // a compile-time template parameter like the other three) - defaults to Fold (index 0),
    // matching every build/listening session so far.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{waveshaperTypeParamID, 1},
        "Waveshaper Type",
        juce::StringArray{"Fold", "Fuzz", "Saturate", "BitCrush"},
        0));

    // Ring Modulator (see KarplunkRingModulator.h) - its own area, not a Waveshaper Type, since it
    // needs its own Frequency control and runs alongside whichever Waveshaper Type is selected
    // (applied in-loop, after the Waveshaper - see KarplunkStringLineChannel::renderChannelSample()).
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
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // The Feedback Topology seam (see KarplunkVoice.h) - a runtime dropdown like Waveshaper Type,
    // at the user's explicit request, so Single and Dual can be A/B'd live. Defaults to Single
    // (index 0) - preserves every existing preset/test's behavior exactly.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{topologyParamID, 1},
        "Topology",
        juce::StringArray{"Single", "Dual"},
        0));

    // Dual-topology only (has no effect at Topology=Single, since KarplunkVoice::renderNextSample()
    // never reads it in that branch) - how much of each line's write-back value comes from the
    // OTHER line, live/every-sample. Provably safe across the entire 0-100% range with no ceiling
    // needed - see KarplunkVoice.h's own two-part safety argument (per-sample boundedness via
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
    // broadband effect into a harmonic-dependent one - see KarplunkVoice.h's own comment for why
    // this needs no new safety ceiling either (a pure delay only rotates phase, never adds gain).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{coupleDelayParamID, 1},
        "Couple Delay",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Dual-topology only - latched at noteOn (not live), same convention as Brightness, since real
    // unison detuning isn't a performance gesture the way Cross-Couple sweeping might be. 0% = both
    // lines at the identical target pitch (the primary, physically-grounded design - real coupled
    // piano unisons are nominally same-pitch strings, see KarplunkVoice.h's own header comment);
    // 100% offsets line B by KarplunkVoice::maxDetuneSemitones (~50 cents, raised from an initial
    // ~20 cent starting point once the user heard it and wanted more range).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{detuneParamID, 1},
        "Detune",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // The Loop Filter seam (see KarplunkLoopFilter.h) - a runtime dropdown like Waveshaper Type,
    // at the user's explicit request, so Two-Point Average and Resonant can be A/B'd live.
    // Defaults to Two-Point Average (index 0) - preserves every existing preset/test's behavior
    // exactly.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{loopFilterTypeParamID, 1},
        "Loop Filter Type",
        juce::StringArray{"Two-Point Average", "Resonant"},
        0));

    // Resonant-loop-filter-only (no effect at Loop Filter Type=Two-Point Average) - live/every-
    // sample, provably safe across the full 0-100% range with no ceiling needed (a resonant peak
    // mixed in via a convex combination can't push the loop's combined gain above what the
    // existing Two-Point Average stage already safely caps it to) - see KarplunkLoopFilter.h's own
    // closed-form argument.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{resonanceParamID, 1},
        "Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));

    // 80Hz-8kHz, skewed toward a vowel-ish/formant-relevant range - a reasoned starting range, not
    // measured against Karplunk's own loop, to be confirmed by listening. The stability proof is
    // completely independent of this value (see KarplunkLoopFilter.h), so this is a purely musical
    // choice - an absolute Hz value, not tracking the note's own pitch.
    juce::NormalisableRange<float> formantFrequencyRange(80.0f, 8000.0f);
    formantFrequencyRange.setSkewForCentre(800.0f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{formantFrequencyParamID, 1},
        "Formant Freq",
        formantFrequencyRange,
        1000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    return { params.begin(), params.end() };
}

void KarplunkAudioProcessor::prepareToPlay(double sampleRate, int)
{
    for (auto& v : voices)
        v.prepare(sampleRate);
    voiceAllocator.reset();
    monoNoteStack.reset();
    previousMonoMode = monoParam->load() >= 0.5f;
    previousTopology = (int) topologyParam->load();

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

    formantFrequencySmoothed.reset(sampleRate, smoothingRampSeconds);
    formantFrequencySmoothed.setCurrentAndTargetValue(formantFrequencyParam->load());
}

void KarplunkAudioProcessor::releaseResources() {}

bool KarplunkAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void KarplunkAudioProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
    {
        const auto note = message.getNoteNumber();
        const auto velocity = message.getFloatVelocity();

        if (previousMonoMode)
        {
            // Mono always retriggers with whatever note the stack says should now sound - on a
            // plain note-on that's just this note itself (see KarplunkMonoNoteStack::noteOn()).
            // Glide only actually engages if this is a legato retrigger (Voice::noteOn() checks
            // isActive() itself) - a fresh note from silence always snaps straight to pitch.
            const auto event = monoNoteStack.noteOn(note, velocity);
            voices[0].setBrightness(brightnessParam->load());
            voices[0].setDetuneAmount(detuneParam->load());
            voices[0].noteOn(event.note, event.velocity01, glideTimeParam->load() * 0.001f);
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
            // The core last-note-priority behavior (see KarplunkMonoNoteStack.h's own comment):
            // releasing the currently-sounding note falls back to retriggering whichever
            // still-held note is now on top, rather than just letting it ring out - only release
            // the voice once nothing at all remains held.
            const auto result = monoNoteStack.noteOff(message.getNoteNumber());
            if (result.stillHeld)
            {
                voices[0].setBrightness(brightnessParam->load());
                voices[0].setDetuneAmount(detuneParam->load());
                voices[0].noteOn(result.event.note, result.event.velocity01, glideTimeParam->load() * 0.001f);
            }
            else
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

void KarplunkAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
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
    formantFrequencySmoothed.setTargetValue(formantFrequencyParam->load());

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

    // Mono only ever sounds one voice, so it gets no headroom reduction - the 8-voice headroom
    // below exists so a full chord doesn't clip harder than a single Poly note did; applying it
    // to a single Mono voice would just make Mono sound quieter than Poly for no reason.
    const auto headroomGain = mono ? 1.0f : polyHeadroomGain;

    // Waveshaper Type is a discrete choice like Mono, but - unlike Mono - has no cross-referencing
    // bookkeeping (voiceAllocator/monoNoteStack) that could go stale on a mid-note switch; both
    // concrete waveshapers are stateless, so reading this fresh every block and letting it change
    // mid-note is completely safe.
    const auto waveshaperType = (int) waveshaperTypeParam->load();

    // Loop Filter Type is the same kind of discrete choice as Waveshaper Type, for the same reason:
    // both concrete filters are always constructed/prepared/kept current (via setDamping()'s own
    // fan-out - see KarplunkVoice.h), so a mid-note switch just leaves the UNSELECTED filter's own
    // history momentarily stale until reselected - no implicit all-notes-off needed, unlike Mono/
    // Topology, which have real cross-referencing bookkeeping that would otherwise go stale.
    const auto loopFilterType = (int) loopFilterTypeParam->load();

    auto midiIterator = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        while (midiIterator != midiEnd && (*midiIterator).samplePosition == sample)
        {
            handleMidiMessage((*midiIterator).getMessage());
            ++midiIterator;
        }

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
        const auto formantFrequency = formantFrequencySmoothed.getNextValue();

        float mixedSample = 0.0f;
        for (auto& v : voices)
        {
            v.setDamping(damping);
            v.setBowAmount(bowAmount);
            v.setBowForce(bowForce);
            v.setStructure(structure);
            v.setPosition(position);
            v.setWaveshapeAmount(waveshape);
            v.setWaveshaperType(waveshaperType);
            v.setRingModAmount(ringModAmount);
            v.setRingModFrequency(ringModFrequency);
            v.setTopology(topology);
            v.setCoupleDelay(coupleDelay);
            v.setCrossCoupleAmount(crossCouple);
            v.setLoopFilterType(loopFilterType);
            v.setResonance(resonance);
            v.setFormantFrequency(formantFrequency);
            mixedSample += v.renderNextSample();
        }

        const auto out = mixedSample * headroomGain * outputLevelSmoothed.getNextValue();
        for (int channel = 0; channel < numChannels; ++channel)
            buffer.setSample(channel, sample, out);
    }
}

bool KarplunkAudioProcessor::hasEditor() const { return true; }

const juce::String KarplunkAudioProcessor::getName() const { return JucePlugin_Name; }

bool KarplunkAudioProcessor::acceptsMidi() const { return true; }
bool KarplunkAudioProcessor::producesMidi() const { return false; }
bool KarplunkAudioProcessor::isMidiEffect() const { return false; }
double KarplunkAudioProcessor::getTailLengthSeconds() const { return 8.0; }

int KarplunkAudioProcessor::getNumPrograms() { return 1; }
int KarplunkAudioProcessor::getCurrentProgram() { return 0; }
void KarplunkAudioProcessor::setCurrentProgram(int) {}
const juce::String KarplunkAudioProcessor::getProgramName(int) { return {}; }
void KarplunkAudioProcessor::changeProgramName(int, const juce::String&) {}

void KarplunkAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void KarplunkAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KarplunkAudioProcessor();
}
