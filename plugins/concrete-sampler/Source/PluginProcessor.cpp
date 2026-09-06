#include "PluginProcessor.h"

#include <cmath>

namespace
{
    // No factory presets yet - Phase 7 defines the twelve machine presets from the plan's machine
    // table (concrete-sampler-plugin-plan.md), decoded from real .aupreset files the same way
    // every other plugin's getFactoryPresets() is, per common/Presets/FactoryPreset.h's
    // convention. Empty for now: FactoryPresetList/getNumPrograms() handle zero presets correctly.
    const std::vector<wildjag::FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<wildjag::FactoryPreset> presets = {};
        return presets;
    }

    // A non-owning, channel-shifted view onto outputBuffer for ConcreteBusRouter's destination
    // indirection (see that class's comment, and concrete-sampler-plugin-plan.md's Architecture
    // #1 "architect for aux outs"). v1's router always returns offset 0, so this always spans the
    // same channels as outputBuffer itself - it exists so the indirection is real code a voice
    // actually renders through, not a comment promising it'll be added later. Real-time safe:
    // JUCE's external-pointer AudioBuffer constructor uses a fixed-size on-object channel-pointer
    // array (up to 32 channels) rather than allocating, so this never touches the heap.
    juce::AudioBuffer<float> channelView(juce::AudioBuffer<float>& outputBuffer, int channelOffset)
    {
        return juce::AudioBuffer<float>(outputBuffer.getArrayOfWritePointers() + channelOffset,
                                         outputBuffer.getNumChannels() - channelOffset,
                                         0, outputBuffer.getNumSamples());
    }

    // Derives one zone's WORKING buffer (see ConcreteSampleZone.h) from its already-loaded SOURCE
    // buffer via Phase 4's capture pass, and rescales start/end/loop points by however much the
    // resample step changed the buffer's length - a no-op today (v1 has no trim/loop UI yet, so
    // start=0/end=full-length always - see Phase 8), but correct once one exists. Zones with no
    // source (a missing-file zone, Architecture #2) pass through unchanged.
    ConcreteSampleZone bakeZone(const ConcreteSampleZone& sourceZone, const ConcreteCapturePass::Settings& settings)
    {
        auto zone = sourceZone;
        if (zone.sourceBuffer == nullptr || zone.sourceBuffer->getNumSamples() <= 0)
            return zone;

        const auto baked = ConcreteCapturePass::apply(*zone.sourceBuffer, zone.sourceSampleRate, settings);
        const auto scale = (double) baked.buffer->getNumSamples() / (double) zone.sourceBuffer->getNumSamples();

        zone.buffer = baked.buffer;
        zone.captureTransposeSemitones = baked.captureTransposeSemitones;
        zone.start = (juce::int64) std::llround((double) zone.start * scale);
        zone.end = (juce::int64) std::llround((double) zone.end * scale);
        zone.loopStart = (juce::int64) std::llround((double) zone.loopStart * scale);
        zone.loopEnd = (juce::int64) std::llround((double) zone.loopEnd * scale);
        return zone;
    }

    ConcreteSampleSet::Ptr bakeSampleSet(const ConcreteSampleSet& sourceSet, const ConcreteCapturePass::Settings& settings)
    {
        ConcreteSampleSet::Ptr result(new ConcreteSampleSet());
        result->zones.reserve(sourceSet.zones.size());
        for (const auto& zone : sourceSet.zones)
            result->zones.push_back(bakeZone(zone, settings));
        return result;
    }
}

