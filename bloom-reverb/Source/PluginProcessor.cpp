#include "PluginProcessor.h"
#include "PluginEditor.h"

BloomAudioProcessor::BloomAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    diffusionParam = apvts.getRawParameterValue(diffusionParamID);
    feedbackParam = apvts.getRawParameterValue(feedbackParamID);
    sizeParam = apvts.getRawParameterValue(sizeParamID);
    dampingParam = apvts.getRawParameterValue(dampingParamID);
    bandwidthHzParam = apvts.getRawParameterValue(bandwidthHzParamID);
    bitDepthParam = apvts.getRawParameterValue(bitDepthParamID);
    mixParam = apvts.getRawParameterValue(mixParamID);
    bypassParam = apvts.getRawParameterValue(bypassParamID);
}

BloomAudioProcessor::~BloomAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout BloomAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Centered near 0.5 per the Bloom spec - that's the diffusion coefficient sweet spot that
    // produces the slow, smooth echo-density buildup rather than either an obvious discrete
    // slap-back (low end) or metallic ringing (high end).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{diffusionParamID, 1},
        "Diffusion",
        juce::NormalisableRange<float>(0.3f, 0.7f, 0.001f),
        0.5f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2); })));

    // Default 99%: tuned against reference-irs/preset-45.wav and preset-49.wav (real Midiverb II
    // captures) - both have a ~3.5s decay tail, which needs feedback pushed close to its ceiling to
    // reproduce (85% dies out within ~1.3s, far short of the real hardware's tail).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{feedbackParamID, 1},
        "Feedback",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        99.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    // The plugin's de facto "attack time" control (per spec, no separate envelope parameter):
    // scales the FDN's delay-line lengths, so a larger Size means more samples/round-trips before
    // the network reaches full echo density, i.e. a slower bloom.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{sizeParamID, 1},
        "Size",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.01f, 0.5f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("x")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + "x"; })));

    // Default 20%: tuned against the reference IRs via tools/compare_irs.py's log-spectral-distance
    // score - higher damping settings (tried up to 65%) consistently scored WORSE, i.e. the real
    // hardware's tail is brighter for longer than the spec's "~15kHz, fairly damped" assumption
    // suggested. Swept 0-65%; the log-spectral-distance score bottomed out in the 15-25% band.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{dampingParamID, 1},
        "Damping",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        20.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    // Default 19kHz: also tuned against the reference IRs (see Damping above) - narrower bandwidths
    // scored worse, consistent with Damping's finding that these particular captures are brighter
    // than the spec's ~15kHz assumption. Left as a parameter rather than raised in the spec's own
    // range, since a different reference capture (or a future preset) may well want it darker.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{bandwidthHzParamID, 1},
        "Bandwidth",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f, 0.3f),
        19000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String((int) v) + " Hz"; })));

    // Default 13: unlike Damping/Bandwidth, a LITTLE quantization grain (not none) did measurably
    // improve the log-spectral-distance match against the reference IRs - consistent with real
    // 12-16 bit hardware having some audible grain even when otherwise fairly bright/undamped.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{bitDepthParamID, 1},
        "Bit Depth",
        juce::NormalisableRange<float>(4.0f, 16.0f, 0.1f),
        13.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("bit")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " bit"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{mixParamID, 1},
        "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        40.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1}, "Bypass", false));

    return {params.begin(), params.end()};
}

void BloomAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);
    wetBuffer.setSize(2, samplesPerBlock);
}

void BloomAudioProcessor::releaseResources() {}

bool BloomAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void BloomAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    if (buffer.getNumChannels() < 2)
        return;

    const auto numSamples = buffer.getNumSamples();

    engine.setDiffusion(diffusionParam->load());
    engine.setFeedback(feedbackParam->load() * 0.01f);
    engine.setSize(sizeParam->load());
    engine.setDamping(dampingParam->load() * 0.01f);
    engine.setBandwidthHz(bandwidthHzParam->load());
    engine.setBitDepth(bitDepthParam->load());

    wetBuffer.setSize(2, numSamples, false, false, true);
    wetBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
    wetBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);

    engine.processStereo(wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1), numSamples);

    const auto wet = mixParam->load() * 0.01f;
    const auto dry = 1.0f - wet;

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    const auto* wetLeft = wetBuffer.getReadPointer(0);
    const auto* wetRight = wetBuffer.getReadPointer(1);

    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = left[i] * dry + wetLeft[i] * wet;
        right[i] = right[i] * dry + wetRight[i] * wet;
    }
}

juce::AudioProcessorEditor* BloomAudioProcessor::createEditor()
{
    return new BloomAudioProcessorEditor(*this);
}

bool BloomAudioProcessor::hasEditor() const { return true; }

const juce::String BloomAudioProcessor::getName() const { return JucePlugin_Name; }

bool BloomAudioProcessor::acceptsMidi() const { return false; }
bool BloomAudioProcessor::producesMidi() const { return false; }
bool BloomAudioProcessor::isMidiEffect() const { return false; }
double BloomAudioProcessor::getTailLengthSeconds() const { return 8.0; }

int BloomAudioProcessor::getNumPrograms() { return 1; }
int BloomAudioProcessor::getCurrentProgram() { return 0; }
void BloomAudioProcessor::setCurrentProgram(int) {}
const juce::String BloomAudioProcessor::getProgramName(int) { return {}; }
void BloomAudioProcessor::changeProgramName(int, const juce::String&) {}

void BloomAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void BloomAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BloomAudioProcessor();
}
