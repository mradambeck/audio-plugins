#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

#include "../../common/Presets/FactoryPreset.h"

// Simple naive (non-band-limited) phase-accumulator oscillator. Aliasing at bass fundamentals
// is much less audible than at lead-register pitches, and a bit of grit from it actually suits
// this plugin's industrial/gritty character - band-limiting (BLEP/BLIT) is deliberately not
// worth the complexity here.
enum class AlloyOscWaveform
{
    saw,
    square,
    triangle,
    sine
};

class AlloyPhaseOscillator
{
public:
    void setFrequency(float hz, double sampleRate) noexcept
    {
        increment = hz / (float) sampleRate;
    }

    void resetPhase(float startPhase) noexcept { phase = startPhase; }

    float renderAndAdvance(AlloyOscWaveform waveform) noexcept
    {
        return renderAndAdvanceWithPhaseOffset(waveform, 0.0f);
    }

    // Phase modulation (not true frequency modulation) - offsetting the read phase by another
    // oscillator's output each sample is what DX7-style FM synths actually implement under the
    // hood; it's numerically simpler than true FM and sounds effectively identical for the
    // sine-ish carriers this is used with.
    float renderAndAdvanceWithPhaseOffset(AlloyOscWaveform waveform, float phaseOffset) noexcept
    {
        auto readPhase = phase + phaseOffset;
        readPhase -= std::floor(readPhase);

        const auto value = waveformValue(waveform, readPhase);

        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return value;
    }

private:
    static float waveformValue(AlloyOscWaveform waveform, float p) noexcept
    {
        switch (waveform)
        {
            case AlloyOscWaveform::saw:      return 2.0f * p - 1.0f;
            case AlloyOscWaveform::square:   return p < 0.5f ? 1.0f : -1.0f;
            case AlloyOscWaveform::triangle: return 4.0f * std::abs(p - 0.5f) - 1.0f;
            case AlloyOscWaveform::sine:     return std::sin(juce::MathConstants<float>::twoPi * p);
            default:                         return 0.0f;
        }
    }

    float phase = 0.0f;
    float increment = 0.0f;
};

class AlloyAudioProcessor : public juce::AudioProcessor
{
public:
    AlloyAudioProcessor();
    ~AlloyAudioProcessor() override;

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

    static const juce::StringArray& getWaveformChoices();
    static const juce::StringArray& getSubWaveformChoices();
    static const juce::StringArray& getFmWaveformChoices();
    static const juce::StringArray& getOctaveChoices();
    static const juce::StringArray& getSubOctaveChoices();
    static const juce::StringArray& getUnisonChoices();

    static constexpr auto analogWaveformParamID = "analogWaveform";
    static constexpr auto analogOctaveParamID = "analogOctave";
    static constexpr auto analogUnisonParamID = "analogUnison";
    static constexpr auto analogDetuneParamID = "analogDetune";
    static constexpr auto analogFilterCutoffParamID = "analogFilterCutoff";
    static constexpr auto analogFilterResonanceParamID = "analogFilterResonance";
    static constexpr auto analogFilterEnvAmountParamID = "analogFilterEnvAmount";
    static constexpr auto analogVelocityToFilterParamID = "analogVelocityToFilter";
    static constexpr auto analogFilterAttackParamID = "analogFilterAttack";
    static constexpr auto analogFilterDecayParamID = "analogFilterDecay";
    static constexpr auto analogFilterSustainParamID = "analogFilterSustain";
    static constexpr auto analogFilterReleaseParamID = "analogFilterRelease";
    static constexpr auto analogAmpAttackParamID = "analogAmpAttack";
    static constexpr auto analogAmpDecayParamID = "analogAmpDecay";
    static constexpr auto analogAmpSustainParamID = "analogAmpSustain";
    static constexpr auto analogAmpReleaseParamID = "analogAmpRelease";
    static constexpr auto analogGlideTimeParamID = "analogGlideTime";
    static constexpr auto analogVolumeParamID = "analogVolume";

