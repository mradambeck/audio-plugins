#pragma once

#include <juce_core/juce_core.h>

#include <array>

// The three pitch-engine modes that replace Phase 1's high-quality interpolation step, plus that
// interpolation itself kept as a fourth "reference" mode (see concrete-sampler-plugin-plan.md's
// Phase 2) - the reference path stays available for auditioning/measurement, but no real machine
// preset ever selects it once Phase 7 lands. One class, not one subclass per mode, so
// ConcreteVoice's inner render loop calls the same processSample() regardless of mode - only
// startNote() picking which Mode to pass branches on machine type, not the per-sample hot path.
//
// Single-channel: a stereo zone needs one instance PER CHANNEL (see ConcreteVoice), since Mode B/C
// carry internal history (a held sample, a delta-sigma integrator) that must not be shared between
// independent channels of audio. Given identical control inputs (pitchRatio, baseRateHz, start
// position), two instances fed a stereo zone's L/R data naturally stay in lockstep on
// getSourcePhase() without it needing to be threaded through externally.
class ConcretePitchEngine
{
public:
    enum class Mode
    {
        reference,                  // Phase 1's cubic interpolation - never used by a real preset
        variableClockZeroOrderHold, // Mode A
        dropSampleDecimation,       // Mode B
        deltaSigma,                 // Mode C
    };

    void prepare(double hostSampleRateIn) noexcept { hostSampleRate = hostSampleRateIn; }

    // Begins reading from `startPosition` (in source samples). Must be called before the first
    // processSample() of a note.
    void start(double startPosition) noexcept
    {
        sourcePhase = startPosition;
        baseRateTickPhase = 0.0;
        heldSample = 0.0f;
        pendingFirstTick = true;
        oversampleTickAccumulator = 0.0;
        deltaSigmaIntegrator = 0.0f;
        deltaSigmaFeedback = 0.0f;
        decimationFilterState.fill(0.0f);
    }

    double getSourcePhase() const noexcept { return sourcePhase; }

    // Produces one output sample and advances internal state by one host sample's worth.
    //   - pitchRatio: 2^(semitonesTotal/12) - the note's total transposition as a frequency ratio.
    //   - effectiveSourceRateHz: what rate to treat `data` as running at. For Mode::reference this
    //     is the zone's own sourceSampleRate (the file's real rate); for the three machine modes
    //     it's the baseRate parameter instead (a machine's assumed capture rate stands in for the
    //     file's actual one - see concrete-sampler-plugin-plan.md's Phase 2). Only ever used as a
    //     ratio against hostSampleRate, so the same value means the same thing at 44.1k/48k/96k.
    float processSample(Mode mode, const float* data, int dataLength, double pitchRatio, double effectiveSourceRateHz) noexcept
    {
        switch (mode)
        {
            case Mode::reference:                  return readReference(data, dataLength, pitchRatio, effectiveSourceRateHz);
            case Mode::variableClockZeroOrderHold:  return readModeA(data, dataLength, pitchRatio, effectiveSourceRateHz);
            case Mode::dropSampleDecimation:        return readModeB(data, dataLength, pitchRatio, effectiveSourceRateHz);
            case Mode::deltaSigma:                  return readModeC(data, dataLength, pitchRatio, effectiveSourceRateHz);
        }
        return 0.0f;
    }

private:
    float readReference(const float* data, int dataLength, double pitchRatio, double sourceRateHz) noexcept;
    float readModeA(const float* data, int dataLength, double pitchRatio, double baseRateHz) noexcept;
    float readModeB(const float* data, int dataLength, double pitchRatio, double baseRateHz) noexcept;
    float readModeC(const float* data, int dataLength, double pitchRatio, double baseRateHz) noexcept;

    double hostSampleRate = 44100.0;
    double sourcePhase = 0.0;

    // Mode B state (also doubles as "have we produced a first sample yet" for Mode A/reference,
    // which don't otherwise need it).
    double baseRateTickPhase = 0.0;
    float heldSample = 0.0f;
    bool pendingFirstTick = true;

    // Mode C state. decimationFilterState is a 2-stage cascade, not a single pole - see
    // readModeC()'s own comment for why a single stage isn't enough to prevent the oversampled
    // bitstream's own high-frequency noise from aliasing back down into the audible range.
    double oversampleTickAccumulator = 0.0;
    float deltaSigmaIntegrator = 0.0f;
    float deltaSigmaFeedback = 0.0f;
    std::array<float, 2> decimationFilterState{};
};
