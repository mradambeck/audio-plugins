#include "PluginProcessor.h"

#include <algorithm>

namespace
{
    // Factory presets: raw parameter values (the same values setValueNotifyingHost() takes after
    // normalising, not display percentages) applied in one shot when the preset is selected.
    // Captured from seven .aupreset files the user saved via a host's native preset UI (decoded
    // from each preset's embedded jucePluginState, not hand-tuned).
    const std::vector<wildjag::FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<wildjag::FactoryPreset> presets = {
            { "Black Mirror", {
                { ShieldsAudioProcessor::bandwidthHzParamID, 12684.0f },
                { ShieldsAudioProcessor::bitDepthParamID, 16.0f },
                { ShieldsAudioProcessor::bypassParamID, 0.0f },
                { ShieldsAudioProcessor::dampingParamID, 76.90000152587891f },
                { ShieldsAudioProcessor::diffusionParamID, 0.699999988079071f },
                { ShieldsAudioProcessor::dryParamID, 0.0f },
                { ShieldsAudioProcessor::feedbackParamID, 100.0f },
                { ShieldsAudioProcessor::lowCutHzParamID, 90.80000305175781f },
                { ShieldsAudioProcessor::sizeParamID, 4.0f },
                { ShieldsAudioProcessor::wetParamID, 200.0f },
                { ShieldsAudioProcessor::wobbleParamID, 14.0f },
            } },
            { "Curb", {
                { ShieldsAudioProcessor::bandwidthHzParamID, 10380.0f },
                { ShieldsAudioProcessor::bitDepthParamID, 14.40000057220459f },
                { ShieldsAudioProcessor::bypassParamID, 0.0f },
                { ShieldsAudioProcessor::dampingParamID, 40.10000228881836f },
                { ShieldsAudioProcessor::diffusionParamID, 0.320000022649765f },
                { ShieldsAudioProcessor::dryParamID, 0.0f },
                { ShieldsAudioProcessor::feedbackParamID, 58.10000228881836f },
                { ShieldsAudioProcessor::lowCutHzParamID, 44.5f },
                { ShieldsAudioProcessor::sizeParamID, 0.5299999713897705f },
                { ShieldsAudioProcessor::wetParamID, 200.0f },
                { ShieldsAudioProcessor::wobbleParamID, 20.39999961853027f },
            } },
            { "Freaks", {
                { ShieldsAudioProcessor::bandwidthHzParamID, 10200.0f },
                { ShieldsAudioProcessor::bitDepthParamID, 9.100000381469727f },
                { ShieldsAudioProcessor::bypassParamID, 0.0f },
                { ShieldsAudioProcessor::dampingParamID, 98.70000457763672f },
                { ShieldsAudioProcessor::diffusionParamID, 0.5940000414848328f },
                { ShieldsAudioProcessor::dryParamID, 0.0f },
                { ShieldsAudioProcessor::feedbackParamID, 77.30000305175781f },
                { ShieldsAudioProcessor::lowCutHzParamID, 69.69999694824219f },
                { ShieldsAudioProcessor::sizeParamID, 1.139999985694885f },
                { ShieldsAudioProcessor::wetParamID, 200.0f },
                { ShieldsAudioProcessor::wobbleParamID, 99.59999847412109f },
            } },
            { "Geeks", {
                { ShieldsAudioProcessor::bandwidthHzParamID, 2699.0f },
                { ShieldsAudioProcessor::bitDepthParamID, 14.40000057220459f },
                { ShieldsAudioProcessor::bypassParamID, 0.0f },
                { ShieldsAudioProcessor::dampingParamID, 56.0f },
                { ShieldsAudioProcessor::diffusionParamID, 0.3450000286102295f },
                { ShieldsAudioProcessor::dryParamID, 0.0f },
                { ShieldsAudioProcessor::feedbackParamID, 65.20000457763672f },
                { ShieldsAudioProcessor::lowCutHzParamID, 34.5f },
                { ShieldsAudioProcessor::sizeParamID, 1.100000023841858f },
                { ShieldsAudioProcessor::wetParamID, 200.0f },
                { ShieldsAudioProcessor::wobbleParamID, 0.0f },
            } },
            { "Scavengers Reign", {
                { ShieldsAudioProcessor::bandwidthHzParamID, 15076.0f },
                { ShieldsAudioProcessor::bitDepthParamID, 10.39999961853027f },
                { ShieldsAudioProcessor::bypassParamID, 0.0f },
                { ShieldsAudioProcessor::dampingParamID, 89.30000305175781f },
                { ShieldsAudioProcessor::diffusionParamID, 0.699999988079071f },
                { ShieldsAudioProcessor::dryParamID, 0.0f },
                { ShieldsAudioProcessor::feedbackParamID, 100.0f },
                { ShieldsAudioProcessor::lowCutHzParamID, 96.80000305175781f },
                { ShieldsAudioProcessor::sizeParamID, 1.279999971389771f },
                { ShieldsAudioProcessor::wetParamID, 200.0f },
                { ShieldsAudioProcessor::wobbleParamID, 48.90000152587891f },
            } },
            { "The Wire", {
                { ShieldsAudioProcessor::bandwidthHzParamID, 9999.0f },
                { ShieldsAudioProcessor::bitDepthParamID, 9.899999618530273f },
                { ShieldsAudioProcessor::bypassParamID, 0.0f },
                { ShieldsAudioProcessor::dampingParamID, 37.40000152587891f },
                { ShieldsAudioProcessor::diffusionParamID, 0.699999988079071f },
                { ShieldsAudioProcessor::dryParamID, 0.0f },
                { ShieldsAudioProcessor::feedbackParamID, 27.30000114440918f },
                { ShieldsAudioProcessor::lowCutHzParamID, 61.60000228881836f },
                { ShieldsAudioProcessor::sizeParamID, 2.119999885559082f },
                { ShieldsAudioProcessor::wetParamID, 200.0f },
                { ShieldsAudioProcessor::wobbleParamID, 6.400000095367432f },
            } },
            { "Twilight Zone", {
                { ShieldsAudioProcessor::bandwidthHzParamID, 5519.0f },
                { ShieldsAudioProcessor::bitDepthParamID, 16.0f },
                { ShieldsAudioProcessor::bypassParamID, 0.0f },
                { ShieldsAudioProcessor::dampingParamID, 12.5f },
                { ShieldsAudioProcessor::diffusionParamID, 0.3630000054836273f },
                { ShieldsAudioProcessor::dryParamID, 0.0f },
                { ShieldsAudioProcessor::feedbackParamID, 89.59999847412109f },
                { ShieldsAudioProcessor::lowCutHzParamID, 61.60000228881836f },
                { ShieldsAudioProcessor::sizeParamID, 2.099999904632568f },
                { ShieldsAudioProcessor::wetParamID, 200.0f },
                { ShieldsAudioProcessor::wobbleParamID, 75.90000152587891f },
            } },
        };
        return presets;
    }
}