    static constexpr auto subEnabledParamID = "subEnabled";
    static constexpr auto subWaveformParamID = "subWaveform";
    static constexpr auto subOctaveParamID = "subOctave";
    static constexpr auto subVolumeParamID = "subVolume";

    static constexpr auto fmCarrierWaveformParamID = "fmCarrierWaveform";
    static constexpr auto fmCarrierOctaveParamID = "fmCarrierOctave";
    static constexpr auto fmCarrierVolumeParamID = "fmCarrierVolume";
    static constexpr auto fmVelocityToCarrierParamID = "fmVelocityToCarrier";
    static constexpr auto fmCarrierAttackParamID = "fmCarrierAttack";
    static constexpr auto fmCarrierDecayParamID = "fmCarrierDecay";
    static constexpr auto fmCarrierSustainParamID = "fmCarrierSustain";
    static constexpr auto fmCarrierReleaseParamID = "fmCarrierRelease";

    static constexpr auto fmModulatorWaveformParamID = "fmModulatorWaveform";
    static constexpr auto fmModulatorOctaveParamID = "fmModulatorOctave";
    static constexpr auto fmModulatorVolumeParamID = "fmModulatorVolume";
    static constexpr auto fmVelocityToBrightnessParamID = "fmVelocityToBrightness";
    static constexpr auto fmModulatorBrightnessParamID = "fmModulatorBrightness";
    static constexpr auto fmModulatorAttackParamID = "fmModulatorAttack";
    static constexpr auto fmModulatorDecayParamID = "fmModulatorDecay";
    static constexpr auto fmModulatorSustainParamID = "fmModulatorSustain";
    static constexpr auto fmModulatorReleaseParamID = "fmModulatorRelease";

    static constexpr auto arpEnabledParamID = "arpEnabled";
    static constexpr auto arpSyncParamID = "arpSync";
    static constexpr auto arpDivisionParamID = "arpDivision";
    static constexpr auto arpRateParamID = "arpRate";
    static constexpr auto arpPatternParamID = "arpPattern";
    static constexpr auto arpOctaveRangeParamID = "arpOctaveRange";
    static constexpr auto arpGateParamID = "arpGate";
    static constexpr auto arpHoldParamID = "arpHold";

    static constexpr auto mixDriveParamID = "mixDrive";
    static constexpr auto mixToneParamID = "mixTone";
    static constexpr auto mixOutputParamID = "mixOutput";
    static constexpr auto mixAgeParamID = "mixAge";

    static const juce::StringArray& getArpDivisionChoices();
    static const juce::StringArray& getArpPatternChoices();
    static const juce::StringArray& getArpOctaveRangeChoices();

    // Called from the UI's Panic button (message thread). Only sets an atomic flag - heldNotes,
    // gateOn, and the ADSR objects are audio-thread-owned state, so the actual silence-everything
    // work happens in processBlock (see the flag check at its top) rather than here, to avoid a
    // data race between the two threads.
    void requestPanic() noexcept { panicRequested.store(true, std::memory_order_relaxed); }

