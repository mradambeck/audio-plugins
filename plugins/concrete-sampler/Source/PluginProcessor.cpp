#include "PluginProcessor.h"

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
}

ConcreteAudioProcessor::ConcreteAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets())
{
}

ConcreteAudioProcessor::~ConcreteAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout ConcreteAudioProcessor::createParameterLayout()
{
    // No parameters yet - Phase 1 introduces sample-zone/playback controls (root note, tune,
    // level, pan) and Phase 2 onward add the pitch engine/quantizer/filter/capture-pass controls.
    // See concrete-sampler-plugin-plan.md.
    return {};
}

void ConcreteAudioProcessor::prepareToPlay(double, int) {}
void ConcreteAudioProcessor::releaseResources() {}

bool ConcreteAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Main output only, mono or stereo - matches every other instrument/effect in this catalog
    // (see AGENTS.md's mono/stereo fix, commit 6230d6d). Aux buses are deliberately not declared
    // in v1 (see the plan's Architecture #1 / Decisions), but every voice will route through a
    // destination-index indirection from Phase 1 onward specifically so adding them later doesn't
    // touch voice code - this bus declaration is the only part that will need to change.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ConcreteAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // No sample zones or voices yet (Phase 1) - output is silence until then.
    buffer.clear();
}

bool ConcreteAudioProcessor::hasEditor() const { return true; }

const juce::String ConcreteAudioProcessor::getName() const { return JucePlugin_Name; }

bool ConcreteAudioProcessor::acceptsMidi() const { return true; }
bool ConcreteAudioProcessor::producesMidi() const { return false; }
bool ConcreteAudioProcessor::isMidiEffect() const { return false; }

// 0 until Phase 1 gives voices a real amp envelope/release tail to report - never sounds yet, so
// there is nothing to keep this processor alive for after the last note.
double ConcreteAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ConcreteAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int ConcreteAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void ConcreteAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String ConcreteAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }
void ConcreteAudioProcessor::changeProgramName(int, const juce::String&) {}

void ConcreteAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void ConcreteAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ConcreteAudioProcessor();
}