ShieldsAudioProcessor::ShieldsAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets())
{
    diffusionParam = apvts.getRawParameterValue(diffusionParamID);
    feedbackParam = apvts.getRawParameterValue(feedbackParamID);
    sizeParam = apvts.getRawParameterValue(sizeParamID);
    dampingParam = apvts.getRawParameterValue(dampingParamID);
    bandwidthHzParam = apvts.getRawParameterValue(bandwidthHzParamID);
    lowCutHzParam = apvts.getRawParameterValue(lowCutHzParamID);
    bitDepthParam = apvts.getRawParameterValue(bitDepthParamID);
    dryParam = apvts.getRawParameterValue(dryParamID);
    wetParam = apvts.getRawParameterValue(wetParamID);
    wobbleParam = apvts.getRawParameterValue(wobbleParamID);
    bypassParam = apvts.getRawParameterValue(bypassParamID);
}

ShieldsAudioProcessor::~ShieldsAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout ShieldsAudioProcessor::createParameterLayout()
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
    // the network reaches full echo density, i.e. a slower shields.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{sizeParamID, 1},
        "Size",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.01f, 0.5f),
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("x")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + "x"; })));

    // Default 20%: tuned against the reference IRs via ../common/tools/compare_wavs.py's log-spectral-distance
    // score - higher damping settings (tried up to 65%) consistently scored WORSE, i.e. the real
    // hardware's tail is brighter for longer than the spec's "~15kHz, fairly damped" assumption
    // suggested. Swept 0-65%; the log-spectral-distance score bottomed out in the 15-25% band.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{dampingParamID, 1},
        "Treble Decay",
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
        "High EQ Cutoff",
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

    // Default 20Hz (0% - the bottom of the range): matches the pattern of every other opt-in
    // coloration control here (Wobble, etc) - the default sits at the setting that leaves the
    // signal chain unaffected, so this control changes nothing about the existing sound until a
    // player actually reaches for it.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{lowCutHzParamID, 1},
        "Low EQ Cutoff",
        juce::NormalisableRange<float>(20.0f, 200.0f, 0.1f, 0.5f),
        20.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String((int) v) + " Hz"; })));

    // Independent Dry/Wet gains (matching Caverns' convention), not a single crossfading Mix
    // knob - Wet gets headroom past unity (up to 200%) so it can be pushed louder than the dry
    // tap itself rather than topping out at matching it.
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
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        40.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    // Optional, opt-in delay-line modulation - off (0%) by default, so the plugin's core character
    // stays exactly the static/unmodulated "grainy hardware" sound it was tuned for; see
    // ShieldsFDNEngine::setWobble()'s comment for the DSP and the "How it works" README section for
    // why this exists (blurring the small-FDN resonant peaks a player who wants that has a way to,
    // without changing anyone else's default sound at all).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{wobbleParamID, 1},
        "Wobble",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1}, "Bypass", false));

    return {params.begin(), params.end()};
}

void ShieldsAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate);

    // Headroom over the host's stated block size so processBlock() never has to resize. Hosts are
    // allowed to hand over a block LARGER than samplesPerBlock (Logic's offline bounce and a
    // buffer-size change that lands before the re-prepare both do), and the previous
    // setSize(..., avoidReallocating: true) call in processBlock() only avoids a reallocation when
    // the new size still fits the existing allocation - so those cases malloc'd on the audio thread,
    // which is a priority-inversion stall exactly when the machine is already loaded. Sizing high
    // once here costs a few hundred KB and makes the audio path allocation-free.
    maxBlockSize = std::max(samplesPerBlock, 1) * blockSizeHeadroom;
    wetBuffer.setSize(2, maxBlockSize);
    prepared = true;
}

void ShieldsAudioProcessor::releaseResources() {}

// Hosts call this to clear tails between transport jumps/offline passes. The engine already had a
// reset(), but nothing was ever wired to it, so a wedged tail (see the NaN sanitising in
// ShieldsFDNEngine::processStereo) could only be cleared by deleting the plugin instance.
void ShieldsAudioProcessor::reset()
{
    engine.reset();
}

bool ShieldsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void ShieldsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    if (buffer.getNumChannels() < 2)
        return;

    // prepareToPlay() must have run: the engine's delay lines are empty until it does, and indexing
    // them divides by zero. Hosts shouldn't do this, but an early-out beats a SIGFPE if one does.
    if (! prepared)
        return;

    const auto numSamples = buffer.getNumSamples();

    // wetBuffer is sized with headroom in prepareToPlay() so this never reallocates. If a host ever
    // exceeds even that, process what fits rather than allocating on the audio thread - a truncated
    // block is a far better failure than a dropout, and the assertion catches it in a debug build.
    jassert(numSamples <= maxBlockSize);
    const auto samplesToProcess = std::min(numSamples, maxBlockSize);

    engine.setDiffusion(diffusionParam->load());
    engine.setFeedback(feedbackParam->load() * 0.01f);
    engine.setSize(sizeParam->load());
    engine.setDamping(dampingParam->load() * 0.01f);
    engine.setBandwidthHz(bandwidthHzParam->load());
    engine.setLowCutHz(lowCutHzParam->load());
    engine.setBitDepth(bitDepthParam->load());
    engine.setWobble(wobbleParam->load() * 0.01f);

    wetBuffer.copyFrom(0, 0, buffer, 0, 0, samplesToProcess);
    wetBuffer.copyFrom(1, 0, buffer, 1, 0, samplesToProcess);

    engine.processStereo(wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1), samplesToProcess);

    const auto dryGain = dryParam->load() * 0.01f;
    const auto wetGain = wetParam->load() * 0.01f;

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    const auto* wetLeft = wetBuffer.getReadPointer(0);
    const auto* wetRight = wetBuffer.getReadPointer(1);

    for (int i = 0; i < samplesToProcess; ++i)
    {
        left[i] = left[i] * dryGain + wetLeft[i] * wetGain;
        right[i] = right[i] * dryGain + wetRight[i] * wetGain;
    }
}

// createEditor() lives in PluginEditor.cpp, not here - keeps PluginProcessor.cpp (and ShieldsTests,
// which links only this file) free of any GUI/LookAndFeel/font dependency.
bool ShieldsAudioProcessor::hasEditor() const { return true; }

const juce::String ShieldsAudioProcessor::getName() const { return JucePlugin_Name; }

bool ShieldsAudioProcessor::acceptsMidi() const { return false; }
bool ShieldsAudioProcessor::producesMidi() const { return false; }
bool ShieldsAudioProcessor::isMidiEffect() const { return false; }
double ShieldsAudioProcessor::getTailLengthSeconds() const { return 8.0; }

int ShieldsAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int ShieldsAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void ShieldsAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String ShieldsAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }
void ShieldsAudioProcessor::changeProgramName(int, const juce::String&) {}

void ShieldsAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void ShieldsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ShieldsAudioProcessor();
}
