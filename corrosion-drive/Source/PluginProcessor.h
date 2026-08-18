#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class CorrosionAudioProcessor : public juce::AudioProcessor
{
public:
    CorrosionAudioProcessor();
    ~CorrosionAudioProcessor() override;

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

    static constexpr auto driveParamID = "drive";
    static constexpr auto toneParamID = "tone";
    static constexpr auto biasParamID = "bias";
    static constexpr auto characterParamID = "character";
    static constexpr auto outputParamID = "output";
    static constexpr auto dryParamID = "dry";
    static constexpr auto compParamID = "comp";
    static constexpr auto bypassParamID = "bypass";
    static constexpr auto rectBlendParamID = "rectBlend";
    static constexpr auto rectMixParamID = "rectMix";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateToneFilter();

    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    // Used to read Color's own NormalisableRange (respecting its skew) so the companion EQ dip's
    // gain can be driven by the knob's actual rotation, not a naive linear reading of its Hz value.
    juce::RangedAudioParameter* toneRangedParam = nullptr;
    std::atomic<float>* biasParam = nullptr;
    // Blends the waveshaper between plain tanh (0, soft) and a harder-kneed tanh variant (1,
    // diode-pair-like) -- see the constant/comment next to characterHardnessMultiplier in the .cpp.
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* compParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* rectBlendParam = nullptr;
    std::atomic<float>* rectMixParam = nullptr;

    // Holds an unprocessed copy of the input each block so the Dry knob can blend it back in
    // alongside the driven/waveshaped Wet signal. Sized once in prepareToPlay, not per block.
    juce::AudioBuffer<float> dryBuffer;

    // Comp blends dryBuffer above between its plain and compressed forms -- this never touches
    // the signal that continues into the drive stage, only the parallel copy the Dry knob blends
    // back in. See the fixed settings comment next to prepare() in the .cpp.
    juce::dsp::Compressor<float> dryCompressor;

    // Holds a copy of dryBuffer from just before compression, so Comp can blend the compressed
    // result back against it -- same pattern as rectDryBuffer below, one stage over.
    juce::AudioBuffer<float> compDryBuffer;

    // Holds a copy of the signal from just before the Rect stage, so Rect Mix can blend the
    // rectified result back against it -- separate from dryBuffer above, which captures the
    // signal before the drive stage runs, much earlier in the chain.
    juce::AudioBuffer<float> rectDryBuffer;

    using IIRFilterDuplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                                 juce::dsp::IIR::Coefficients<float>>;
    IIRFilterDuplicator toneFilter;

    // A peaking EQ dip at 286Hz, Q 0.71, tied to the Color knob: full -7dB cut at Color's
    // leftmost (darkest) position, fading to 0dB (no effect) at its rightmost. Coefficients are
    // recomputed alongside toneFilter's in updateToneFilter().
    IIRFilterDuplicator colorDipFilter;

    // A companion peaking EQ boost at 5300Hz, Q 0.57, also tied to the Color knob -- but running
    // the opposite direction from colorDipFilter: 0dB (no effect) at Color's leftmost, rising to
    // +2.8dB at its rightmost. Coefficients recomputed alongside the others in updateToneFilter().
    IIRFilterDuplicator colorPresenceFilter;

    // A third Color-tied peaking EQ at 4320Hz, Q 0.41 -- same direction as colorDipFilter: 0dB at
    // Color's rightmost, rising to +3dB at its leftmost. Coefficients recomputed in updateToneFilter().
    IIRFilterDuplicator colorMidPeakFilter;

    // A fourth Color-tied peaking EQ at 186Hz, Q 0.6 -- same direction as colorDipFilter: full
    // -3dB cut at Color's leftmost, fading to 0dB (no effect) at its rightmost. Coefficients
    // recomputed alongside the others in updateToneFilter().
    IIRFilterDuplicator colorLowDipFilter;

    // A fifth Color-tied band, this time a high shelf (not a peak like the four above) at 4kHz,
    // Q 0.4 -- same direction as colorDipFilter: 0dB at Color's rightmost, rising to +6.5dB at
    // its leftmost. Coefficients recomputed alongside the others in updateToneFilter().
    IIRFilterDuplicator colorHighShelfFilter;

    // Removes the DC offset introduced by biasing the waveshaper asymmetrically for warmth.
    IIRFilterDuplicator dcBlocker;

    // Rectification (full/half-wave) shifts the signal's average sharply upward, independently
    // of the drive-stage bias above -- needs its own DC blocker, separate from dcBlocker's.
    IIRFilterDuplicator rectDcBlocker;

    // A single, feedback-free echo tied to the Color knob: full (but subtle) mix at Color's
    // leftmost/darkest position, fading to silent at its rightmost. Only ever fed the dry input
    // (never its own output), so it's one slap, not a repeating trail. See processBlock().
    juce::dsp::DelayLine<float> slapbackDelay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorrosionAudioProcessor)
};