ConcreteAudioProcessor::ConcreteAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("ConcreteCaptureBake"),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets()),
      currentSampleSet(new ConcreteSampleSet()),
      rawSampleSet(new ConcreteSampleSet())
{
    formatManager.registerBasicFormats();

    pitchEngineModeParam = apvts.getRawParameterValue(pitchEngineModeParamID);
    baseRateParam = apvts.getRawParameterValue(baseRateParamID);
    coarseTuneParam = apvts.getRawParameterValue(coarseTuneParamID);
    fineTuneParam = apvts.getRawParameterValue(fineTuneParamID);
    bitDepthParam = apvts.getRawParameterValue(bitDepthParamID);
    quantizerModeParam = apvts.getRawParameterValue(quantizerModeParamID);
    captureTransposeParam = apvts.getRawParameterValue(captureTransposeParamID);
    captureDriveParam = apvts.getRawParameterValue(captureDriveParamID);
    captureAutoCompensateParam = apvts.getRawParameterValue(captureAutoCompensateParamID);
    captureBypassParam = apvts.getRawParameterValue(captureBypassParamID);
    captureIterationsParam = apvts.getRawParameterValue(captureIterationsParamID);
    filterModelParam = apvts.getRawParameterValue(filterModelParamID);
    filterCutoffParam = apvts.getRawParameterValue(filterCutoffParamID);
    filterResonanceParam = apvts.getRawParameterValue(filterResonanceParamID);
    filterEnvAmountParam = apvts.getRawParameterValue(filterEnvAmountParamID);
    filterKeyTrackParam = apvts.getRawParameterValue(filterKeyTrackParamID);
    captureDoubleSmearParam = apvts.getRawParameterValue(captureDoubleSmearParamID);
    doubleSmearFilterModelParam = apvts.getRawParameterValue(doubleSmearFilterModelParamID);
    doubleSmearCutoffParam = apvts.getRawParameterValue(doubleSmearCutoffParamID);
    doubleSmearResonanceParam = apvts.getRawParameterValue(doubleSmearResonanceParamID);

    // Only the parameters that actually change what ConcreteCapturePass::apply() produces need to
    // trigger a re-bake - captureAutoCompensate and every live filterXxx parameter are deliberately
    // excluded (see their own declaration comments in PluginProcessor.h).
    for (auto* paramID : { bitDepthParamID, quantizerModeParamID, captureTransposeParamID,
                            captureDriveParamID, captureBypassParamID, captureIterationsParamID,
                            captureDoubleSmearParamID, doubleSmearFilterModelParamID,
                            doubleSmearCutoffParamID, doubleSmearResonanceParamID })
        apvts.addParameterListener(paramID, this);

    startThread();
}

ConcreteAudioProcessor::~ConcreteAudioProcessor()
{
    // Must happen before any of this object's OTHER members (or apvts) start being destroyed -
    // see run()'s own comment. juce::Thread's own destructor would eventually stop the thread too,
    // but only AFTER this derived class's members are already gone, which is too late if run() is
    // still mid-rebakeNow() at that point.
    stopThread(2000);
}

namespace
{
    // pitchEngineModeParam's raw value is the AudioParameterChoice's selected INDEX as a float
    // (0..3) - this is the one place that index order has to match the StringArray passed to the
    // AudioParameterChoice constructor in createParameterLayout() below.
    ConcretePitchEngine::Mode pitchEngineModeFromParam(float rawIndex) noexcept
    {
        switch ((int) std::lround(rawIndex))
        {
            case 1:  return ConcretePitchEngine::Mode::variableClockZeroOrderHold;
            case 2:  return ConcretePitchEngine::Mode::dropSampleDecimation;
            case 3:  return ConcretePitchEngine::Mode::deltaSigma;
            default: return ConcretePitchEngine::Mode::reference;
        }
    }

    // Same convention as pitchEngineModeFromParam() - quantizerModeParam's raw value is the
    // AudioParameterChoice's selected index (0..1) as a float, matching the StringArray order in
    // createParameterLayout() below.
    ConcreteQuantizer::Mode quantizerModeFromParam(float rawIndex) noexcept
    {
        return (int) std::lround(rawIndex) == 1 ? ConcreteQuantizer::Mode::companded
                                                 : ConcreteQuantizer::Mode::linear;
    }

