#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "KarplunkStringLine.h"

// The Feedback Topology seam (base case): a single delay line in a loop with one loop filter.
// Unlike Excitation/Loop Filter/Delay Tuning, topology isn't a template parameter on one fixed
// class - it changes member *layout* (a future dual-cross-coupled-line topology needs two
// KarplunkStringLine members and a cross-mix stage, not just different behaviour in one member).
// So a new topology is a wholly separate class reusing Excitation/LoopFilter/InterpolationType by
// value the same way this one does, not a fourth template argument here. See
// karplunk-synth/README.md's "Future swap-in points" table for what a cross-coupled or
// nonlinear-in-the-loop topology would need beyond this class.
template <typename Excitation, typename LoopFilter, typename InterpolationType = LinearInterpolator>
class SingleLineKarplunkVoice
{
public:
    static constexpr int kLowestSupportedMidiNote = 21;    // A0, 27.5 Hz
    static constexpr int kHighestSupportedMidiNote = 108;  // C8 - interpolation-quality ceiling,
                                                             // not a real-time-safety limit

    // capacity = ceil(sampleRate / 27.5 * 1.15) + 8. The x1.15 reserves ~2.5 semitones of
    // headroom for future downward pitch-drift/bend below A0 (documented, not implemented yet -
    // see the Delay Tuning row in README.md's swap-in table); +8 covers a future higher-order
    // (Lagrange-style) interpolator's reach. 44.1kHz -> 1853 samples (~7.4KB); 48kHz -> 2016
    // (~8.1KB); 96kHz -> 4023 (~16.1KB) - trivial memory, computed once here, never resized after
    // prepare().
    static int requiredCapacitySamples(double sampleRate) noexcept
    {
        constexpr double lowestSupportedHz = 27.5;
        constexpr double headroomFactor = 1.15;
        constexpr int interpolationPad = 8;
        return (int) std::ceil(sampleRate / lowestSupportedHz * headroomFactor) + interpolationPad;
    }

    // Allocates (the excitation scratch buffer, and via KarplunkStringLine::prepare) - only ever
    // call this from PluginProcessor::prepareToPlay, never from the audio thread.
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = sampleRate;
        capacitySamples = requiredCapacitySamples(sampleRate);

        excitation.prepare(sampleRate);
        loopFilter.prepare(sampleRate);
        stringLine.prepare(sampleRate, capacitySamples);

        excitationScratch.resize((size_t) capacitySamples);
        silenceHoldSamples = (int) (sampleRate * 0.05); // ~50ms

        reset();
    }

    void reset() noexcept
    {
        excitation.reset();
        loopFilter.reset();
        stringLine.reset();
        active = false;
        silenceRunSamples = 0;
    }

    // Always retriggers - each MIDI note-on is physically a fresh pluck, not a pitch to glide
    // toward, so there's no held-note/legato-repitch logic here (unlike alloy-bass's continuous
    // VCO voice).
    void noteOn(int midiNoteNumber, float velocity01) noexcept
    {
        reset();

        const auto delaySamples = midiNoteToDelaySamples(midiNoteNumber);
        stringLine.setDelaySamples(delaySamples);

        // Standard Karplus-Strong priming: fill the delay line directly with excitation samples,
        // bypassing the loop filter for this initial fill (the filter only applies to samples
        // that have gone all the way around the loop at least once). Just write() - see
        // KarplunkStringLine.h's class comment for why that's safe here (a pure, non-consuming
        // read means there's no separate priming method needed).
        const auto primeLength = std::min((int) delaySamples, capacitySamples);
        excitation.generate(excitationScratch.data(), primeLength, velocity01);
        for (int i = 0; i < primeLength; ++i)
            stringLine.write(excitationScratch[(size_t) i]);

        active = true;
    }

    // No-op for now - a plucked string physically continues ringing after the key is released.
    // Documented seam for a future "palm mute" variant that would accelerate loopFilter's gain
    // toward 0 on release instead; no real-time-safety implication either way (pure arithmetic
    // state change, no allocation).
    void noteOff() noexcept {}

    void setDamping(float amount01) noexcept
    {
        loopFilter.setDamping(amount01);
    }

    // Only takes effect on the next noteOn() - excitation.generate() runs once per pluck, not
    // per-sample, so there's nothing to smooth here the way damping/output level are smoothed.
    void setBrightness(float amount01) noexcept
    {
        excitation.setBrightness(amount01);
    }

    float renderNextSample() noexcept
    {
        const auto delayed = stringLine.read();
        const auto filtered = loopFilter.processSample(delayed);
        stringLine.write(filtered);

        if (std::abs(filtered) < silenceThreshold)
        {
            if (++silenceRunSamples >= silenceHoldSamples)
                active = false;
        }
        else
        {
            silenceRunSamples = 0;
        }

        return filtered;
    }

    bool isActive() const noexcept { return active; }

private:
    float midiNoteToDelaySamples(int midiNoteNumber) const noexcept
    {
        const auto clampedNote = std::clamp(midiNoteNumber, kLowestSupportedMidiNote, kHighestSupportedMidiNote);
        const auto frequencyHz = 440.0 * std::pow(2.0, (clampedNote - 69) / 12.0);
        const auto delaySamples = (float) (sampleRateHz / frequencyHz);

        // Clamps interpolation-quality floor above kHighestSupportedMidiNote - not a
        // real-time-safety concern, just protects fractional-delay accuracy at very short delays.
        return std::max(8.0f, delaySamples);
    }

    Excitation excitation;
    LoopFilter loopFilter;
    KarplunkStringLine<InterpolationType> stringLine;

    std::vector<float> excitationScratch;
    int capacitySamples = 0;
    double sampleRateHz = 44100.0;

    bool active = false;
    int silenceRunSamples = 0;
    int silenceHoldSamples = 0;
    static constexpr float silenceThreshold = 0.0001f; // -80 dBFS
};
