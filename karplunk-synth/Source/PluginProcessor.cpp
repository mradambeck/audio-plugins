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
    structureParam = apvts.getRawParameterValue(structureParamID);
    positionParam = apvts.getRawParameterValue(positionParamID);
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

    return { params.begin(), params.end() };
}

void KarplunkAudioProcessor::prepareToPlay(double sampleRate, int)
{
    for (auto& v : voices)
        v.prepare(sampleRate);
    voiceAllocator.reset();

    dampingSmoothed.reset(sampleRate, smoothingRampSeconds);
    dampingSmoothed.setCurrentAndTargetValue(dampingParam->load());

    outputLevelSmoothed.reset(sampleRate, smoothingRampSeconds);
    outputLevelSmoothed.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(outputLevelParam->load()));

    bowAmountSmoothed.reset(sampleRate, smoothingRampSeconds);
    bowAmountSmoothed.setCurrentAndTargetValue(bowAmountParam->load());

    structureSmoothed.reset(sampleRate, smoothingRampSeconds);
    structureSmoothed.setCurrentAndTargetValue(structureParam->load());

    positionSmoothed.reset(sampleRate, smoothingRampSeconds);
    positionSmoothed.setCurrentAndTargetValue(positionParam->load());
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
        std::array<bool, numVoices> isActive{};
        for (int i = 0; i < numVoices; ++i)
            isActive[(size_t) i] = voices[(size_t) i].isActive();

        const auto note = message.getNoteNumber();
        const auto voiceIndex = voiceAllocator.allocateVoiceForNoteOn(note, isActive);

        voices[(size_t) voiceIndex].setBrightness(brightnessParam->load());
        voices[(size_t) voiceIndex].noteOn(note, message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        const auto voiceIndex = voiceAllocator.findVoiceForNoteOff(message.getNoteNumber());
        if (voiceIndex >= 0)
            voices[(size_t) voiceIndex].noteOff();
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        for (auto& v : voices)
            v.reset();
        voiceAllocator.reset();
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
    structureSmoothed.setTargetValue(structureParam->load());
    positionSmoothed.setTargetValue(positionParam->load());

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
        const auto structure = structureSmoothed.getNextValue();
        const auto position = positionSmoothed.getNextValue();

        float mixedSample = 0.0f;
        for (auto& v : voices)
        {
            v.setDamping(damping);
            v.setBowAmount(bowAmount);
            v.setStructure(structure);
            v.setPosition(position);
            mixedSample += v.renderNextSample();
        }

        const auto out = mixedSample * polyHeadroomGain * outputLevelSmoothed.getNextValue();
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