    // Same convention again - the raw value is the AudioParameterChoice's selected index (0..5),
    // matching the StringArray order in createParameterLayout() below (Bypass/SSM/CEM Loss/CEM
    // Compensated/Digital+VCA/One-Pole). Shared by the live filter parameter and the double-smear
    // filter parameter, which are two independent choices using this same mapping.
    ConcreteFilterModel::Mode filterModeFromParam(float rawIndex) noexcept
    {
        switch ((int) std::lround(rawIndex))
        {
            case 1:  return ConcreteFilterModel::Mode::ssm;
            case 2:  return ConcreteFilterModel::Mode::cemResonanceLoss;
            case 3:  return ConcreteFilterModel::Mode::cemResonanceCompensated;
            case 4:  return ConcreteFilterModel::Mode::digitalVca;
            case 5:  return ConcreteFilterModel::Mode::onePole;
            default: return ConcreteFilterModel::Mode::bypass;
        }
    }
}

ConcreteCapturePass::Settings ConcreteAudioProcessor::currentCapturePassSettings() const
{
    ConcreteCapturePass::Settings settings;
    settings.bypass = captureBypassParam->load() >= 0.5f;
    settings.transposeSemitones = (double) captureTransposeParam->load();
    settings.inputDriveDb = (double) captureDriveParam->load();
    settings.iterations = (int) std::lround(captureIterationsParam->load());
    settings.quantizerMode = quantizerModeFromParam(quantizerModeParam->load());
    settings.bitDepthBits = (int) std::lround(bitDepthParam->load());
    settings.doubleSmear = captureDoubleSmearParam->load() >= 0.5f;
    settings.doubleSmearFilterModel = filterModeFromParam(doubleSmearFilterModelParam->load());
    settings.doubleSmearCutoffHz = (double) doubleSmearCutoffParam->load();
    settings.doubleSmearResonance01 = (double) doubleSmearResonanceParam->load();
    return settings;
}

void ConcreteAudioProcessor::rebakeNow()
{
    // Always re-bakes from rawSampleSet (never-rescaled, source-space start/end), NOT from
    // getCurrentSampleSet() - see rawSampleSet's own comment for the compounding-shrink bug that
    // baking from the previous BAKED result caused.
    publishSampleSet(bakeSampleSet(*getRawSampleSet(), currentCapturePassSettings()));
}

void ConcreteAudioProcessor::run()
{
    while (!threadShouldExit())
    {
        wait(-1);
        // Re-checks bakeRequested rather than baking once per wait() - a parameterChanged() that
        // lands WHILE rebakeNow() is already running (e.g. a host smoothing a knob move into many
        // rapid automation events) must not be missed just because this thread wasn't back at
        // wait() yet to receive it.
        while (bakeRequested.exchange(false) && !threadShouldExit())
            rebakeNow();
    }
}

void ConcreteAudioProcessor::parameterChanged(const juce::String&, float)
{
    // Deliberately the ENTIRE body - may run on the audio thread (see this method's declaration
    // comment in PluginProcessor.h), so it must stay lock-free/allocation-free/wait-free. notify()
    // is a plain juce::WaitableEvent signal under the hood, safe to call from any thread.
    bakeRequested = true;
    notify();
}

