#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

// A fixed, non-ADSR amplitude envelope modeling the Kurzweil K250's dual-VCA "contoured" playback
// shape (see concrete-sampler-plugin-plan.md's Phase 6 and the K250's machine-table entry: "The
// CEM3335 chips are dual VCAs doing amplitude contouring, not filtering. Use the contoured
// envelope mode here."). A generic ADSR holds a flat sustain level for as long as a note is held;
// this instead keeps decaying the whole time a note is held, in two stages (a quick initial dip
// followed by a much slower tail), the way a struck/plucked acoustic source's amplitude actually
// behaves - the real K250 continuously reshapes level rather than gating a fixed one.
//
// Fixed-shape by design (no APVTS knobs), matching every other voice envelope in this codebase -
// see ConcreteVoice.cpp's own comment on why its ADSRs are fixed-parameter for now. ConcreteVoice
// selects between this and its ADSR via ampEnvelopeMode; only that selector is exposed live, not
// this class's own stage timings.
class ConcreteContourEnvelope
{
public:
    void setSampleRate(double sampleRateIn) noexcept { sampleRate = juce::jmax(1.0, sampleRateIn); }

    void noteOn() noexcept
    {
        stage = Stage::attack;
        level = 0.0f;
    }

    // Matches juce::ADSR's noteOff(): begins a release fade from whatever level the contour has
    // decayed to so far, rather than jumping from a flat sustain the way a generic ADSR's release
    // would. ConcreteVoice is responsible for not calling this at all on a one-shot zone's ordinary
    // note-off, same as it already does for the ADSR path (see ConcreteVoice::stopNote()).
    void noteOff() noexcept
    {
        if (stage != Stage::idle)
            stage = Stage::release;
    }

    // Matches juce::ADSR's reset(): silences immediately, no release fade.
    void reset() noexcept
    {
        stage = Stage::idle;
        level = 0.0f;
    }

    bool isActive() const noexcept { return stage != Stage::idle; }

    float getNextSample() noexcept
    {
        switch (stage)
        {
            case Stage::idle:
                return 0.0f;

            case Stage::attack:
                level += attackStep();
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay1;
                }
                return level;

            case Stage::decay1:
                level *= decay1Coeff();
                if (level <= decay1FloorLevel)
                {
                    level = decay1FloorLevel;
                    stage = Stage::decay2;
                }
                return level;

            case Stage::decay2:
                level *= decay2Coeff();
                if (level <= silenceFloor)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                return level;

            case Stage::release:
            default:
                level -= releaseStep();
                if (level <= silenceFloor)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                return level;
        }
    }

private:
    enum class Stage { idle, attack, decay1, decay2, release };

    static constexpr float attackSeconds = 0.002f;
    static constexpr float decay1Seconds = 0.15f;
    static constexpr float decay1FloorLevel = 0.6f;
    static constexpr float decay2Seconds = 3.0f;
    static constexpr float decay2FloorLevel = 0.001f;
    static constexpr float releaseSeconds = 0.05f;
    static constexpr float silenceFloor = 0.0001f;

    float attackStep() const noexcept { return 1.0f / (float) (attackSeconds * sampleRate); }
    float releaseStep() const noexcept { return 1.0f / (float) (releaseSeconds * sampleRate); }

    // Per-sample multiplicative coefficients so each stage's decay is a real exponential (matching
    // how every analog envelope, and this plugin's own ADSR-driven stages, actually decay) rather
    // than a linear ramp - std::pow(target, 1/N) started from 1.0 reaches exactly `target` after N
    // samples.
    float decay1Coeff() const noexcept { return std::pow(decay1FloorLevel, 1.0f / (float) (decay1Seconds * sampleRate)); }
    float decay2Coeff() const noexcept { return std::pow(decay2FloorLevel, 1.0f / (float) (decay2Seconds * sampleRate)); }

    double sampleRate = 44100.0;
    Stage stage = Stage::idle;
    float level = 0.0f;
};
