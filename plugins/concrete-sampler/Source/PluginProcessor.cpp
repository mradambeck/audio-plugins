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
}

ConcreteAudioProcessor::ConcreteAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets()),
      currentSampleSet(new ConcreteSampleSet())
{
    formatManager.registerBasicFormats();

    pitchEngineModeParam = apvts.getRawParameterValue(pitchEngineModeParamID);
    baseRateParam = apvts.getRawParameterValue(baseRateParamID);
    coarseTuneParam = apvts.getRawParameterValue(coarseTuneParamID);
    fineTuneParam = apvts.getRawParameterValue(fineTuneParamID);
}

ConcreteAudioProcessor::~ConcreteAudioProcessor() = default;

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
                    voices[(size_t) i].stopNote(false);
        }

        const auto mode = pitchEngineModeFromParam(pitchEngineModeParam->load());
        // Mode::reference plays the file back at its own real rate; the three machine modes
        // instead assume the machine's own base rate governs playback, ignoring the file's actual
        // rate entirely - see ConcretePitchEngine.h's header comment on effectiveSourceRateHz.
        const auto effectiveSourceRateHz = mode == ConcretePitchEngine::Mode::reference
            ? zone.sourceSampleRate : (double) baseRateParam->load();

        const auto voiceIndex = voiceAllocator.allocateVoiceForNoteOn(note, zoneIndex, zone.chokeGroup, isActive);
        voices[(size_t) voiceIndex].startNote(sampleSet, zoneIndex, note, velocity01, mode, effectiveSourceRateHz,
                                                (int) std::lround(coarseTuneParam->load()), fineTuneParam->load());
    }
    else if (message.isNoteOff())
    {
        const auto note = message.getNoteNumber();
        const auto voiceIndex = voiceAllocator.findVoiceForNoteOff(note);
        if (voiceIndex >= 0)
            voices[(size_t) voiceIndex].stopNote(true);
    }
}

bool ConcreteAudioProcessor::loadSample(const juce::File& file)
{
    auto zone = ConcreteSampleIO::loadZoneFromFile(formatManager, file, 60);
    if (zone.sourceMissing)
        return false;

    ConcreteSampleSet::Ptr newSet(new ConcreteSampleSet());
    newSet->zones.push_back(zone);
    publishSampleSet(newSet);
    return true;
}

void ConcreteAudioProcessor::setRootNoteForZone(int zoneIndex, int newRootNote)
{
    const auto existing = getCurrentSampleSet();
    if (!juce::isPositiveAndBelow(zoneIndex, (int) existing->zones.size()))
        return;

    ConcreteSampleSet::Ptr newSet(new ConcreteSampleSet());
    newSet->zones = existing->zones;
    newSet->zones[(size_t) zoneIndex].rootNote = newRootNote;
    publishSampleSet(newSet);
}

bool ConcreteAudioProcessor::relocateZone(int zoneIndex, const juce::File& newFile)
{
    const auto existing = getCurrentSampleSet();
    if (!juce::isPositiveAndBelow(zoneIndex, (int) existing->zones.size()))
        return false;

    auto relocated = ConcreteSampleIO::relocateZone(existing->zones[(size_t) zoneIndex], formatManager, newFile);
    if (relocated.sourceMissing)
        return false;

    ConcreteSampleSet::Ptr newSet(new ConcreteSampleSet());
    newSet->zones = existing->zones;
    newSet->zones[(size_t) zoneIndex] = relocated;
    publishSampleSet(newSet);
    return true;
}

void ConcreteAudioProcessor::publishSampleSet(ConcreteSampleSet::Ptr newSet)
{
    const juce::SpinLock::ScopedLockType lock(sampleSetLock);
    currentSampleSet = newSet;
}

ConcreteSampleSet::Ptr ConcreteAudioProcessor::getCurrentSampleSet() const
{
    const juce::SpinLock::ScopedLockType lock(sampleSetLock);
    return currentSampleSet;
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
        const auto sampleSet = getCurrentSampleSet();
        state.appendChild(ConcreteSampleIO::sampleSetToValueTree(*sampleSet, embedSamplesOverride), nullptr);

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
    publishSampleSet(ConcreteSampleIO::valueTreeToSampleSet(zonesTree, formatManager));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ConcreteAudioProcessor();
}
