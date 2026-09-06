#pragma once

#include "ConcreteFilterModels.h"
#include "ConcreteQuantizer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

// Phase 4: the offline resample -> input drive/saturation -> quantization pipeline that produces
// a zone's WORKING buffer from its SOURCE buffer (see ConcreteSampleZone.h and concrete-sampler-
// plugin-plan.md's Architecture #2/#3 and Phase 4). Runs on a background thread in the real
// plugin (see ConcreteAudioProcessor's bake thread) - never on the audio thread - and never
// touches the source buffer it reads from.
//
// The technique being modeled: pitch the source UP before "capturing" it (e.g. playing a 33rpm
// record at 45 - about +5 semitones), so what's actually stored is shorter and higher-pitched.
// Drive and quantization then act on THAT resampled signal, baking in artifacts that reflect
// having been captured at the elevated rate. On playback, auto-compensate (see ConcreteVoice)
// subtracts the same transpose back out so the note still lands at its expected pitch, but now
// carrying those artifacts. Deliberately does NOT otherwise run through any filter model
// (Architecture #3 - filters are playback-side only on the real hardware) - Phase 5's "double
// smear" is the one deliberate exception, an explicitly non-authentic option (default off) that
// inserts a filter pass at the very END of the whole chain (resample -> drive -> quantize ->
// filter, once, after all iterations - not per-iteration), mirroring "audio that already came out
// of a playback path being re-recorded." Quantization always runs (using the current Bit Depth/
// Quantizer Mode - Phase 3) even when the resample+drive technique itself is bypassed, since on
// real hardware SOME converter is always in the signal path - `settings.bypass` specifically
// disables resample+drive+quantize+doubleSmear ALL FOUR as one unit ("monitor the raw source with
// nothing engaged"), not quantization alone.
class ConcreteCapturePass
{
public:
    struct Settings
    {
        bool bypass = true;                 // Disables resample+drive+quantize+doubleSmear entirely - see the class comment.
        double transposeSemitones = 5.0;    // The 33->45rpm-style pitch-up amount, applied per iteration.
        double inputDriveDb = 0.0;          // 0 = no added saturation/HF rolloff.
        int iterations = 1;                 // Repeats the whole resample->drive->quantize chain this many times
                                             // for compounding degradation - capped at 4 by the APVTS range.
        ConcreteQuantizer::Mode quantizerMode = ConcreteQuantizer::Mode::linear;
        int bitDepthBits = 16;

        // Phase 5's double smear (Architecture #3) - deliberately independent of the live playback
        // filter's own model/cutoff/resonance parameters, so turning THOSE never triggers a
        // re-bake (only these do). Off by default.
        bool doubleSmear = false;
        ConcreteFilterModel::Mode doubleSmearFilterModel = ConcreteFilterModel::Mode::ssm;
        double doubleSmearCutoffHz = 8000.0;
        double doubleSmearResonance01 = 0.0;
    };

    struct Result
    {
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        // Total pitch-up actually baked across all iterations (0 if bypassed) - see
        // ConcreteSampleZone::captureTransposeSemitones, which this is written into.
        double captureTransposeSemitones = 0.0;
    };

    // sourceBuffer must be non-null with at least one sample. sourceSampleRate is needed for the
    // drive stage's HF-rolloff filter, whose cutoff is expressed in Hz.
    static Result apply(const juce::AudioBuffer<float>& sourceBuffer, double sourceSampleRate,
                         const Settings& settings);

private:
    // Offline, fixed-ratio version of ConcretePitchEngine's own cubic-interpolation reference
    // path - clean/transparent by design, since the resample step itself models a pitch change
    // (like a variable-speed tape), not a converter's limitations. ratio > 1 pitches up and
    // shortens the buffer (samplesOut = round(samplesIn / ratio)); ratio < 1 pitches down and
    // lengthens it.
    static juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& input, double ratio);

    // Soft (tanh) saturation plus a one-pole lowpass whose cutoff falls as drive increases -
    // modeling "sampling hot rolled off the high end on playback" (see the plan's Phase 4 notes
    // on the MPC60). A no-op when driveDb <= 0, so the default (0dB) stays exactly transparent.
    static void applyDrive(juce::AudioBuffer<float>& buffer, double sourceSampleRate, double driveDb);

    // Runs one static-cutoff/resonance pass of the given filter model over the whole buffer, once,
    // per channel (a fresh ConcreteFilterModel instance per channel - see that class for why -
    // there's no envelope/key-tracking modulation here, unlike the live playback filter; this is
    // one fixed setting baked in once).
    static void applyDoubleSmear(juce::AudioBuffer<float>& buffer, double sourceSampleRate,
                                  ConcreteFilterModel::Mode model, double cutoffHz, double resonance01);
};
