#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class DamageAudioProcessor : public juce::AudioProcessor
{
public:
    DamageAudioProcessor();
    ~DamageAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Current input level as seen by the gate's envelope follower, in dB - read by the editor
    // to draw a live level indicator against the gate threshold. Updated on the audio thread,
    // read on the message thread, so it's a plain atomic rather than something requiring a lock.
    float getGateLevelDb() const noexcept { return gateLevelDb.load(std::memory_order_relaxed); }

    static constexpr auto gateParamID = "gate";
    static constexpr auto driveParamID = "drive";
    static constexpr auto widthParamID = "width";
    static constexpr auto highPassParamID = "highPass";
    static constexpr auto lowPassParamID = "lowPass";
    static constexpr auto dryParamID = "dry";
    static constexpr auto bypassParamID = "bypass";
    static constexpr auto squareParamID = "square";
    static constexpr auto oscillateParamID = "oscillate";
    static constexpr auto oscFreqParamID = "oscFreq";
    static constexpr auto wetParamID = "wet";
    static constexpr auto slowReleaseParamID = "slowRelease";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateFilters();

    int currentProgramIndex = 0;

    std::atomic<float>* gateParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* highPassParam = nullptr;
    // Used to read Hi Pass's own NormalisableRange (respecting its skew) so the companion EQ
    // dip's gain can be driven by the knob's actual rotation, not a naive linear reading of its
    // Hz value.
    juce::RangedAudioParameter* highPassRangedParam = nullptr;
    std::atomic<float>* lowPassParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* squareParam = nullptr;
    std::atomic<float>* oscillateParam = nullptr;
    std::atomic<float>* oscFreqParam = nullptr;
    std::atomic<float>* wetParam = nullptr;
    std::atomic<float>* slowReleaseParam = nullptr;

    // Gate ballistics: the envelope follower tracks input level with its own attack/release,
    // and a hysteresis gap between the open and close thresholds stops the gate re-triggering
    // on ripple right at the threshold. The gain itself is smoothed on top of that so the gate
    // fades rather than snaps - together these are what stop gating from sounding like a buzz
    // instead of a clean mute.
    float gateEnvelope = 0.0f;
    float gateGain = 1.0f;
    bool gateOpen = true;

    float gateEnvelopeAttackCoeff = 0.0f;
    float gateEnvelopeReleaseCoeff = 0.0f;
    float gateGainAttackCoeff = 0.0f;
    float gateGainReleaseCoeff = 0.0f;

    // Alternate, longer release coefficients selected instead of the pair above when the
    // "Slow Release" button is on, for a gentler fade-out than the default gate ballistics.
    float gateEnvelopeReleaseCoeffSlow = 0.0f;
    float gateGainReleaseCoeffSlow = 0.0f;

    juce::HeapBlock<float> gateGainBuffer;

    std::atomic<float> gateLevelDb { -100.0f };

    // A second, independent pulse wave whose own frequency is pushed around by the input signal's
    // playing dynamics (FM - see fmModulationIndex in PluginProcessor.cpp), then used to chop the
    // fuzzed signal's level at whatever rate that FM lands on. It's deliberately unipolar (0/1)
    // rather than bipolar (+1/-1): flipping the signal's polarity multiplies it against another
    // waveform and the product is heard as a new pitch of its own (sum/difference tones unrelated
    // to the input). The chop depth is then limited (see oscChopFloor) so the dip never reaches
    // full silence - at audio-rate oscillator frequencies a full 0/1 swing is itself a loud square
    // wave at that frequency, heard as an added note rather than a texture on the input.
    float oscPhase = 0.0f;
    juce::HeapBlock<float> oscBuffer;

    // Holds the untouched input for the Dry/Wet controls, captured before the gate/fuzz/filter
    // chain runs so Dry can mix in the clean signal independently of how the Wet chain sounds.
    juce::AudioBuffer<float> dryBuffer;

    // A second, faster gate dedicated to the oscillator, tracking the same input level as the
    // main gate above but with much shorter release times. The main gate's release is tuned to
    // fade a note out musically; left in charge of the oscillator too, that same slow tail let
    // the oscillator's chop keep audibly ringing well after a note had already died away. This
    // one closes quickly behind the input instead, so the oscillator stops with the note.
    float oscGateEnvelope = 0.0f;
    float oscGateGain = 1.0f;
    bool oscGateOpen = true;

    float oscGateEnvelopeAttackCoeff = 0.0f;
    float oscGateEnvelopeReleaseCoeff = 0.0f;
    float oscGateGainAttackCoeff = 0.0f;
    float oscGateGainReleaseCoeff = 0.0f;

    juce::HeapBlock<float> oscGateBuffer;

    using IIRFilterDuplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                                 juce::dsp::IIR::Coefficients<float>>;
    // Chained in series (high-pass then low-pass) to form an adjustable band-pass window over the
    // fuzzed signal, replacing the single low-pass-only Tone control.
    IIRFilterDuplicator highPassFilter;
    IIRFilterDuplicator lowPassFilter;

    // A peaking EQ dip at 286Hz, Q 0.25, tied to Hi Pass: full -3dB cut at Hi Pass's leftmost
    // (fully open, 20Hz) position, fading to 0dB (no effect) at its rightmost (2000Hz).
    // Coefficients recomputed alongside the other filters' in updateFilters().
    IIRFilterDuplicator highPassDipFilter;

    // Removes the DC offset produced by comparing the driven signal against an off-centre
    // threshold to set the pulse's duty cycle — that asymmetry biases the output up or down.
    IIRFilterDuplicator dcBlocker;

    // A subtle chorus applied only to the captured dry tap, not the fuzzed wet signal. Its mix
    // is driven by the Drive knob (see dryChorusMaxMix in PluginProcessor.cpp) rather than being
    // a control of its own, so the dry signal gains a slow, gentle movement as Drive is pushed
    // harder, and sits perfectly still when Drive is at its minimum.
    juce::dsp::Chorus<float> dryChorus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DamageAudioProcessor)
};