    // Test/tooling-only - ageNoiseRandom (see its own member comment) is otherwise seeded
    // randomly from system entropy/time, so offline renders (AlloyRenderIR) can't reproduce a
    // bit-identical Age sequence across separate process invocations without this. Never called
    // from the live audio path; production behavior (a different, non-repeating drift/warble
    // pattern each time the plugin loads - the correct behavior for modeling analog drift) is
    // unchanged.
    void setAgeSeedForTesting(juce::int64 seed) noexcept { ageNoiseRandom.setSeed(seed); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void handleMidiMessage(const juce::MidiMessage& message);
    float frequencyForNote(int midiNoteNumber, int octaveShift) const noexcept;
    double getCurrentBpm() const;

    // Memoizes noteToHz() (a free function in PluginProcessor.cpp's anonymous namespace) against
    // the caller's own persistent (lastNote, lastHz) cache slot - skips the pow() inside noteToHz()
    // when `note` hasn't changed since the last call through THIS slot. Four independent call
    // sites in processBlock (base/sub/FM-modulator/FM-carrier pitch) each pass their own pair of
    // member floats, since each combines currentSemitone with a different (also block-constant)
    // octave-shift term - a change in any of those, not just currentSemitone itself, must still
    // invalidate the cache, which comparing the full combined `note` argument handles correctly.
    float cachedNoteToHz(float note, float& lastNote, float& lastHz) const noexcept;

    // Shared by both the raw-MIDI path (arp off) and the arpeggiator engine (arp on) - the
    // difference between them is forceRetrigger: raw MIDI legatos between still-held notes
    // (forceRetrigger=false lets a note-on just re-pitch an already-sounding voice), but every
    // arp step is a fresh pluck regardless of what came before.
    void triggerVoiceNoteOn(int midiNoteNumber, float velocity, bool forceRetrigger);
    void triggerVoiceNoteOff();

    // Rebuilds the arp's current step pool (held notes expanded across the octave range, in
    // whatever order Pattern calls for) from whichever note list is authoritative right now -
    // heldNotes normally, or the latched chord while Hold is engaged. Returns a reference to the
    // persistent arpScratchResultPool member (not by value) - called every arp step from inside
    // processBlock's per-sample loop, so it writes into reusable scratch buffers instead of
    // allocating fresh vectors each call; not const because of that. heldNotes/latchedNotes
    // themselves are only ever read here, never used as scratch space.
    const std::vector<int>& buildArpNotePool();
    void advanceArpStep();

    std::atomic<float>* analogWaveformParam = nullptr;
    std::atomic<float>* analogOctaveParam = nullptr;
    std::atomic<float>* analogUnisonParam = nullptr;
    std::atomic<float>* analogDetuneParam = nullptr;
    std::atomic<float>* analogFilterCutoffParam = nullptr;
    std::atomic<float>* analogFilterResonanceParam = nullptr;
    std::atomic<float>* analogFilterEnvAmountParam = nullptr;
    std::atomic<float>* analogVelocityToFilterParam = nullptr;
    std::atomic<float>* analogFilterAttackParam = nullptr;
    std::atomic<float>* analogFilterDecayParam = nullptr;
    std::atomic<float>* analogFilterSustainParam = nullptr;
    std::atomic<float>* analogFilterReleaseParam = nullptr;
    std::atomic<float>* analogAmpAttackParam = nullptr;
    std::atomic<float>* analogAmpDecayParam = nullptr;
    std::atomic<float>* analogAmpSustainParam = nullptr;
    std::atomic<float>* analogAmpReleaseParam = nullptr;
    std::atomic<float>* analogGlideTimeParam = nullptr;
    std::atomic<float>* analogVolumeParam = nullptr;

    std::atomic<float>* subEnabledParam = nullptr;
    std::atomic<float>* subWaveformParam = nullptr;
    std::atomic<float>* subOctaveParam = nullptr;
    std::atomic<float>* subVolumeParam = nullptr;

    std::atomic<float>* fmCarrierWaveformParam = nullptr;
    std::atomic<float>* fmCarrierOctaveParam = nullptr;
    std::atomic<float>* fmCarrierVolumeParam = nullptr;
    std::atomic<float>* fmVelocityToCarrierParam = nullptr;
    std::atomic<float>* fmCarrierAttackParam = nullptr;
    std::atomic<float>* fmCarrierDecayParam = nullptr;
    std::atomic<float>* fmCarrierSustainParam = nullptr;
    std::atomic<float>* fmCarrierReleaseParam = nullptr;

    std::atomic<float>* fmModulatorWaveformParam = nullptr;
    std::atomic<float>* fmModulatorOctaveParam = nullptr;
    std::atomic<float>* fmModulatorVolumeParam = nullptr;
    std::atomic<float>* fmVelocityToBrightnessParam = nullptr;
    std::atomic<float>* fmModulatorBrightnessParam = nullptr;
    std::atomic<float>* fmModulatorAttackParam = nullptr;
    std::atomic<float>* fmModulatorDecayParam = nullptr;
    std::atomic<float>* fmModulatorSustainParam = nullptr;
    std::atomic<float>* fmModulatorReleaseParam = nullptr;

    std::atomic<float>* arpEnabledParam = nullptr;
    std::atomic<float>* arpSyncParam = nullptr;
    std::atomic<float>* arpDivisionParam = nullptr;
    std::atomic<float>* arpRateParam = nullptr;
    std::atomic<float>* arpPatternParam = nullptr;
    std::atomic<float>* arpOctaveRangeParam = nullptr;
    std::atomic<float>* arpGateParam = nullptr;
    std::atomic<float>* arpHoldParam = nullptr;

    std::atomic<float>* mixDriveParam = nullptr;
    std::atomic<float>* mixToneParam = nullptr;
    std::atomic<float>* mixOutputParam = nullptr;
    std::atomic<float>* mixAgeParam = nullptr;

    // See common/Presets/FactoryPreset.h - getNumPrograms()/getCurrentProgram()/setCurrentProgram()/
    // getProgramName() above just forward to this.
    wildjag::FactoryPresetList factoryPresets;
    double sampleRateHz = 44100.0;

    // ---- Mono note-stack voice architecture (not juce::Synthesiser - this instrument is
    // deliberately monophonic, matching the real SH-101 and how industrial basslines are
    // played). Last-note-priority: a new note while others are still held only re-pitches
    // (with glide) rather than retriggering the envelopes; envelopes only fire on the
    // silence -> sounding transition, and release only when the stack empties entirely. ----
    std::vector<int> heldNotes;
    bool gateOn = false;
    int currentMidiNote = 60;
    float currentVelocity = 1.0f;

    // ---- Arpeggiator: reuses the same mono voice above rather than driving anything of its
    // own - when on, it's the sole thing calling triggerVoiceNoteOn/Off, in place of the raw
    // MIDI handling in handleMidiMessage(). heldNotes above remains the physical key state
    // either way; latchedNotes is only populated/consulted while Hold is engaged. ----
    std::vector<int> latchedNotes;

    // buildArpNotePool()'s persistent scratch buffers - see that method's own comment. Never
    // reused as the authoritative note-list state; heldNotes/latchedNotes above are separate.
    std::vector<int> arpScratchBaseNotes;
    std::vector<int> arpScratchUpPool;
    std::vector<int> arpScratchResultPool;
    bool wasHoldEnabled = false;

    int arpStepSampleCounter = 0;
    int arpStepIndex = 0;
    bool arpGateIsOpen = false;

    // When Sync is on and the host provides a PPQ position, step timing is derived from that
    // position each block instead of from arpStepSampleCounter - phase-locks steps to the host's
    // beat grid (arpStepSampleCounter alone only matches tempo, not where "beat 1" actually is,
    // since it free-runs from whenever prepareToPlay/arp-enable happened to occur).
    juce::int64 arpSyncedStepNumber = 0;
    bool arpSyncedStepValid = false;

    std::atomic<bool> panicRequested { false };

    // Glide smooths in semitone space (not Hz directly) so the pitch ramp sounds musically
    // even rather than front- or back-loaded - Hz-per-semitone isn't constant across the
    // keyboard, semitones-per-semitone is. Deliberately not juce::SmoothedValue here: its
    // reset(sampleRate, time) snaps current to target (it's meant to be called once, in
    // prepareToPlay), but Glide Time is a live-adjustable parameter that has to be re-read
    // every block - so this is a hand-rolled one-pole glide instead, with the coefficient
    // recomputed from the current Glide Time value each block and applied every sample
    // regardless of when the target last changed.
    float currentSemitone = 60.0f;
    float glideTargetSemitone = 60.0f;

    // cachedNoteToHz() cache slots for the four per-sample pitch calls in processBlock (base
    // analog voice, sub, FM modulator, FM carrier) - -1.0e6f is a sentinel far outside any
    // plausible combined semitone+octave-shift argument, forcing the first call through each slot
    // to always compute fresh.
    float lastBaseNoteArg = -1.0e6f, lastBaseNoteHz = 0.0f;
    float lastSubNoteArg = -1.0e6f, lastSubNoteHz = 0.0f;
    float lastFmModulatorNoteArg = -1.0e6f, lastFmModulatorNoteHz = 0.0f;
    float lastFmCarrierNoteArg = -1.0e6f, lastFmCarrierNoteHz = 0.0f;

    static constexpr int maxUnisonVoices = 4;
    std::array<AlloyPhaseOscillator, maxUnisonVoices> analogOscillators;
    AlloyPhaseOscillator subOscillator;

    // ---- Age (Mix section): subtle pitch instability on the Analog VCO only (not Sub, not FM) -
    // a slow wandering "drift" component and a faster "warble" component, each a one-pole lowpass
    // of white noise at a different cutoff (see ageDriftCutoffHz/ageWarbleCutoffHz in the .cpp) so
    // they wander smoothly rather than jittering sample-to-sample. One shared value drifts all
    // unison voices together, on top of their existing relative detune spread. Deliberately left
    // at its default (entropy/time-seeded, non-reproducible between loads) rather than seeded
    // explicitly - a different, non-repeating wander pattern each time is the correct behavior
    // for modeling analog drift. See setAgeSeedForTesting() for the offline-render-only override. ----
    juce::Random ageNoiseRandom;
    float ageDriftState = 0.0f;
    float ageWarbleState = 0.0f;

    // Fixed (not independently controllable) high shelf tied to the Sub oscillator, engaged only
    // while its Waveform is Square - see subShelfHz/Q/GainDb in the .cpp.
    juce::dsp::IIR::Filter<float> subShelfFilter;

    juce::dsp::StateVariableTPTFilter<float> analogFilter;
    juce::ADSR analogFilterEnv;
    juce::ADSR analogAmpEnv;

    // Cache guard for analogFilter.setCutoffFrequency() (see its call site in processBlock) - -1.0f
    // sentinel is provably impossible for either (ADSR values are always in [0,1]; velocity is
    // always >= 0), so the very first sample always computes fresh.
    float lastFilterEnvValue = -1.0f;
    float lastCutoffVelocity = -1.0f;

    // ---- FM Bass: 2-op phase modulation, summed with the analog+sub layer at the very end
    // (not routed through analogFilter - it's a separate voice sharing only the mono
    // note-stack's pitch/gate, per the plan's two-layer design). ----
    AlloyPhaseOscillator fmCarrierOscillator;
    AlloyPhaseOscillator fmModulatorOscillator;
    juce::ADSR fmCarrierEnv;
    juce::ADSR fmModulatorEnv;

    // Smooths the Modulator envelope before it scales phase-modulation depth. The envelope's
    // segment-to-segment slope kinks (attack peak -> decay start, decay end -> sustain) are
    // inaudible on a plain amplitude envelope, but here they drive the carrier's *phase offset* -
    // since instantaneous frequency is the derivative of phase, a sharp kink shows up as a brief
    // frequency jump (an audible pop), worse the shorter/steeper Modulator Decay is. A few
    // milliseconds of one-pole smoothing removes that without softening the envelope's shape.
    float fmModulatorEnvSmoothed = 0.0f;

    // ---- Mix bus: Drive + Tone applied to the combined analog+sub+FM signal, then Output
    // trim, then a fixed (not user-controllable) safety soft-clip - see mixSafetyDrive in the
    // .cpp for why, same philosophy as Flux's fixed feedbackSafetyDrive. ----
    juce::dsp::StateVariableTPTFilter<float> mixToneFilter;

    // Fixed peaking cut tied to the Tone knob (not independently controllable) - deepens as Tone
    // moves toward 0% (left), on top of mixToneFilter's own low-pass. See mixToneEqHz/Q/MaxCutDb.
    juce::dsp::IIR::Filter<float> mixToneEqFilter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlloyAudioProcessor)
};
