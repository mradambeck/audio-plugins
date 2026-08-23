#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

#include "KarplunkExcitation.h"
#include "KarplunkLoopFilter.h"
#include "KarplunkMonoNoteStack.h"
#include "KarplunkVoice.h"
#include "KarplunkVoiceAllocator.h"

// Extended Karplus-Strong string synth. This class owns parameter state, the voice pool, and
// MIDI dispatch only; all DSP lives in the four standalone classes composed by Voice
// (KarplunkExcitation.h, KarplunkLoopFilter.h, KarplunkStringLine.h, KarplunkVoice.h) - see those
// files for the actual algorithm and the swap seams. Voice-to-note allocation/stealing is its own
// standalone, testable class (KarplunkVoiceAllocator.h) - see that file for why it's separate.
class KarplunkAudioProcessor : public juce::AudioProcessor
{
public:
    KarplunkAudioProcessor();
    ~KarplunkAudioProcessor() override;

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

    static constexpr auto dampingParamID = "damping";
    static constexpr auto outputLevelParamID = "outputLevel";
    static constexpr auto brightnessParamID = "brightness";
    static constexpr auto bowAmountParamID = "bowAmount";
    static constexpr auto structureParamID = "structure";
    static constexpr auto positionParamID = "position";
    static constexpr auto monoParamID = "mono";
    static constexpr auto glideTimeParamID = "glideTime";
    static constexpr auto waveshapeParamID = "waveshape";
    static constexpr auto waveshaperTypeParamID = "waveshaperType";
    static constexpr auto ringModAmountParamID = "ringModAmount";
    static constexpr auto ringModFrequencyParamID = "ringModFrequency";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleMidiMessage(const juce::MidiMessage& message) noexcept;

    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* outputLevelParam = nullptr;
    std::atomic<float>* brightnessParam = nullptr;
    std::atomic<float>* bowAmountParam = nullptr;
    std::atomic<float>* structureParam = nullptr;
    std::atomic<float>* positionParam = nullptr;
    std::atomic<float>* monoParam = nullptr;
    std::atomic<float>* glideTimeParam = nullptr;
    std::atomic<float>* waveshapeParam = nullptr;
    std::atomic<float>* waveshaperTypeParam = nullptr;
    std::atomic<float>* ringModAmountParam = nullptr;
    std::atomic<float>* ringModFrequencyParam = nullptr;

    using Voice = SingleLineKarplunkVoice<NoiseExcitation, TwoPointAverageLoopFilter, LinearInterpolator>;

    // 8 voices, basic oldest-voice-stealing (see KarplunkVoiceAllocator.h) - each Voice composes
    // its three area-components by value, so this pool needed zero changes to any of the four
    // experimental-area classes, exactly as the base scaffold's architecture was designed for.
    static constexpr int numVoices = 8;
    std::array<Voice, numVoices> voices;
    KarplunkVoiceAllocator<numVoices> voiceAllocator;

    // Mono mode always uses voices[0] exclusively and drives it through this note stack instead
    // of voiceAllocator - see KarplunkMonoNoteStack.h for the last-note-priority/retrigger
    // behavior this exists for. 16 held notes is generous headroom for a human's ten fingers;
    // never allocates either way.
    KarplunkMonoNoteStack<16> monoNoteStack;

    // Read once per block (not smoothed - a Poly/Mono switch is a discrete mode change, not a
    // live-sweepable control) so a mid-note toggle can be detected and treated as an implicit
    // all-notes-off (see processBlock()) - flipping the mode while notes are held would otherwise
    // leave voiceAllocator's tags or monoNoteStack's held notes stale/inconsistent with whichever
    // mechanism is now in charge.
    bool previousMonoMode = false;

    // Fixed headroom applied to the summed voice output before Output Level, so a full chord at
    // max velocity doesn't clip harder than a single note did in the mono scaffold. A constant
    // (not activity-dependent) scale, so it doesn't itself introduce level pumping as voices
    // come and go. Mono mode only ever sounds one voice, so it uses no headroom reduction at all
    // (1.0) - applying the 8-voice headroom to a single mono voice would make mono notes sound
    // noticeably quieter than the same note played in Poly for no reason.
    static constexpr float polyHeadroomGain = 0.35355339f; // 1 / sqrt(numVoices)

    juce::SmoothedValue<float> dampingSmoothed;
    juce::SmoothedValue<float> outputLevelSmoothed;

    // Unlike Brightness (latched once at noteOn), Bow Amount is smoothed and applied to every
    // voice every sample, same pattern as Decay - bowing is a sustained, live gesture, so a
    // performer should hear the knob change in real time while a note rings.
    juce::SmoothedValue<float> bowAmountSmoothed;

    // Structure and Position are also live/every-sample, same convention as Bow Amount/Decay -
    // both are meant to be swept while a note rings, not just set before plucking.
    juce::SmoothedValue<float> structureSmoothed;
    juce::SmoothedValue<float> positionSmoothed;

    // Also live/every-sample, same convention - a Waveshape sweep should be audible in real time
    // while a note rings, not just latched at the next pluck.
    juce::SmoothedValue<float> waveshapeSmoothed;

    // Ring Mod Amount/Frequency are also live/every-sample, same convention - both are meant to be
    // swept while a note rings (a live-performance sweep of the modulator frequency is a large part
    // of ring modulation's usual character).
    juce::SmoothedValue<float> ringModAmountSmoothed;
    juce::SmoothedValue<float> ringModFrequencySmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KarplunkAudioProcessor)
};