juce::AudioProcessorValueTreeState::ParameterLayout ConcreteAudioProcessor::createParameterLayout()
{
    // Phase 2's first real automatable parameters - root note/key range/tune/level/pan stay
    // zone-list state (Architecture #1), but pitch engine mode/base rate/coarse/fine tune are
    // genuine live controls a machine preset (Phase 7) will set defaults for and the user can
    // push past them. Ranges/step sizes for the tune pair match gradient-pitch's own
    // pitchSemitones/pitchFineCents controls (-24..24st, -50..50ct) for consistency across the
    // catalog's pitch-shifting controls.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{pitchEngineModeParamID, 1},
        "Pitch Engine",
        juce::StringArray{"Reference", "Mode A: Variable Clock", "Mode B: Drop-Sample", "Mode C: Delta-Sigma"},
        0));

    // Default 44.1kHz, NOT one of the machines' own rates (e.g. the SP-1200's 26.04kHz, the first
    // choice tried here) - baseRate substitutes for the loaded file's own rate for Modes A/B/C
    // (see ConcretePitchEngine.h), and almost every file a user loads will itself be a 44.1kHz
    // recording, so defaulting to anything else means root note DOESN'T reproduce the original
    // pitch/tempo out of the box, in every mode, before the user has touched anything - breaking
    // the one convention every sampler upholds. At baseRate == the file's real rate and unison
    // pitch, Mode A reduces to a bit-exact match of Reference (zero-order-hold and cubic
    // interpolation both collapse to reading the exact sample at zero fractional offset), so this
    // default costs nothing: the character still shows up exactly where it should - as soon as the
    // note is transposed away from root, or Base Rate is deliberately lowered to emulate a
    // narrower-bandwidth machine on purpose. Range covers the full machine table's span
    // (concrete-sampler-plugin-plan.md's preset table, Phase 7): the SK-1's fixed 9.38kHz up
    // through the Synclavier's programmable ceiling of 100kHz.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{baseRateParamID, 1},
        "Base Rate",
        juce::NormalisableRange<float>(4000.0f, 100000.0f, 1.0f, 0.4f),
        44100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + " Hz"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{coarseTuneParamID, 1},
        "Coarse Tune",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("st")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{fineTuneParamID, 1},
        "Fine Tune",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ct")));

    // Phase 3's quantization stage (see ConcreteQuantizer.h). Default 16-bit/Linear is
    // deliberately transparent - same "no artifacts before the user or a machine preset asks for
    // them" convention as Base Rate's 44.1kHz default above: at 16 bits the quantization noise
    // floor sits around -96dBFS, far below anything audible, so this costs nothing until a Phase 7
    // preset (or the user directly) dials it down toward one of the machines' real depths (8, 12,
    // 13 bits).
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{bitDepthParamID, 1},
        "Bit Depth",
        1, 16, 16,
        juce::AudioParameterIntAttributes().withLabel("bit")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{quantizerModeParamID, 1},
        "Quantizer Mode",
        juce::StringArray{"Linear", "Companded"}, 0));

    // Phase 4's capture pass (see ConcreteCapturePass.h). Default 5 semitones is "the one people
    // actually want" (the 33->45rpm ratio) - safe to default on even though captureBypass (below)
    // defaults to true, since it has no audible effect until bypass is turned off.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{captureTransposeParamID, 1},
        "Capture Transpose",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.01f),
        5.0f,
        juce::AudioParameterFloatAttributes().withLabel("st")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{captureDriveParamID, 1},
        "Capture Drive",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{captureAutoCompensateParamID, 1},
        "Capture Auto-Compensate",
        true));

    // Defaults on (bypassed) - see the plan's Phase 7 machine-table notes: "every preset ships
    // with the capture pass off - it's a technique the user applies, not part of a machine's stock
    // behavior."
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{captureBypassParamID, 1},
        "Capture Bypass",
        true));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{captureIterationsParamID, 1},
        "Capture Iterations",
        1, 4, 1));

    // Phase 5's playback-side filter (see ConcreteFilterModels.h). Defaults to Bypass/fully-open/
    // no resonance/no modulation - transparent until a machine preset (Phase 7) or the user
    // deliberately engages it, matching every other control's own default convention.
    const juce::StringArray filterModelChoices{"Bypass", "SSM", "CEM Loss", "CEM Compensated", "Digital+VCA", "One-Pole"};

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{filterModelParamID, 1}, "Filter Model", filterModelChoices, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{filterCutoffParamID, 1},
        "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f),
        20000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + " Hz"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{filterResonanceParamID, 1},
        "Filter Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{filterEnvAmountParamID, 1},
        "Filter Env Amount",
        juce::NormalisableRange<float>(-8.0f, 8.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("oct")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{filterKeyTrackParamID, 1},
        "Filter Key Track",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f));

    // Phase 5's "double smear" (Architecture #3) - off by default, its own dedicated model/cutoff/
    // resonance, deliberately independent of the live filter parameters above.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{captureDoubleSmearParamID, 1}, "Double Smear", false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{doubleSmearFilterModelParamID, 1}, "Double Smear Filter Model", filterModelChoices, 1));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{doubleSmearCutoffParamID, 1},
        "Double Smear Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f),
        8000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + " Hz"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{doubleSmearResonanceParamID, 1},
        "Double Smear Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f));

    return { params.begin(), params.end() };
}

void ConcreteAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    for (auto& voice : voices)
        voice.prepare(sampleRate);
}

void ConcreteAudioProcessor::releaseResources() {}

bool ConcreteAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Main output only, mono or stereo - matches every other instrument/effect in this catalog
    // (see AGENTS.md's mono/stereo fix, commit 6230d6d). Aux buses are deliberately not declared
    // in v1 (see the plan's Architecture #1 / Decisions), but every voice routes through
    // ConcreteBusRouter's destination-index indirection specifically so adding them later doesn't
    // touch voice code - this bus declaration is the only part that will need to change.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ConcreteAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Merges the on-screen keyboard's notes into the same MIDI buffer real MIDI input uses - the
    // standard JUCE pattern for embedding a MidiKeyboardComponent in a plugin editor (see
    // keyboardState's own comment).
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    // One snapshot for the whole block (not per-sample/per-event): a single atomic refcount bump
    // via sampleSetLock, not held during rendering. See currentSampleSet's own comment.
    const auto sampleSet = getCurrentSampleSet();

    int samplePosition = 0;
    auto midiIterator = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();

    // Renders in segments bounded by MIDI event positions, matching the sample-accuracy of a
    // per-sample dispatch loop (Strike's own approach) without needing ConcreteVoice's inner loop
    // to be interrupted every sample.
    while (samplePosition < buffer.getNumSamples())
    {
        while (midiIterator != midiEnd && (*midiIterator).samplePosition <= samplePosition)
        {
            handleMidiMessage((*midiIterator).getMessage(), sampleSet);
            ++midiIterator;
        }

        auto segmentEnd = buffer.getNumSamples();
        if (midiIterator != midiEnd)
            segmentEnd = juce::jmin(segmentEnd, (*midiIterator).samplePosition);

        const auto segmentLength = segmentEnd - samplePosition;
        if (segmentLength > 0)
        {
            for (auto& voice : voices)
            {
                if (!voice.isActive())
                    continue;

                const auto channelOffset = busRouter.getChannelOffsetForDestination(voice.getOutputDestination());
                auto destinationView = channelView(buffer, channelOffset);
                voice.renderNextBlock(destinationView, samplePosition, segmentLength);
            }
        }

        samplePosition = segmentEnd;
    }
}

