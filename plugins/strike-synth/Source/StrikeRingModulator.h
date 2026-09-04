#pragma once

#include <cmath>

namespace
{
    constexpr float strikeRingModulatorPi = 3.14159265358979323846f;
}

// StrikeRingModulator: multiplies a signal by a free-running sine oscillator - the classic ring
// modulation effect (sum/difference sideband frequencies, not harmonically related to the input).
//
// Originally applied IN-LOOP (recirculated into the string, not just the final output) at the
// user's explicit request, for a more extreme/metallic/inharmonic character than a bolted-on
// post-effect would give. Reverted to OUTPUT-ONLY after being measured cutting a note's natural
// decay severely even at tiny amounts - the oscillator's own near-zero crossings, multiplied into
// the recirculating signal every single pass (thousands of times per second at a typical
// fundamental), compounded into a note dying in well under 100ms even at Ring Mod=2%, reported by
// the user as the control making notes sound like "a tiny metallic ping" instead of ringing out.
// StrikeStringLineChannel::renderChannelSample() now only calls process() on its OUTPUT-only
// copy - see that call site's own comment.
//
// This is its own area, not a fourth Waveshaper Type, for two reasons: it needs its own Frequency
// control (a modulator Hz, not just a 0-100% amount) that none of the Waveshaper curves have any
// equivalent of, and its safety story is fundamentally different - see process()'s own comment.
//
// Runs its own free-running phase, deliberately NOT synced to the note's own pitch - a real ring
// mod pedal's oscillator is independent of whatever's played through it, which is exactly what
// produces its characteristic inharmonic sidebands (a note-tracked modulator would just be another
// wavefolding-adjacent harmonic effect, not a ring mod). Each voice owns an independent oscillator
// instance (matching every other per-voice seam in this codebase - Waveshaper, dispersion noise,
// etc. are all per-voice, not shared across the 8-voice pool) - a deliberate simplicity tradeoff:
// simultaneously-held notes are each ring-modulated independently rather than phase-locked to a
// single shared oscillator, so a chord's ring-mod character isn't perfectly coherent across voices.
// Phase resets to 0 in reset() (called from every noteOn(), matching this class's "always retriggers,
// everything reset" policy) rather than free-running across notes, so a given note's ring-mod
// artifact starts from the same phase every time - a deliberate, testable choice, not an attempt at
// realism (a real analog oscillator wouldn't reset on every new note either).
class StrikeRingModulator
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        phase = 0.0f;
        currentOscValue = 0.0f;
    }

    // Call exactly once per sample tick, before process() - advances the oscillator's own phase.
    // Kept as a separate call (not folded into process()) for the same reason StrikeBitCrush
    // splits updateFilter()/process(): a shared per-sample state update, computed once, read via
    // a separate accessor.
    void updateOscillator(float frequencyHz) noexcept
    {
        currentOscValue = std::sin(strikeRingModulatorPi * 2.0f * phase);
        phase += frequencyHz / (float) sampleRateHz;
        phase -= std::floor(phase); // wrap to [0, 1)
    }

    // amount01=0 is a bit-exact no-op (gain=1 exactly); amount01=1 is full ring modulation
    // (y = x * oscValue). Interpolating the GAIN toward oscValue, rather than crossfading two
    // separately-computed signals (x, and x*oscValue), is algebraically identical to a true
    // dry/wet blend - `x*(1-amount) + (x*oscValue)*amount == x*(1 + amount*(oscValue-1))` - and
    // has a real safety consequence: since oscValue is bounded to [-1, 1] and amount01 to [0, 1],
    // the resulting gain factor `1 + amount01*(oscValue-1)` is itself ALWAYS bounded to [-1, 1] -
    // ring modulation can only ever shrink or invert a signal passing through it, never amplify it -
    // still true and still relied on now that this is output-only (no risk of accidentally
    // amplifying the audible copy), even though the old "no loop-gain-runaway risk, so no
    // driveCompensation-style split needed" framing no longer applies now that there's no
    // recirculating call to compare against at all.
    float process(float x, float amount01) const noexcept
    {
        const auto gain = 1.0f + amount01 * (currentOscValue - 1.0f);
        return x * gain;
    }

private:
    double sampleRateHz = 44100.0;
    float phase = 0.0f;
    float currentOscValue = 0.0f;
};