void ConcreteAudioProcessor::handleMidiMessage(const juce::MidiMessage& message, const ConcreteSampleSet::Ptr& sampleSet) noexcept
{
    if (message.isNoteOn())
    {
        const auto note = message.getNoteNumber();
        const auto velocity7Bit = message.getVelocity();
        const auto velocity01 = message.getFloatVelocity();

        const auto zoneIndex = sampleSet->lookup(note, velocity7Bit);
        if (zoneIndex < 0)
            return; // no zone covers this note/velocity - nothing to trigger

        const auto& zone = sampleSet->zones[(size_t) zoneIndex];

        std::array<bool, maxVoices> isActive{};
        for (int i = 0; i < maxVoices; ++i)
            isActive[(size_t) i] = voices[(size_t) i].isActive();

        // v1 never sets a non-zero chokeGroup on any zone, so this never fires yet - see
        // Architecture #1 and ConcreteVoiceAllocator.h.
        if (zone.chokeGroup != 0)
        {
            const auto chokeMask = voiceAllocator.getChokeMask(zone.chokeGroup, isActive);
            for (int i = 0; i < maxVoices; ++i)
                if (chokeMask[(size_t) i])
                    voices[(size_t) i].stopNote(false, true); // forced - a choke cuts a one-shot voice too
        }

        const auto mode = pitchEngineModeFromParam(pitchEngineModeParam->load());
        // Mode::reference plays the file back at its own real rate; the three machine modes
        // instead assume the machine's own base rate governs playback, ignoring the file's actual
        // rate entirely - see ConcretePitchEngine.h's header comment on effectiveSourceRateHz.
        const auto effectiveSourceRateHz = mode == ConcretePitchEngine::Mode::reference
            ? zone.sourceSampleRate : (double) baseRateParam->load();

        const auto filterMode = filterModeFromParam(filterModelParam->load());

        const auto voiceIndex = voiceAllocator.allocateVoiceForNoteOn(note, zoneIndex, zone.chokeGroup, isActive);
        voices[(size_t) voiceIndex].startNote(sampleSet, zoneIndex, note, velocity01, mode, effectiveSourceRateHz,
                                                (int) std::lround(coarseTuneParam->load()), fineTuneParam->load(),
                                                captureAutoCompensateParam->load() >= 0.5f,
                                                filterMode, filterCutoffParam->load(), filterResonanceParam->load(),
                                                filterEnvAmountParam->load(), filterKeyTrackParam->load());
    }
    else if (message.isNoteOff())
    {
        const auto note = message.getNoteNumber();
        const auto voiceIndex = voiceAllocator.findVoiceForNoteOff(note);
        if (voiceIndex >= 0)
            voices[(size_t) voiceIndex].stopNote(true, false); // ordinary note-off - respects the zone's oneShot
    }
}

bool ConcreteAudioProcessor::loadSample(const juce::File& file)
{
    auto zone = ConcreteSampleIO::loadZoneFromFile(formatManager, file, 60);
    if (zone.sourceMissing)
        return false;

    ConcreteSampleSet::Ptr newRawSet(new ConcreteSampleSet());
    newRawSet->zones.push_back(zone); // start=0/end=sourceBuffer length - source-space, as required
    publishRawSampleSet(newRawSet);
    return true;
}

void ConcreteAudioProcessor::setRootNoteForZone(int zoneIndex, int newRootNote)
{
    const auto existingRaw = getRawSampleSet();
    if (!juce::isPositiveAndBelow(zoneIndex, (int) existingRaw->zones.size()))
        return;

    ConcreteSampleSet::Ptr newRawSet(new ConcreteSampleSet());
    newRawSet->zones = existingRaw->zones;
    newRawSet->zones[(size_t) zoneIndex].rootNote = newRootNote;
    publishRawSampleSet(newRawSet);
}

void ConcreteAudioProcessor::setOneShotForZone(int zoneIndex, bool oneShot)
{
    const auto existingRaw = getRawSampleSet();
    if (!juce::isPositiveAndBelow(zoneIndex, (int) existingRaw->zones.size()))
        return;

    ConcreteSampleSet::Ptr newRawSet(new ConcreteSampleSet());
    newRawSet->zones = existingRaw->zones;
    newRawSet->zones[(size_t) zoneIndex].oneShot = oneShot;
    publishRawSampleSet(newRawSet);
}

bool ConcreteAudioProcessor::relocateZone(int zoneIndex, const juce::File& newFile)
{
    const auto existingRaw = getRawSampleSet();
    if (!juce::isPositiveAndBelow(zoneIndex, (int) existingRaw->zones.size()))
        return false;

    auto relocated = ConcreteSampleIO::relocateZone(existingRaw->zones[(size_t) zoneIndex], formatManager, newFile);
    if (relocated.sourceMissing)
        return false;

    ConcreteSampleSet::Ptr newRawSet(new ConcreteSampleSet());
    newRawSet->zones = existingRaw->zones;
    newRawSet->zones[(size_t) zoneIndex] = relocated; // source-space, from ConcreteSampleIO::relocateZone()
    publishRawSampleSet(newRawSet);
    return true;
}

void ConcreteAudioProcessor::publishSampleSet(ConcreteSampleSet::Ptr newSet)
{
    const juce::SpinLock::ScopedLockType lock(sampleSetLock);
    currentSampleSet = newSet;
}

void ConcreteAudioProcessor::publishRawSampleSet(ConcreteSampleSet::Ptr newRawSet)
{
    // Baked BEFORE taking the lock - bakeSampleSet() can be expensive (resampling/quantizing a
    // real buffer) and must never run while holding a lock the audio thread might also want (see
    // sampleSetLock's own comment on why it's never held during rendering).
    const auto baked = bakeSampleSet(*newRawSet, currentCapturePassSettings());
    const juce::SpinLock::ScopedLockType lock(sampleSetLock);
    rawSampleSet = newRawSet;
    currentSampleSet = baked;
}

ConcreteSampleSet::Ptr ConcreteAudioProcessor::getCurrentSampleSet() const
{
    const juce::SpinLock::ScopedLockType lock(sampleSetLock);
    return currentSampleSet;
}

ConcreteSampleSet::Ptr ConcreteAudioProcessor::getRawSampleSet() const
{
    const juce::SpinLock::ScopedLockType lock(sampleSetLock);
    return rawSampleSet;
}

bool ConcreteAudioProcessor::hasEditor() const { return true; }

const juce::String ConcreteAudioProcessor::getName() const { return JucePlugin_Name; }

bool ConcreteAudioProcessor::acceptsMidi() const { return true; }
bool ConcreteAudioProcessor::producesMidi() const { return false; }
bool ConcreteAudioProcessor::isMidiEffect() const { return false; }

// 0 until a later phase gives voices a longer release tail worth reporting - Phase 1's basic
// envelope (2ms attack, 50ms release) is short enough that this stays honest.
double ConcreteAudioProcessor::getTailLengthSeconds() const { return 0.05; }

int ConcreteAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int ConcreteAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void ConcreteAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String ConcreteAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }
void ConcreteAudioProcessor::changeProgramName(int, const juce::String&) {}

void ConcreteAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        // The RAW set, not the baked one - sampleSetToValueTree()/zoneToValueTree() persist each
        // zone's start/end/loop points alongside its embedded sourceBuffer audio, and those points
        // must stay in the SAME (source-buffer) terms as what's embedded. The baked set's start/
        // end are rescaled to the WORKING buffer's length (see ConcreteCapturePass.h), which can
        // differ from the embedded source's own length whenever a non-trivial capture transpose is
        // active - persisting those would restore a zone whose start/end don't match its own
        // audio.
        const auto rawSet = getRawSampleSet();
        state.appendChild(ConcreteSampleIO::sampleSetToValueTree(*rawSet, embedSamplesOverride), nullptr);

        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void ConcreteAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;

    const auto restoredState = juce::ValueTree::fromXml(*xml);
    const auto zonesTree = restoredState.getChildWithName(ConcreteZoneIDs::zones);

    apvts.replaceState(restoredState);

    embedSamplesOverride = (bool) zonesTree.getProperty(ConcreteZoneIDs::embedOverride, false);
    // The working buffer is never persisted (Architecture #2) - valueTreeToSampleSet() restores
    // only each zone's sourceBuffer with source-space start/end, which becomes the new raw set;
    // publishRawSampleSet() re-derives and publishes the baked working buffer from it using
    // whatever capture-pass parameter values apvts.replaceState() just restored above.
    publishRawSampleSet(ConcreteSampleIO::valueTreeToSampleSet(zonesTree, formatManager));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ConcreteAudioProcessor();
}
