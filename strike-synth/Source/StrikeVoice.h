#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "StrikeLoopFilter.h"
#include "StrikeRingModulator.h"
#include "StrikeStringLine.h"
#include "StrikeWaveshaper.h"

namespace
{
    constexpr float pi = 3.14159265358979323846f;

    // Bit-exact float comparison for the cache-invalidation checks below (delaySamplesForPitch(),
    // levelOutput()) - a plain == is correct here too (an exact-match cache genuinely wants exact
    // equality, not a tolerance), but trips -Wfloat-equal; comparing raw bit patterns gets the
    // identical semantics without the warning.
    [[maybe_unused]] bool floatBitsEqual(float a, float b) noexcept
    {
        uint32_t bitsA, bitsB;
        std::memcpy(&bitsA, &a, sizeof(bitsA));
        std::memcpy(&bitsB, &b, sizeof(bitsB));
        return bitsA == bitsB;
    }
}

// A generic, reusable cascaded first-order Schroeder allpass primitive - deliberately has no
// notion of "structure" or "dispersion" (the Structure knob's mapping to a gain, and the
// resulting group-delay compensation, live in StrikeStringLineChannel::renderChannelSample(),
// not here), matching how StrikeStringLine itself doesn't know about MIDI notes.
//
// This used to be a SINGLE allpass with a large, variable delay (mirroring Mutable Instruments
// Rings' own string.cc), splitting a note's delay into a shortened main portion plus that one
// big allpass. That turned out to be a real, measured bug: a Schroeder allpass's group delay is
// a function of frequency, tau(theta) = D*(1-g^2)/(1+g^2-2g*cos(theta)), theta = D*omega - for a
// large D (tens of samples), theta sweeps through many full cycles as pitch or Structure change,
// so assuming "the allpass contributes ~D worth of delay" only holds by coincidence, causing up
// to ~95 cents of unpredictable detuning (confirmed by direct pitch measurement).
//
// The actual standard technique (Jaffe & Smith 1983; Van Duyne & Smith 1994; see Julius O.
// Smith's "Physical Audio Signal Processing", ccrma.stanford.edu/~jos/pasp/) is a cascade of
// several IDENTICAL first-order (D=1, single-sample-delay) Schroeder allpass stages instead. A
// D=1 allpass's group delay is smooth and MONOTONIC across the whole spectrum - no oscillation,
// since theta=omega never wraps around 2*pi for realistic pitches - so the cascade's total group
// delay at a note's own fundamental can be computed exactly and compensated for reliably (see
// renderChannelSample()), unlike the large-D case. No delay-line/ring-buffer needed any more -
// each stage is just one float of state.
//
// Direct-form-II Schroeder allpass per stage: w = x + gain*state, y = -gain*w + state,
// state = w. Unconditionally stable for |gain| < 1, independent of the other stages. Unity
// magnitude response at every frequency by construction - it only reshapes phase/group-delay
// (which is what stretches upper partials sharp, creating inharmonicity), so it can't perturb
// loop energy/gain or interact with the existing Bow loudness compensation, which is purely
// magnitude-based (sqrt(1 - loopGain)).
class StrikeDispersionFilter
{
public:
    static constexpr int numStages = 8; // see renderChannelSample()'s own comment for how this was chosen

    void prepare() noexcept { reset(); }
    void reset() noexcept { std::fill(std::begin(state), std::end(state), 0.0f); }

    float process(float x, float gain) noexcept
    {
        float y = x;
        for (int i = 0; i < numStages; ++i)
        {
            const auto w = y + gain * state[i];
            y = -gain * w + state[i];
            state[i] = w;
        }
        return y;
    }

private:
    float state[numStages] = {};
};

// A small, fixed-capacity, INTEGER-sample delay line used only for Couple Delay (see
// StrikeVoice::renderNextSample()'s Dual-topology coupling comment) - deliberately NOT
// StrikeStringLine: that class is sized for a note's own pitch period, uses fractional
// interpolation, and has bulk-priming semantics tied to Karplus-Strong noteOn, none of which apply
// here. Couple Delay shapes the COUPLING PATH's own frequency response, not a note's pitch, so
// sub-sample precision buys nothing - this is just a plain ring buffer, sized once in prepare().
class StrikeShortDelay
{
public:
    void prepare(int maxDelaySamples) noexcept
    {
        buffer.assign((size_t) (std::max(1, maxDelaySamples) + 1), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    // Writes x this tick, returns whatever was written `delaySamples` ticks ago (clamped to this
    // buffer's own capacity). delaySamples=0 returns x itself, immediately - a bit-exact match for
    // "no delay," which is exactly Couple Delay's own default/off value.
    float process(float x, int delaySamples) noexcept
    {
        const auto capacity = (int) buffer.size();
        const auto clampedDelay = std::clamp(delaySamples, 0, capacity - 1);
        buffer[(size_t) writeIndex] = x;
        const auto readIndex = (writeIndex - clampedDelay + capacity) % capacity;
        writeIndex = (writeIndex + 1) % capacity;
        return buffer[(size_t) readIndex];
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
};

// StrikeStringLineChannel: everything that is genuinely PER-STRING - one delay line, one loop
// filter, one excitation, one dispersion filter, one full set of Waveshapers/Ring Modulator, and
// all the pitch/glide/silence-tracking state that goes with a single resonating line. This used
// to BE the whole voice (as `SingleLineStrikeVoice`) before the Feedback Topology seam grew a
// second option: a dual cross-coupled topology needs two of everything in this class, so this is
// now the reusable "one string" building block, and `StrikeVoice` below is the orchestrator
// that owns one or two of these and decides how they're wired together.
//
// The split is a pure refactor for the Single-topology case: `renderChannelSample()` is today's
// former `renderNextSample()` body up to (not including) `stringLine.write()`, which moved out
// into `writeBack()` so a voice-level orchestrator can intercept the value between "computed" and
// "written back" (that interception point is exactly where cross-coupling happens - see
// `StrikeVoice::renderNextSample()`). Same operations, same order, same formulas - Single
// topology's output is bit-exact with what this whole file produced before this split existed.
template <typename Excitation, typename InterpolationType = LinearInterpolator>
class StrikeStringLineChannel
{
public:
    // Range widened at the user's explicit request (their own DAW's note-name display, one octave
    // below this file's older A0/C8-style comments elsewhere - MIDI note numbers are the same
    // either way, only the label differs): MIDI 0 ("C-2") to MIDI 84 ("C5"), replacing the
    // previous MIDI 21-108 span.
    static constexpr int kLowestSupportedMidiNote = 0;     // ~8.18 Hz
    static constexpr int kHighestSupportedMidiNote = 84;   // interpolation-quality ceiling, not a
                                                             // real-time-safety limit

    // capacity = ceil(sampleRate / 8.1758 * 1.15) + 8. The x1.15 reserves ~2.5 semitones of
    // headroom for future downward pitch-drift/bend below the lowest supported note (documented,
    // not implemented yet - see the Delay Tuning row in README.md's swap-in table); +8 covers a
    // future higher-order (Lagrange-style) interpolator's reach. 44.1kHz -> 6211 samples
    // (~24.8KB); 48kHz -> 6761 (~27KB); 96kHz -> 13513 (~54KB) - still trivial memory, computed
    // once here, never resized after prepare(), even though widening the note range down to
    // MIDI 0 grew this ~3.4x versus the previous MIDI-21 floor. A dual-topology voice needs two
    // of these buffers, not one - still trivial (a few hundred KB across the whole 8-voice pool
    // at worst).
    static int requiredCapacitySamples(double sampleRate) noexcept
    {
        constexpr double lowestSupportedHz = 8.1758; // MIDI note 0
        constexpr double headroomFactor = 1.15;
        constexpr int interpolationPad = 8;
        return (int) std::ceil(sampleRate / lowestSupportedHz * headroomFactor) + interpolationPad;
    }

    // Allocates (via StrikeStringLine::prepare) - only ever call this from
    // PluginProcessor::prepareToPlay, never from the audio thread.
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = sampleRate;
        capacitySamples = requiredCapacitySamples(sampleRate);

        excitation.prepare(sampleRate);
        loopFilterTwoPoint.prepare(sampleRate);
        loopFilterResonant.prepare(sampleRate);
        stringLine.prepare(sampleRate, capacitySamples);
        dispersionFilter.prepare();
        waveFolder.prepare(sampleRate);
        bitCrush.prepare(sampleRate);
        ringMod.prepare(sampleRate);

        silenceHoldSamples = (int) (sampleRate * 0.05); // ~50ms

        // See renderChannelSample()'s waveshaping step for what this envelope is for. A one-pole
        // rise toward 1 (the same one-pole-coefficient pattern used throughout this file, e.g.
        // glideCoeff below) - waveshapeAttackSeconds is a short, "just enough to round off the
        // very first sample or two" time constant, not a slow fade-in.
        waveshapeAttackCoeff = 1.0f - std::exp(-1.0f / (waveshapeAttackSeconds * (float) sampleRate));

        reset();
    }

    void reset() noexcept
    {
        excitation.reset();
        loopFilterTwoPoint.reset();
        loopFilterResonant.reset();
        stringLine.reset();
        dispersionFilter.reset();
        waveFolder.reset();
        bitCrush.reset();
        ringMod.reset();
        active = false;
        silenceRunSamples = 0;
        dispersionNoise = 0.0f; // dispersionRngState deliberately NOT reset - see its own comment
        waveshapeAttackEnvelope = 0.0f; // starts silent-of-effect at noteOn - see renderChannelSample()
    }

    // Gives this channel's Excitation and dispersion-noise generator a genuinely different noise
    // sequence from another channel's default state - called exactly once, by StrikeVoice's
    // prepare(), on its SECOND line only. Without this, two identically-constructed, identically-
    // driven channels (same pitch, same damping, same everything else) would produce bit-identical
    // output every sample, which would make Dual topology's cross-coupling AND Detune both
    // silently inaudible at their defaults - coupling or slightly detuning two copies of the exact
    // same signal does nothing new. xorshift is degenerate at seed 0 (same convention
    // dispersionRngState's own default already followed), so 0 is remapped to 1 by both this call
    // and Excitation::setSeed() itself.
    void setNoiseSeed(uint32_t excitationSeed, uint32_t dispersionSeed) noexcept
    {
        excitation.setSeed(excitationSeed);
        dispersionRngState = dispersionSeed != 0 ? dispersionSeed : 1;
    }

    // Always retriggers - each MIDI note-on is physically a fresh pluck (or bow stroke): the
    // excitation/loop filter/dispersion filter/string content are always fully reset below, no
    // exceptions. `glideTimeSeconds` (0 by default, matching every existing call site - Poly mode
    // and every isolated-Channel test never touch it) affects ONLY the PITCH's approach to its new
    // target, not whether a fresh pluck happens - see the class comment above renderChannelSample()'s
    // glide step for why glide-the-pitch-but-still-repluck (not a true legato "no new attack"
    // glide) was the chosen design for Mono. Only actually glides if this channel was already
    // active right before this call (a legato retrigger - there's a previous pitch to glide
    // from); a fresh note struck from silence always snaps straight to its own pitch, matching
    // standard "auto-glide" convention on hardware mono synths.
    //
    // `semitoneOffset` (0 by default) is Detune's own doing - StrikeVoice's second line passes
    // a nonzero value here (see its own noteOn()) so the two lines can play at slightly different
    // pitches; a single channel used on its own (Single topology, or any isolated test) never
    // touches this and behaves exactly as before.
    void noteOn(int midiNoteNumber, float velocity01, float glideTimeSeconds = 0.0f, float semitoneOffset = 0.0f) noexcept
    {
        const auto wasActive = active; // captured before reset() clears it

        reset();

        const auto clampedNote = std::clamp(midiNoteNumber, kLowestSupportedMidiNote, kHighestSupportedMidiNote);
        targetPitchMidi = (float) clampedNote + semitoneOffset;

        if (wasActive && glideTimeSeconds > 0.0f)
        {
            // currentPitchMidi deliberately keeps its PRE-reset value here (not touched by
            // reset() - same convention as currentDelaySamples always following the same rule)
            // as the glide's starting point; renderChannelSample()'s glide step carries it toward
            // targetPitchMidi over glideTimeSeconds from here.
            glideCoeff = 1.0f - std::exp(-1.0f / (glideTimeSeconds * (float) sampleRateHz));
        }
        else
        {
            currentPitchMidi = targetPitchMidi; // nothing to glide from, or glide time is 0
            glideCoeff = 1.0f;                  // reaches target on the very next glide step
        }

        currentDelaySamples = delaySamplesForPitch(currentPitchMidi);
        stringLine.setDelaySamples(currentDelaySamples);

        // No bulk-priming loop any more - the per-tick enveloped excitation (see
        // renderChannelSample()) provides its own energy from t=0. This is mechanically equivalent
        // to the old bulk write() priming for the whole first lap: stringLine.read() returns
        // exactly 0 until the first injected sample has looped all the way around once, so the
        // loop filter sees silence and the injected signal reaches the string raw/unfiltered
        // either way - the classic Karplus-Strong "excitation only gets filtered once it's looped
        // around" property is preserved by construction, not by coincidence. Keyed to the note's
        // own TARGET duration, not the (possibly still-gliding) current one - a glide shouldn't
        // make the excitation's own decay-to-sustain timing itself uncertain.
        excitation.setBaseDuration((int) delaySamplesForPitch(targetPitchMidi));

        active = true;
        noteVelocity = velocity01;
    }

    // Starts the excitation's Release stage - the injected envelope ramps down to 0 over a
    // bowAmount-dependent time rather than being cut off instantly (see StrikeExcitation.h). A
    // plucked string (bowAmount at or near 0) physically continues ringing after release
    // regardless, via the loop filter's own decay, same mechanism a bowed string then uses too -
    // physically correct (a released bow doesn't instantly silence the string). No
    // real-time-safety implication (pure arithmetic state change, no allocation).
    void noteOff() noexcept { excitation.noteOff(); }

    // Fans out to BOTH concrete loop filters unconditionally (cheap scalar stores either way) -
    // matches Waveshaper's own "always keep every concrete instance current" convention, so
    // whichever type is selected next (via setLoopFilterType()) is already up to date for a live
    // mid-note switch.
    void setDamping(float amount01) noexcept
    {
        loopFilterTwoPoint.setDamping(amount01);
        loopFilterResonant.setDamping(amount01);
    }

    // Runtime selector between the two concrete loop filters (0 = Two-Point Average, 1 =
    // Resonant) - see StrikeLoopFilter.h's own comment for why this seam migrated to a runtime
    // choice, mirroring Waveshaper Type exactly.
    void setLoopFilterType(int type) noexcept { loopFilterType = type; }

    // Resonant-loop-filter-only (has no effect at Loop Filter Type = Two-Point Average) - live,
    // every-sample, same convention as Waveshape/Structure. See StrikeResonantLoopFilter's own
    // comment for why none of these need a safety ceiling (output-only, never recirculated).
    void setResonance(float amount01) noexcept { loopFilterResonant.setResonance(amount01); }
    void setFilterCutoff(float hz) noexcept { loopFilterResonant.setCutoffFrequency(hz); }

    // Filter envelope (Cutoff's own Attack->Decay sweep) - also Resonant-loop-filter-only, also
    // live/every-sample, same convention. See StrikeResonantLoopFilter's own comment for the
    // envelope shape and why envelopeAmount is bipolar.
    void setFilterEnvAmount(float bipolarAmount) noexcept { loopFilterResonant.setEnvelopeAmount(bipolarAmount); }
    void setFilterAttack(float seconds) noexcept { loopFilterResonant.setEnvelopeAttackSeconds(seconds); }
    void setFilterDecay(float seconds) noexcept { loopFilterResonant.setEnvelopeDecaySeconds(seconds); }

    // Since the unified-envelope redesign, nextExcitationSample() reads brightness every tick, the
    // same way it reads bowAmount - so this class itself has no "only at noteOn" restriction any
    // more. It's still a "next pluck" characteristic control in practice because PluginProcessor
    // only calls this once, right before noteOn() - not every sample the way it drives
    // setBowAmount()/setDamping() - a deliberate call-site choice (Brightness stays a set-once tone
    // knob, unlike the live-performance Pluck/Bow control), not a DSP-level limitation.
    void setBrightness(float amount01) noexcept
    {
        excitation.setBrightness(amount01);
    }

    // Pluck-side only (no effect on the friction bow model's own noise floor) - runtime selector
    // between the excitation's three noise-color generators (White/Pink/Brown), same
    // live/every-sample convention as Waveshaper Type/Loop Filter Type - see
    // StrikeExcitation::setNoiseColor()'s own comment.
    void setNoiseColor(int color) noexcept
    {
        excitation.setNoiseColor(color);
    }

    // Unlike setBrightness(), this DOES take effect immediately, live, while a note rings - see
    // renderChannelSample(), which queries the excitation every sample for as long as the note is
    // held. PluginProcessor smooths this the same way it smooths Decay, not latched like
    // Brightness.
    void setBowAmount(float amount01) noexcept
    {
        excitation.setBowAmount(amount01);
    }

    // Bow-side only (no effect at bowAmount=0) - see StrikeExcitation::setBowForce()'s own
    // comment. Live, every-sample, same convention as Bow Amount.
    void setBowForce(float amount01) noexcept
    {
        excitation.setBowForce(amount01);
    }

    // Live, every-sample, same convention as setBowAmount()/setDamping() - see renderChannelSample().
    void setStructure(float amount01) noexcept { structure = amount01; }
    void setPosition(float amount01) noexcept { position = amount01; }

    // Live, every-sample - see renderChannelSample()'s waveshaping step. amount01=0 is a bit-exact
    // no-op (neither waveshaper is ever even called in that case), matching Structure's own
    // precedent for a "new control defaults to unchanged behavior" convention.
    void setWaveshapeAmount(float amount01) noexcept { waveshapeAmount = amount01; }

    // Runtime selector between the two concrete waveshapers (0 = Fold, 1 = BitCrush) - see
    // StrikeWaveshaper.h's own comment for why this seam is a runtime choice rather than a
    // compile-time template parameter like the other three.
    void setWaveshaperType(int type) noexcept { waveshaperType = type; }

    // Live, every-sample - see renderChannelSample()'s ring modulation step (applied in-loop,
    // after the Waveshaper). amount01=0 is a bit-exact no-op (the oscillator isn't even advanced
    // in that case), same convention as Waveshape.
    void setRingModAmount(float amount01) noexcept { ringModAmount = amount01; }
    void setRingModFrequency(float hz) noexcept { ringModFrequency = hz; }

    // What renderNextSample() used to compute in one pass: the value about to be written back
    // into the loop (`filtered`) and the value about to be heard (`forOutput`) - see
    // StrikeWaveFolder's own comment for why those two are deliberately different signals.
    // Deliberately does NOT write to `stringLine` or touch the silence-tracking/Position-tap
    // state - see writeBack()/positionOutput() below, and StrikeVoice::renderNextSample() for
    // why that split exists (it's the exact point cross-coupling reads from/writes to).
    struct ChannelResult
    {
        float filtered;
        float forOutput;
    };

    ChannelResult renderChannelSample() noexcept
    {
        // Glide: smoothed in PITCH (MIDI-note/log-frequency) space, not delay-samples space
        // directly - a one-pole approach toward targetPitchMidi is what a real analog portamento
        // circuit does (an RC-smoothed 1V/oct control voltage), giving the familiar
        // decelerating-as-it-approaches-the-target character, which a one-pole smoothing of
        // delaySamples itself would NOT reproduce (delay length and pitch aren't linearly
        // related, so equal steps in delay-space are not equal steps in pitch-space). Runs
        // unconditionally every tick - at glideCoeff=1.0 (set whenever noteOn() didn't actually
        // glide) this converges to targetPitchMidi in a single step and stays there, so there's
        // no separate "am I gliding right now" branch needed here.
        currentPitchMidi += glideCoeff * (targetPitchMidi - currentPitchMidi);
        currentDelaySamples = delaySamplesForPitch(currentPitchMidi);
        stringLine.setDelaySamples(currentDelaySamples);

        // Structure: a cascade of first-order allpass stages stretches upper partials sharp
        // relative to the fundamental (the classic Jaffe/Smith dispersion technique - see
        // StrikeDispersionFilter's own comment for why a cascade of small stages replaced the
        // original single large-delay allpass). Linear gain mapping - no dead-zone epsilon needed,
        // gain=0 is already exactly safe with this formula (falls through to the plain read below
        // instead of ever being evaluated at gain=0 anyway).
        //
        // The cascade's own contribution at this note's fundamental is computed exactly and
        // subtracted from mainDelay - unlike the old design, this is smooth/monotonic in omega
        // (not oscillating), so it stays well-behaved across the whole note range, not just near
        // DC. numStages/maxDispersionGain were tuned by rendering and measuring actual per-harmonic
        // pitch (not just the fundamental) across several combinations - the first, conservative
        // values (numStages=3, gain=0.3, chosen only from a closed-form worst-case check) kept
        // every note in tune but left almost no measurable stretch ACROSS the harmonic series
        // (< 1 cent of spread between the 1st and 9th partial - correctly tuned, but not really
        // dispersion, just a small uniform shift). Raising numStages instead of gain alone let the
        // achievable stretch grow without pushing any single stage's own nonlinearity as hard,
        // reaching numStages=8 before gain became the binding constraint again: past
        // roughly gain=0.5, the fundamental itself measurably drifts (up to ~30 cents at gain=0.6,
        // confirmed empirically, and switching the compensation from group delay to phase delay -
        // see below - made no measurable difference, which rules out that distinction as the
        // cause; the true resonant frequency itself deviates from the naive omega=2*pi/delaySamples
        // this formula assumes, by an amount this design doesn't attempt to solve). Settled on
        // maxDispersionGain=0.5 as the practical ceiling: ~5-13 cents of stretch on the loudest,
        // most audible harmonics (3rd/5th/7th) while every supported note's fundamental stays
        // within ~12.5 cents of its own baseline at worst (only at Structure > 90% on a few notes)
        // - see git history/PR discussion for the measured numbers this was tuned against, and
        // both pitch-stability tests below.
        //
        // Above Structure=75%, an additional mechanism kicks in that DOES deliberately perturb the
        // fundamental - ported directly from Mutable Instruments Rings' actual string.cc (read
        // directly from github.com/pichenettes/eurorack, not guessed): lowpassed noise is used as
        // multiplicative FM of the delay length itself (`delay_fm`, applied to `delay` before the
        // main/allpass split), scaled by `(4*(dispersion-0.75))^2 * 0.025`. This is NOT a bug to
        // compensate away - real Rings does not try to keep the fundamental locked as Structure
        // increases; this noise-driven pitch instability IS the audible "breaking up/unstable"
        // character that makes high Structure obviously perceptible on real hardware, on top of
        // (not instead of) the allpass stretch above. Confirmed by rendering/listening: the pure
        // allpass stretch alone (gain=0.5, below) measured as real but was judged tonally
        // indistinguishable by ear even in an isolated A/B - this mechanism is what real Rings
        // actually relies on for the top quarter of the knob to read as audibly different, not a
        // stronger version of the same stretch. Uses a fixed noise-lowpass coefficient
        // (dispersionNoiseFilterCoeff) in place of Rings' brightness-derived one - tying this to
        // Strike's own Brightness knob (a different control, the excitation's own tone) would
        // create a surprising cross-coupling between two otherwise-independent parameters.
        float delayed;
        if (structure > 0.0f)
        {
            const auto rawNoise = nextDispersionUniformNoise() / (0.2f + dispersionNoiseFilterCoeff);
            dispersionNoise += dispersionNoiseFilterCoeff * (rawNoise - dispersionNoise);

            const auto rawNoiseAmount = structure > 0.75f ? 4.0f * (structure - 0.75f) : 0.0f;
            const auto noiseAmount = rawNoiseAmount * rawNoiseAmount * 0.025f;
            const auto delayFm = 1.0f + dispersionNoise * noiseAmount;
            const auto fmDelaySamples = currentDelaySamples * delayFm;

            const auto gain = structure * maxDispersionGain;

            // Compensate using PHASE delay, not group delay - what actually keeps the loop
            // resonating exactly at the target frequency is the TOTAL PHASE at that frequency
            // equalling a multiple of 2*pi, which is governed by phase delay
            // (-angle(H(e^jw))/w), not group delay (-dangle/dw); the two only coincide exactly
            // when the phase response is locally linear near omega. Kept as the more principled
            // choice even though it measured no different from group delay in practice here (see
            // the tuning note above) - the two formulas happen to agree closely in the regime this
            // design operates in, but phase delay is the one that's actually correct by
            // definition, so there's no reason to prefer the approximation.
            const auto omega = 2.0f * pi / fmDelaySamples;
            const auto numReal = -gain + std::cos(omega);
            const auto numImag = -std::sin(omega);
            const auto denReal = 1.0f - gain * std::cos(omega);
            const auto denImag = gain * std::sin(omega);
            const auto stagePhase = std::atan2(numImag, numReal) - std::atan2(denImag, denReal);
            const auto totalPhase = (float) StrikeDispersionFilter::numStages * stagePhase;
            const auto totalPhaseDelay = -totalPhase / omega;
            const auto mainDelay = fmDelaySamples - totalPhaseDelay;

            if (mainDelay >= 4.0f)
                delayed = dispersionFilter.process(stringLine.readAt(mainDelay), gain);
            else
                delayed = stringLine.read();
        }
        else
        {
            delayed = stringLine.read();
        }

        // Loop Filter Type: runtime choice between the two concrete filters (see
        // StrikeLoopFilter.h's own comment for why this seam migrated to a runtime dropdown,
        // mirroring Waveshaper Type). Both getLoopGain() below and processSample() here branch on
        // the same `loopFilterType`, so whichever filter is selected drives both the recirculating
        // signal AND the Bow loudness-compensation math consistently.
        auto filtered = loopFilterType == 0 ? loopFilterTwoPoint.processSample(delayed)
                                             : loopFilterResonant.processSample(delayed);

        // Unified excitation injection: unconditional, every tick (no more `held` gate) - an idle
        // (never-triggered) or fully-released excitation just returns ~0 on its own, see
        // StrikeExcitation.h. One bowAmount-dependent gain curve, not two independently-tuned
        // constants or factors: at bowAmount=0 this is exactly 1.0 (no loudness compensation and
        // no boost at all - matches the old design's uncompensated pluck burst, which bypassed
        // both the loop filter and any gain adjustment entirely by being bulk-written straight
        // into the string). At bowAmount=1 it's continuousLevelAnalog * sqrt(1 - loopGain).
        // Interpolating the SAME target value (not the two factors separately) matters:
        // interpolating sqrt(1-loopGain) alone from 1.0 while leaving it active at bowAmount=0 was
        // a real bug caught by actually rendering and measuring a pluck's peak - it crushed pluck
        // loudness by up to ~14x at high Decay, since sqrt(1-loopGain) ranges down to ~0.02 there
        // and was previously never applied to the one-shot burst at all.
        //
        // sqrt(1 - loopGain) itself is a partial loudness compensation across Decay settings - the
        // naive theoretical model (treating this loop like a simple single-pole feedback, energy ~
        // 1/(1-loopGain^2)) overstated the actual gain by ~30dB when measured (this loop's real
        // per-pass gain is frequency-dependent - TwoPointAverageLoopFilter is a lowpass, not a flat
        // scalar - so a simple scalar model doesn't hold). continuousLevelAnalog below is tuned
        // against the ACTUAL measured relationship, not the theoretical one; the residual damping-
        // dependence left after that tuning is accepted, not chased further - see README's Future
        // swap-in points.
        //
        // tanh() here caps only the injected contribution, not the whole signal - raw white
        // noise's own crest factor, combined with this loop's resonant buildup, produces measured
        // single-sample peaks well above the steady-state RMS, which would clip hard once multiple
        // bowed voices sum in PluginProcessor. At bowAmount=0 this only engages for the (rare)
        // near-full-velocity noise sample that would already be near +-1 anyway - a light, always-
        // on safety softening at the very top of the range, not a structural change from before.
        const auto activeLoopGain = loopFilterType == 0 ? loopFilterTwoPoint.getLoopGain() : loopFilterResonant.getLoopGain();
        const auto fullBowGain = continuousLevelAnalog * std::sqrt(1.0f - activeLoopGain);

        // Attack-loudness hump compensation: measured a real ~5dB attack-peak loudness BUMP as
        // bowAmount rises from 0, peaking around bowHumpPeakAmount and settling back down toward
        // fullBowGain's own (much gentler) curve past it - reported by the user as "pluck to bow
        // causes gain around 14%." Root cause is the excitation's own Attack TIME (see
        // StrikeExcitation::nextExcitationSample()) lengthening from a near-instant pluck burst
        // toward a much slower bow attack as bowAmount rises - even a small amount of "bow" mixed
        // in injects real energy into the resonant loop over a much longer window than a pure
        // pluck's near-instant burst did, before the (much smaller) fullBowGain-based reduction
        // above catches up. A measured (not derived) x*e^(1-x)-shaped dip, peaking at exactly 1.0
        // (full compensation) at bowHumpPeakAmount and tapering to 0 (no compensation) at both
        // bowAmount=0 and well past the peak - see git history/PR discussion for the measured
        // before/after numbers.
        const auto bowHumpRatio = excitation.getBowAmount() / bowHumpPeakAmount;
        const auto bowHumpCompensation = 1.0f - bowHumpCompensationAmount * bowHumpRatio * std::exp(1.0f - bowHumpRatio);

        const auto injectionGain = (1.0f + excitation.getBowAmount() * (fullBowGain - 1.0f)) * bowHumpCompensation;
        // `filtered` (the string's own already-computed current content - post-Structure-
        // dispersion, post-loop-filter) is passed in as the friction model's velocity proxy - see
        // StrikeExcitation.h's own comment for why this is the direct single-rail analogue of
        // what real two-rail digital waveguide bow models read from their own rails, and why it's
        // read BEFORE this tick's own contribution is added (the one-sample-delayed feedback real
        // real-time friction implementations use to avoid an implicit per-sample solve).
        filtered += std::tanh(excitation.nextExcitationSample(noteVelocity, filtered) * injectionGain);

        // Waveshaper: nonlinearly reshapes the COMBINED signal (recirculating loop content plus
        // this tick's freshly injected excitation) right before it's written back - so the
        // distortion becomes part of what the string is actually resonating with, compounding
        // every pass around the loop, not a one-shot effect applied only to the output. See
        // StrikeWaveshaper.h for why this is a runtime choice between concrete classes rather
        // than a template parameter like the other three seams, and why amount01 is passed
        // directly per-call rather than cached via a setter.
        //
        // Two SEPARATE calls per waveshaper, not one shared value, despite both starting from the
        // same pre-waveshape `filtered` - discovered by measuring, not planned upfront, that "safe
        // to feed back into the loop" and "sounds right on the output" are genuinely different
        // requirements at high drive (see StrikeWaveFolder's own comment for the full story).
        // The write-back call always uses full drive compensation (driveCompensation=1, the
        // safety-critical default) since it's what actually recirculates; the output call uses
        // much less (foldOutputDriveCompensation) by rendering and measuring loudness parity,
        // since output is never fed back and full compensation was measured crushing the fold's
        // own audible character almost to silence at high drive.
        // Only the IN-LOOP write-back happens here now, unconditionally regardless of
        // distortionPosition - Fold's own recirculating character is independent of the Pre/Post
        // Filter control (see setDistortionPosition()'s own comment: that control only reorders
        // the OUTPUT-only copy relative to the filter, never what's fed back into the string).
        // The OUTPUT-only copy itself - for EITHER waveshaper, at EITHER Distortion Position - is
        // entirely handled by applyOutputEffects() now, not here: `forOutput` stays the clean,
        // unshaped signal unconditionally, so Position's own tap-cancellation (see
        // positionOutput()'s own comment) always sees the same clean reference regardless of
        // Distortion Position - the ONLY thing that control changes is where, relative to the
        // filter, the (always post-Position) distortion lands. Keeping Fold's own output shaping
        // upstream of Position for Pre Filter specifically was tried first and measured breaking
        // that invariant (Pre Filter and Post Filter differed even at Loop Filter Type=Two-Point
        // Average, where there's no filter stage to differ around at all) - moving both
        // waveshapers' entire output-only behavior into applyOutputEffects() fixed it.
        const auto preWaveshapeSignal = filtered;
        if (waveshaperType == 0 && waveshapeAmount > 0.0f)
        {
            // Knob range compression - the user found the full 0-100% Waveshape turn pushed Fold
            // past a musically usable point well before reaching 100%. The knob's displayed
            // 0-100% is unchanged - only how far that maps into Fold's own amount01 range is
            // rescaled.
            const auto effectiveAmount = waveshapeAmount * foldMaxAmountFraction;
            filtered = waveFolder.process(preWaveshapeSignal, effectiveAmount, 1.0f);
            filtered = preWaveshapeSignal + waveshapeAttackEnvelope * (filtered - preWaveshapeSignal);
        }
        waveshapeAttackEnvelope += waveshapeAttackCoeff * (1.0f - waveshapeAttackEnvelope);

        return { filtered, preWaveshapeSignal };
    }

    // Pre/Post Filter - at the user's explicit request ("sometimes you want the distortion to
    // happen on the filtered signal, sometimes you want the distortion to go through the
    // filter"): reorders the WAVESHAPER's (Fold or BitCrush, whichever is selected) output-only
    // copy relative to the Resonant loop filter's own outputColor() coloring, in
    // applyOutputEffects() below. Purely an output-signal-path reorder - Fold's own IN-LOOP
    // recirculating contribution (renderChannelSample() above) is completely unaffected either
    // way, so this doesn't touch Fold's established resonant character or reopen any of the
    // safety questions that character's own in-loop status was already settled for. 0 = Pre
    // Filter (distortion first, then filtered - the default), 1 = Post Filter (filtered first,
    // then distortion applied to the filtered signal). Has no audible effect at Loop Filter Type =
    // Two-Point Average (there's no filter stage in the output path to be before or after in that
    // case - both positions literally converge to the same code path, so they're bit-exact there,
    // not just similar).
    void setDistortionPosition(int position) noexcept { distortionPosition = position; }

    // Post-Position output-only effects: BOTH waveshapers' entire output-only behavior (not just
    // BitCrush's, any more - see renderChannelSample()'s own comment for why Fold's moved here
    // too), Ring Mod, and Resonance's own peak coloring all live here, applied by the orchestrator
    // to positionOutput()'s own result (see StrikeVoice::renderNextSample()), after being
    // measured breaking Position's tap-cancellation design when they ran BEFORE it: Position
    // subtracts a phase-shifted tap of the SAME periodic signal to cancel specific harmonics (see
    // positionOutput()'s own comment) - that cancellation relies on the two things being
    // subtracted staying correlated copies of one periodic waveform. Ring Mod (an independent
    // oscillator) and BitCrush (discrete quantization) both break that correlation, turning a
    // designed CANCELLATION into an uncorrelated ADDITION instead - measured making a Ring-
    // Modulated, Position-tapped note up to ~25x louder than the same signal without Position
    // engaged, not quieter as the safety argument ("ring mod can only shrink a signal") assumed in
    // isolation. Running all of them strictly AFTER Position (and after write-back, since none of
    // them recirculate any more - see StrikeLoopFilter.h/StrikeRingModulator.h's own comments)
    // sidesteps the interaction entirely: Position's cancellation now only ever sees the string's
    // own clean, correlated content, exactly as designed.
    float applyOutputEffects(float positionedOutput) noexcept
    {
        auto value = positionedOutput;

        auto applyDistortion = [this](float x) noexcept {
            return waveshaperType == 0 ? applyFoldOutputOnly(x) : applyBitCrush(x);
        };

        if (distortionPosition == 0 && waveshapeAmount > 0.0f)
            value = applyDistortion(value);

        if (loopFilterType == 1)
            value = loopFilterResonant.outputColor(value);

        if (distortionPosition == 1 && waveshapeAmount > 0.0f)
            value = applyDistortion(value);

        if (ringModAmount > 0.0f)
        {
            ringMod.updateOscillator(ringModFrequency);
            value = ringMod.process(value, ringModAmount);
        }

        return value;
    }

    // BitCrush used to also be written back into the loop (like Fold still is), and was measured
    // causing real "massive feedback": quantizing the recirculating signal creates a classic
    // zero-input limit cycle (a well-known failure mode of quantization inside a feedback loop) -
    // a plucked note that should decay to silence instead locked into a stable non-decaying buzz,
    // or at higher amounts actively GREW over time (measured RMS rising from -26dB to -5dB and
    // staying there over a few seconds at Waveshape=100%, on a single plucked note with no
    // continuous excitation at all). Now that quantization never re-enters the string, there's no
    // limit-cycle risk left at any amount - full range restored (bitCrushMaxAmountFraction=1)
    // instead of the knob-compression workaround this used before that root cause was found.
    float applyBitCrush(float value) noexcept
    {
        const auto effectiveAmount = waveshapeAmount * bitCrushMaxAmountFraction;
        bitCrush.updateFilter(value, effectiveAmount);
        return bitCrush.process();
    }

    // Fold's Post-Filter output-only copy - only called from applyOutputEffects() when
    // distortionPosition==1 (see setDistortionPosition()'s own comment). Same attack-envelope
    // crossfade renderChannelSample() applies for the Pre-Filter case, just computed here instead,
    // against whatever signal (the filtered one) it's handed - waveshapeAttackEnvelope itself is
    // still only ever advanced once per tick, in renderChannelSample(), so both call sites always
    // read the same, single, per-tick envelope value.
    float applyFoldOutputOnly(float value) noexcept
    {
        const auto effectiveAmount = waveshapeAmount * foldMaxAmountFraction;
        const auto folded = waveFolder.process(value, effectiveAmount, foldOutputDriveCompensation);
        return value + waveshapeAttackEnvelope * (folded - value);
    }

    // Writes the (possibly cross-coupled - see StrikeVoice::renderNextSample()) value back into
    // this channel's own delay line, and updates this channel's silence tracking - split out from
    // renderChannelSample() specifically so a voice-level orchestrator can substitute a different
    // value than the one this channel itself computed (Dual topology's whole point). At Single
    // topology, StrikeVoice always calls this with EXACTLY the value renderChannelSample() just
    // returned as `filtered`, unchanged - bit-identical to the pre-split code, which called
    // stringLine.write(filtered) immediately after computing it.
    void writeBack(float value) noexcept
    {
        stringLine.write(value);

        if (std::abs(value) < silenceThreshold)
        {
            if (++silenceRunSamples >= silenceHoldSamples)
                active = false;
        }
        else
        {
            silenceRunSamples = 0;
        }
    }

    // Position: an independent, non-recursive second read of the same string, combined with the
    // OUTPUT only - never written back (writeBack() above already wrote whatever value the
    // orchestrator gave it, so the loop's own pitch/decay/stability is untouched by this). This is
    // the classic physical-modeling "excite/listen at a different point along the string" effect:
    // a real string excited/read at position p has zero energy at every harmonic n where n*p is an
    // integer (a node falls exactly there) - at p=0.5 (the exact midpoint), every EVEN harmonic is
    // missing, giving the "hollow, square-wave-like" character the Position control is meant to
    // produce. clampedPosition folds the knob symmetrically into roughly [0.01, 0.5] (matches
    // Mutable Instruments Rings' own formula, the reference this is modeled on) - avoiding a
    // degenerate near-zero-length tap while keeping the effect symmetric around the string's exact
    // midpoint.
    //
    // The cancellation only exists in the INTERFERENCE between `forOutput` and positionTap, not in
    // positionTap alone - a single read of a periodic signal at any phase has identical harmonic
    // MAGNITUDES to any other read of it (phase-shifting can't remove energy from a harmonic, only
    // rotate its phase), confirmed by measuring harmonic content of positionTap alone across the
    // whole Position range and finding it literally unchanged. Combining the two - forOutput's k-th
    // harmonic component summed with a copy phase-shifted by clampedPosition's own fraction of a
    // cycle - is what creates real magnitude cancellation. The SIGN matters: summing (tried first)
    // cancels ODD harmonics at p=0.5 (confirmed by the same math, then measured), the wrong
    // polarity; subtracting cancels EVEN harmonics there, matching the physically-correct,
    // documented behavior - verified by measurement, not assumed. positionOutputGain scales the
    // subtracted tap - tuned empirically (see git history for the measured numbers), not guessed.
    float positionOutput(float forOutput) const noexcept
    {
        const auto clampedPosition = 0.5f - 0.98f * std::abs(position - 0.5f);
        const auto positionTap = stringLine.readAt(currentDelaySamples * clampedPosition);
        return forOutput - positionOutputGain * positionTap;
    }

    bool isActive() const noexcept { return active; }

    // The currently-playing note's own period, in samples - see StrikeVoice::levelOutput()'s own
    // comment for why this feeds a pitch-adaptive time constant there.
    float getCurrentDelaySamples() const noexcept { return currentDelaySamples; }

private:
    // xorshift32, same technique/rationale as StrikeExcitation::nextNoiseSample() (deterministic,
    // allocation-free, no JUCE dependency) - a separate RNG/state from the excitation's own noise,
    // since this drives delay-length FM (pitch), not the injected excitation signal itself. Never
    // reset in reset()/noteOn() (matching StrikeExcitation's own convention), so consecutive notes
    // on the same voice get non-repeating noise while a freshly-constructed processor stays fully
    // deterministic - relied on by StrikeProcessorTests' "two fresh processors render bit-
    // identical output" regression test.
    float nextDispersionUniformNoise() noexcept
    {
        dispersionRngState ^= dispersionRngState << 13;
        dispersionRngState ^= dispersionRngState >> 17;
        dispersionRngState ^= dispersionRngState << 5;
        return (float) dispersionRngState / (float) UINT32_MAX * 2.0f - 1.0f;
    }

    // Takes a FRACTIONAL MIDI note so Glide's per-sample pitch value (which spends most of its
    // life between two integer notes while gliding) can be converted every tick, not just at
    // noteOn() - the note-on path clamps to a real MIDI note first (see noteOn()), so this itself
    // doesn't re-clamp the input, only the output (the interpolation-quality floor below).
    // Called every render tick via the glide step (renderChannelSample()), even when pitch isn't
    // actively gliding - in which case midiNoteFloat is bit-identical to the previous call's, since
    // a fixed-point one-pole recurrence (glideCoeff=1, or a converged glide) keeps producing the
    // exact same value. Cached so the std::pow call only actually runs when the input changes
    // (gliding, or a fresh note-on/pitch bend), not on every sample of a held, steady-pitch note.
    float delaySamplesForPitch(float midiNoteFloat) noexcept
    {
        if (hasCachedDelaySamples && floatBitsEqual(midiNoteFloat, cachedPitchMidi))
            return cachedDelaySamples;

        const auto frequencyHz = 440.0 * std::pow(2.0, ((double) midiNoteFloat - 69.0) / 12.0);
        const auto delaySamples = (float) (sampleRateHz / frequencyHz);

        // Clamps interpolation-quality floor above kHighestSupportedMidiNote - not a
        // real-time-safety concern, just protects fractional-delay accuracy at very short delays.
        cachedPitchMidi = midiNoteFloat;
        cachedDelaySamples = std::max(8.0f, delaySamples);
        hasCachedDelaySamples = true;
        return cachedDelaySamples;
    }

    float cachedPitchMidi = 0.0f;
    float cachedDelaySamples = 0.0f;
    bool hasCachedDelaySamples = false;

    Excitation excitation;
    // Runtime-selectable, not a template parameter (see StrikeLoopFilter.h's own comment) -
    // both concrete filters always present, branched on by loopFilterType. Damping is fanned out
    // to both via setDamping() so whichever is active is always current.
    TwoPointAverageLoopFilter loopFilterTwoPoint;
    StrikeResonantLoopFilter loopFilterResonant;
    int loopFilterType = 0; // 0 = Two-Point Average, 1 = Resonant - see setLoopFilterType()
    StrikeStringLine<InterpolationType> stringLine;
    StrikeDispersionFilter dispersionFilter;
    // Runtime-selectable, not a template parameter - see StrikeWaveshaper.h's own comment for
    // why this one seam works differently from Excitation/Delay Tuning. Both concrete types live
    // here unconditionally (no polymorphism/vtable), selected per-sample by `waveshaperType` in
    // renderChannelSample().
    StrikeWaveFolder waveFolder;
    StrikeBitCrush bitCrush;

    // Its own area, not a fifth Waveshaper type - see StrikeRingModulator.h's own comment.
    StrikeRingModulator ringMod;

    int capacitySamples = 0;
    double sampleRateHz = 44100.0;
    float currentDelaySamples = 0.0f;

    // Glide state (see noteOn()'s and renderChannelSample()'s own comments) - none of these are
    // touched by reset(), same convention as currentDelaySamples always following: a legato
    // retrigger needs the PRE-reset pitch to survive the reset as the glide's starting point.
    float currentPitchMidi = 0.0f;
    float targetPitchMidi = 0.0f;
    float glideCoeff = 1.0f;

    bool active = false;
    int silenceRunSamples = 0;
    int silenceHoldSamples = 0;
    static constexpr float silenceThreshold = 0.0001f; // -80 dBFS

    float noteVelocity = 0.0f;
    float structure = 0.0f;
    float waveshapeAmount = 0.0f;
    int waveshaperType = 0; // 0 = Fold, 1 = BitCrush - see setWaveshaperType()
    int distortionPosition = 0; // 0 = Pre Filter, 1 = Post Filter - see setDistortionPosition()
    float ringModAmount = 0.0f;
    float ringModFrequency = 200.0f; // matches PluginProcessor's own default

    // Attack envelope for the waveshaping step - see renderChannelSample()'s own comment. Reset to
    // 0 at noteOn() (via reset()); waveshapeAttackCoeff is computed from waveshapeAttackSeconds in
    // prepare(), same one-pole-coefficient pattern used throughout this file.
    float waveshapeAttackEnvelope = 0.0f;
    float waveshapeAttackCoeff = 1.0f;
    static constexpr float waveshapeAttackSeconds = 0.008f; // ~8ms - just long enough to round off
                                                              // the pluck's own near-instant attack

    // See renderChannelSample()'s comment on the two separate waveshaper calls per type - how much
    // drive compensation the OUTPUT-only path gets (0 = none/loudest, 1 = full/matches the
    // recirculating path). Tuned by measurement.
    static constexpr float foldOutputDriveCompensation = 0.0f;

    // Waveshape knob range compression - see renderChannelSample()'s own comment. Fold still needs
    // this (it stays in-loop, so its own drive has a musically-useful-range ceiling well before
    // 100%, tuned directly by the user). BitCrush no longer does - now that it's output-only (see
    // renderChannelSample()'s own comment), there's no loop-gain/limit-cycle concern left to cap,
    // so its knob gets its full, uncompressed range back.
    static constexpr float foldMaxAmountFraction = 0.59f;
    static constexpr float bitCrushMaxAmountFraction = 1.0f;

    // Defaults to the string's midpoint (clampedPosition = 0.5, the maximum tap fraction), not 0
    // - 0 folds to clampedPosition = 0.01, a near-zero-length tap that's most correlated with
    // `filtered` and closest to doubling the output; 0.5 is where the tap is least correlated
    // with the main signal (the classic "plucking a string at its middle" partial-cancellation
    // point), matching the intended UI default and avoiding an accidental worst-case default for
    // anything (like these tests) that constructs a Channel without ever calling setPosition().
    float position = 0.5f;

    // Injected-signal loudness at full bow, applied on top of the sqrt(1-loopGain) compensation
    // above - tuned empirically by rendering and measuring actual steady-state RMS against a
    // plucked note's peak level (not by reasoning about the loop's theoretical gain alone - see
    // this class's own renderChannelSample() comment, and git history/PR discussion for the
    // measured numbers this was calibrated against).
    static constexpr float continuousLevelAnalog = 4.0f;

    // See renderChannelSample()'s own comment for the attack-loudness-hump this compensates for.
    // Tuned empirically by rendering and measuring attack-peak level across the Pluck/Bow range.
    static constexpr float bowHumpPeakAmount = 0.1f;
    static constexpr float bowHumpCompensationAmount = 0.65f;

    // Scales the subtracted tap in `forOutput - positionOutputGain * positionTap` - see
    // positionOutput()'s own comment for why subtraction (not addition) gives the
    // physically-correct even-harmonic cancellation at Position = 50%. Tuned empirically, not
    // guessed.
    static constexpr float positionOutputGain = 1.0f;

    // Structure's per-stage allpass gain at full Structure (100%) - see
    // StrikeDispersionFilter's own comment and renderChannelSample()'s Structure comment for how
    // this and numStages were chosen via closed-form arithmetic, not by feel.
    static constexpr float maxDispersionGain = 0.5f;

    // Structure's noise-driven delay-FM state (see renderChannelSample()'s comment) - dispersionNoise
    // is the running lowpassed noise value; dispersionRngState is nextDispersionUniformNoise()'s
    // own xorshift32 state, seeded to a fixed non-zero constant (xorshift is degenerate at 0) -
    // overridable via setNoiseSeed() (see its own comment) for a second, independently-noisy line.
    uint32_t dispersionRngState = 1;
    float dispersionNoise = 0.0f;

    // Fixed stand-in for Rings' brightness-derived noise_filter (SemitonesToRatio((brightness-1)*
    // 48) - deliberately not tied to Strike's own Brightness knob, a different, independent
    // control (excitation tone). 0.25 matches Rings' own noise_filter value at ITS default
    // brightness (0.5) - a reasoned starting point, not a final tuning; see git history if this
    // gets revisited after listening.
    static constexpr float dispersionNoiseFilterCoeff = 0.25f;
};

// StrikeVoice: the Feedback Topology seam's orchestrator. Owns one or two
// StrikeStringLineChannel instances and decides how they're wired together - Single topology
// (the original scaffold, one delay line in a loop) or Dual (two lines, cross-coupled at their
// write-back point). This is a RUNTIME choice (a "Topology" dropdown), like Waveshaper Type and
// Ring Modulator, not a compile-time template swap - the user wants to A/B the two topologies by
// ear in real time, same as every other seam this session. Both channels are always constructed/
// prepared/reset unconditionally (matching the exact convention every runtime-selectable seam in
// this codebase already follows - all four Waveshapers and the Ring Modulator are owned by value
// regardless of which is selected) - `lineB` simply never gets rendered when Topology is Single,
// costing zero CPU, the same way an unselected Waveshaper costs zero CPU.
//
// Two lines is genuinely required for real coupling, not incidental: without a way to make lineB
// sound different from lineA, cross-coupling two identical signals does nothing (the maths reduces
// to a no-op - see renderNextSample()'s own comment). Real, physically-grounded coupled-string
// technique (Weinreich, "Coupled Piano Strings," JASA 62(6), 1977; Julius O. Smith, "Physical
// Audio Signal Processing," Appendix C.13 "Two Coupled Strings," ccrma.stanford.edu/~jos/pasp/)
// couples strings that are each independently exciting/decaying - what actually produces the
// audible "double decay"/beating character is the coupling ACTING ON two genuinely independent
// strings, not a mistuning trick. `setNoiseSeed()` (see StrikeStringLineChannel's own comment)
// is how this class gives lineB that independence, at prepare() time.
template <typename Excitation, typename InterpolationType = LinearInterpolator>
class StrikeVoice
{
public:
    using Channel = StrikeStringLineChannel<Excitation, InterpolationType>;

    // Mirrors Channel's own constants exactly (both must stay in sync - a compile-time assertion
    // isn't practical across two independently-instantiable templates, so this is a documented
    // invariant, not an enforced one) - kept here too since existing call sites (PluginProcessor,
    // every isolated-voice test) reference `Voice::kLowestSupportedMidiNote` directly, not
    // `Voice::Channel::kLowestSupportedMidiNote`.
    static constexpr int kLowestSupportedMidiNote = 0;
    static constexpr int kHighestSupportedMidiNote = 84;

    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = sampleRate;

        // slowOutputCoeff depends only on slowOutputTimeSeconds (a compile-time constant) and
        // sampleRateHz (fixed for the life of the instance once prepare() has run) - it's a true
        // constant, so it's computed once here rather than every sample in levelOutput(), the same
        // "precompute a coefficient once in prepare()" pattern StrikeStringLineChannel's own
        // waveshapeAttackCoeff already uses.
        slowOutputCoeff = 1.0f - std::exp(-1.0f / (float) (slowOutputTimeSeconds * sampleRateHz));

        lineA.prepare(sampleRate);
        lineB.prepare(sampleRate);

        // Distinct, fixed, nonzero seeds - lineA keeps every existing default (1), so Single
        // topology and every pre-existing test stay bit-exact; lineB gets a different constant so
        // it's never a silent clone of lineA - see Channel::setNoiseSeed()'s own comment.
        lineB.setNoiseSeed(2, 2);

        const auto maxCoupleDelaySamples = (int) std::ceil(maxCoupleDelaySeconds * sampleRate) + 1;
        coupleDelayAToB.prepare(maxCoupleDelaySamples);
        coupleDelayBToA.prepare(maxCoupleDelaySamples);

        reset();
    }

    void reset() noexcept
    {
        lineA.reset();
        lineB.reset();
        coupleDelayAToB.reset();
        coupleDelayBToA.reset();
        fastOutputEnvelope = 0.0f;
        slowOutputEnvelope = 0.0f;
    }

    // Fans out to both lines unconditionally (cheap field/state resets either way - matches every
    // other always-present-but-maybe-unused seam in this codebase). Detune (see setDetuneAmount())
    // is applied here, once, as a semitone offset on lineB's own target pitch only - latched at
    // noteOn, the same "set-once tone" category as Brightness, since real unison detuning isn't a
    // live performance gesture the way Bow or Waveshape are.
    void noteOn(int midiNoteNumber, float velocity01, float glideTimeSeconds = 0.0f) noexcept
    {
        lineA.noteOn(midiNoteNumber, velocity01, glideTimeSeconds, 0.0f);
        lineB.noteOn(midiNoteNumber, velocity01, glideTimeSeconds, detuneAmount01 * maxDetuneSemitones);
    }

    void noteOff() noexcept
    {
        lineA.noteOff();
        lineB.noteOff();
    }

    void setDamping(float amount01) noexcept { lineA.setDamping(amount01); lineB.setDamping(amount01); }

    // Runtime selector for the Loop Filter seam (0 = Two-Point Average, 1 = Resonant) - live,
    // every-sample, same convention as Waveshaper Type. Fanned out identically to both lines, which
    // is exactly what keeps the Dual-topology safety proof valid for either filter type - see
    // renderNextSample()'s own coupling-safety comment.
    void setLoopFilterType(int type) noexcept { lineA.setLoopFilterType(type); lineB.setLoopFilterType(type); }
    void setResonance(float amount01) noexcept { lineA.setResonance(amount01); lineB.setResonance(amount01); }
    void setFilterCutoff(float hz) noexcept { lineA.setFilterCutoff(hz); lineB.setFilterCutoff(hz); }
    void setFilterEnvAmount(float bipolarAmount) noexcept { lineA.setFilterEnvAmount(bipolarAmount); lineB.setFilterEnvAmount(bipolarAmount); }
    void setFilterAttack(float seconds) noexcept { lineA.setFilterAttack(seconds); lineB.setFilterAttack(seconds); }
    void setFilterDecay(float seconds) noexcept { lineA.setFilterDecay(seconds); lineB.setFilterDecay(seconds); }
    void setBrightness(float amount01) noexcept { lineA.setBrightness(amount01); lineB.setBrightness(amount01); }
    void setBowAmount(float amount01) noexcept { lineA.setBowAmount(amount01); lineB.setBowAmount(amount01); }
    void setBowForce(float amount01) noexcept { lineA.setBowForce(amount01); lineB.setBowForce(amount01); }
    void setNoiseColor(int color) noexcept { lineA.setNoiseColor(color); lineB.setNoiseColor(color); }
    void setStructure(float amount01) noexcept { lineA.setStructure(amount01); lineB.setStructure(amount01); }
    void setPosition(float amount01) noexcept { lineA.setPosition(amount01); lineB.setPosition(amount01); }
    void setWaveshapeAmount(float amount01) noexcept { lineA.setWaveshapeAmount(amount01); lineB.setWaveshapeAmount(amount01); }
    void setWaveshaperType(int type) noexcept { lineA.setWaveshaperType(type); lineB.setWaveshaperType(type); }
    void setDistortionPosition(int position) noexcept { lineA.setDistortionPosition(position); lineB.setDistortionPosition(position); }
    void setRingModAmount(float amount01) noexcept { lineA.setRingModAmount(amount01); lineB.setRingModAmount(amount01); }
    void setRingModFrequency(float hz) noexcept { lineA.setRingModFrequency(hz); lineB.setRingModFrequency(hz); }

    // Runtime selector for the Feedback Topology seam (0 = Single, 1 = Dual) - live, every-sample,
    // same convention as Waveshaper Type. See renderNextSample() for what actually changes.
    void setTopology(int t) noexcept { topology = t; }

    // Live, every-sample, same convention as Waveshape/Structure/Position - see
    // renderNextSample()'s cross-coupling formula and its own safety argument for why the full
    // 0-100% range is provably safe with no ceiling needed (unlike every Waveshaper curve).
    void setCrossCoupleAmount(float amount01) noexcept { crossCoupleAmount = amount01; }

    // Live, every-sample - see renderNextSample()'s Dual-topology coupling comment for the closed-
    // form argument that this needs no ceiling either, exactly like Cross-Couple itself. 0ms is a
    // bit-exact match for the original (undelayed) coupling formula - see StrikeShortDelay's own
    // comment for why.
    void setCoupleDelay(float milliseconds) noexcept { coupleDelayMs = milliseconds; }

    // Latched at noteOn (see noteOn()'s own comment), not live - 0 = both lines at the identical
    // pitch (the primary, physically-grounded design - see this class's own header comment), 1 =
    // lineB offset by maxDetuneSemitones.
    void setDetuneAmount(float amount01) noexcept { detuneAmount01 = amount01; }

    float renderNextSample() noexcept
    {
        if (topology == 0)
        {
            // Single topology: lineB is never touched this tick at all (not rendered, not
            // written back, not read from), so it costs nothing and cannot perturb lineA in any
            // way. applyOutputEffects() (BitCrush/Ring Mod/Resonance color) runs strictly AFTER
            // positionOutput() - see its own comment for why that order matters. levelOutput()'s
            // own envelope tracking reads `positioned` (the Position-tapped signal it was always
            // designed around), not the further-processed `output`, so it keeps taming the
            // string's own genuine resonant warble without fighting Ring Mod's own fast,
            // deliberate amplitude modulation (using the even-earlier `a.filtered` here instead
            // was tried first and measured reintroducing the loudness-warble regression this
            // exact leveler exists to prevent - Position's own tap already meaningfully changes
            // the signal's dynamics, so the leveler needs to see it too, just not what comes
            // after it).
            const auto a = lineA.renderChannelSample();
            lineA.writeBack(a.filtered);
            const auto positioned = lineA.positionOutput(a.forOutput);
            return levelOutput(lineA.applyOutputEffects(positioned), positioned, lineA.getCurrentDelaySamples());
        }

        // Dual topology: cross-couple both lines' write-back values at the exact point each
        // line's own processing (Structure's dispersion, loop filter, excitation injection,
        // Waveshaper, Ring Mod - everything renderChannelSample() does) has finished, right before
        // either line's stringLine.write(). This is where Julius O. Smith's coupled-waveguide
        // formalization places the coupling too: each string's own OUTGOING, fully-processed wave
        // meets a shared junction, not its raw incoming read. `crossCoupleAmount` (`c`) is an exact
        // convex-combination weight, not an independent added gain:
        //
        //   writeBackA = (1-c)*filteredA + c*filteredB
        //   writeBackB = (1-c)*filteredB + c*filteredA
        //
        // At c=0 this reduces to writeBackA = filteredA exactly - lineA alone would still be
        // bit-exact with Single topology; the only difference Dual topology has at c=0 is that
        // lineB is ALSO independently exciting/ringing/tapped and summed into the final output.
        //
        // SAFETY, two independent arguments (both hold for every c in [0,1], no tuning/ceiling
        // needed - unlike every Waveshaper curve's maxDrive or Structure's maxDispersionGain):
        //  1. Per-sample boundedness: a convex combination (weights c and 1-c, both >= 0, summing
        //     to 1) can never exceed the larger of its two inputs in magnitude - and filteredA/
        //     filteredB are EACH already unconditionally bounded before cross-coupling ever runs
        //     (every Waveshaper's own output is bounded by construction, and the excitation
        //     injection is tanh-capped). Cross-coupling literally cannot introduce a new way to
        //     blow up.
        //  2. Steady-state loop-gain analysis: decompose (filteredA, filteredB) into common mode
        //     m=(A+B)/2 and differential mode d=(A-B)/2. Algebraically, writeBackA+writeBackB =
        //     filteredA+filteredB (common mode UNCHANGED by coupling), while writeBackA-writeBackB
        //     = (1-2c)*(filteredA-filteredB) (differential mode scaled by (1-2c) each pass). Both
        //     lines get IDENTICAL setDamping()/setLoopFilterType() calls every sample (see those
        //     setters above), so their ACTIVE loop filter's magnitude response H(w) is EXACTLY
        //     equal at every frequency, always - not approximately, and regardless of which Loop
        //     Filter Type is selected (Resonance/Cutoff/Envelope no longer factor in at all here -
        //     they're output-only now, see StrikeResonantLoopFilter's own comment, so they don't
        //     touch either line's recirculating H(w)). Common-mode
        //     round-trip gain per pass is exactly H(w) (unaffected by coupling, already proven safe
        //     by every existing Single-topology test, for either filter type - see
        //     StrikeLoopFilter.h's own comment for why |H(w)| <= 0.9995 holds for BOTH
        //     TwoPointAverageLoopFilter and StrikeResonantLoopFilter, at every frequency); the
        //     differential-mode gain is H(w)*(1-2c), and since (1-2c) in [-1,1] for c in [0,1],
        //     |H(w)*(1-2c)| <= |H(w)| <= 0.9995 always. So BOTH modes stay strictly contractive
        //     (< 1) for every Damping/Resonance/Formant setting, EITHER Loop Filter Type, and the
        //     ENTIRE Cross-Couple range - a strictly stronger guarantee than Structure needed,
        //     since cross-coupling is incapable, by construction, of raising either mode's gain
        //     above what an uncoupled line already safely has.
        //
        // Couple Delay inserts a short, fixed integer-sample delay into EACH direction of the
        // coupling path (StrikeShortDelay - a plain ring buffer, no fractional interpolation
        // needed since this shapes the coupling's own frequency response, not a note's pitch), so
        // writeBackA mixes in line B's value from `delaySamples` ticks ago rather than this
        // instant's. Physically, this is closer to what Weinreich/Smith's own reference actually
        // models - a real bridge coupling isn't instantaneous - and musically, it's what turns the
        // coupling from a flat, broadband effect into a genuinely HARMONIC-DEPENDENT one: a pure
        // delay is a phase shift, so different harmonics arrive at the coupling point at different
        // relative phases, reinforcing or partially cancelling depending on how the delay length
        // compares to each harmonic's own period - the same phase-interference mechanism Position's
        // tap already uses, just applied to the coupling path instead of a listening tap.
        //
        // SAFETY EXTENDS CLEANLY to any delay, with the identical bound, not a new one: viewed per
        // sample-rate frequency omega, a k-sample delay is a pure phase rotation, z^-k = e^-i*omega*k
        // - it CANNOT change magnitude, only phase. The common/differential-mode transfer factors
        // become Hm(omega) = (1-c) + c*e^-i*omega*k and Hd(omega) = (1-c) - c*e^-i*omega*k instead of
        // the plain real (1-2c) the undelayed case reduces to at k=0 - but both are still 2-tap FIR
        // filters whose COEFFICIENT MAGNITUDES sum to exactly (1-c)+c = 1, so by the triangle
        // inequality |Hm(omega)| <= 1 and |Hd(omega)| <= 1 for EVERY omega and EVERY delaySamples,
        // not just k=0 - the exact same bound as the undelayed proof above, now shown to hold
        // regardless of delay amount. Combined with each line's own per-frequency loop gain staying
        // under 1 regardless of coupling, the whole system stays strictly contractive at every
        // frequency for any Cross-Couple/Couple Delay/Damping combination - no new ceiling needed,
        // the same "already at its provably-safe maximum" property Cross-Couple's own range has.
        const auto a = lineA.renderChannelSample();
        const auto b = lineB.renderChannelSample();

        const auto c = crossCoupleAmount;
        const auto delaySamples = (int) std::lround(coupleDelayMs * 0.001f * (float) sampleRateHz);
        const auto delayedBForA = coupleDelayBToA.process(b.filtered, delaySamples);
        const auto delayedAForB = coupleDelayAToB.process(a.filtered, delaySamples);
        lineA.writeBack((1.0f - c) * a.filtered + c * delayedBForA);
        lineB.writeBack((1.0f - c) * b.filtered + c * delayedAForB);

        // Each line's Position tap is inherently a per-string concept (a pickup location ALONG A
        // PARTICULAR string) - there's no single well-defined "position" against a combined
        // signal, so each line gets its own tap off its own stringLine, summed at voice level.
        // dualTopologyOutputGain (0.5, plain linear averaging/-6dB) is a conservative starting
        // headroom choice, not the incoherent-power-sum 1/sqrt(2) an active-voice-count-based
        // headroom would use - the two coupled lines aren't statistically independent the way
        // pooled voices are, so the more conservative constant was chosen deliberately; flagged
        // for level-matching against Single topology by ear, same as that headroom constant was
        // itself originally reasoned rather than measured.
        const auto positionedA = lineA.positionOutput(a.forOutput);
        const auto positionedB = lineB.positionOutput(b.forOutput);
        const auto outputA = lineA.applyOutputEffects(positionedA);
        const auto outputB = lineB.applyOutputEffects(positionedB);

        // Couple Delay loudness compensation: measured a real, consistent ~2.8dB drop the instant
        // Couple Delay becomes nonzero (any amount from the smallest tested, 0.5ms, up through the
        // full 10ms range) - reported by the user as "Couple Delay causes about a 10dB drop"
        // (measured closer to 2.8-4dB in isolation, but compounds with the general loudness pass
        // this same investigation covered - see PluginProcessor.cpp's own masterPreGain/
        // headroomSmoothed comments). Root cause: at Couple Delay=0, both lines' write-back mixes
        // in the OTHER line's CURRENT (same-instant) value - since both lines default to the same
        // pitch (Detune=0), they're nearly identical signals, and summing two nearly-identical
        // signals reinforces coherently (close to a full linear sum). Any nonzero delay
        // phase-shifts the coupled copy relative to the receiving line's own content, breaking
        // that phase alignment across most harmonics - a real, physically-expected drop in
        // coherent reinforcement, not a bug in the coupling math itself, but still worth
        // compensating back toward the Couple Delay=0 loudness the user reasonably expects to
        // carry through as Couple Delay is dialed in. Saturates almost immediately (a small time
        // constant, matching the measured "already near-max drop by 0.5ms" shape) and is exactly
        // 1.0 at Couple Delay=0, so the existing bit-exact "0ms matches the original undelayed
        // formula" behavior is unaffected.
        const auto coupleDelayCompensation = 1.0f + coupleDelayCompensationAmount
            * (1.0f - std::exp(-coupleDelayMs / coupleDelayCompensationTimeConstantMs));

        return levelOutput(dualTopologyOutputGain * coupleDelayCompensation * (outputA + outputB),
                            dualTopologyOutputGain * coupleDelayCompensation * (positionedA + positionedB),
                            0.5f * (lineA.getCurrentDelaySamples() + lineB.getCurrentDelaySamples()));
    }

    // Single topology: only lineA matters (lineB was triggered by noteOn() too, per this class's
    // own "fan out unconditionally" convention, but is never rendered/silence-tracked, so its own
    // `active` flag would never naturally go false - checking it here would be wrong, not just
    // unnecessary). Dual topology: either line still ringing keeps the voice active.
    bool isActive() const noexcept
    {
        return topology == 0 ? lineA.isActive() : (lineA.isActive() || lineB.isActive());
    }

private:
    // Loudness leveling: tame the natural, audible loudness "warble" a noise-driven resonant loop
    // produces (see the original per-channel comment, preserved in spirit here) - moved from
    // per-channel to voice-level in this refactor, since a voice only ever has ONE audible output
    // stream regardless of how many lines it internally has, and the leveler has no per-string
    // physical meaning the way Position does. Straight relocation, not a redesign - same formula,
    // same constants, same order of operations, applied once to whichever `output` value
    // renderNextSample() produced (Single or Dual).
    //
    // `reference` (the Position-tapped signal BEFORE applyOutputEffects() - `positioned`, not the
    // further-processed `output` passed alongside it) is what the fast/slow envelopes are computed
    // FROM now, even though `output` (the fully-processed, Ring-Mod/BitCrush/Resonance-colored
    // signal) is what actually gets scaled and returned - a real bug fix, not a stylistic split:
    // when this used `output` itself, Ring Mod's own fast, deliberate amplitude swings dragged the
    // FAST envelope down every time the oscillator neared zero, while the SLOW envelope barely
    // moved - so levelingGain (slow/fast) spiked toward its own 3x ceiling right when Ring Mod
    // wanted the signal quiet, measured pumping a held bow note up to ~5x louder WITH Ring Mod on
    // than off, exactly backwards from Ring Mod's own bounded-shrink-only safety property. Using
    // the even-earlier `filtered` (before Position's own tap) was tried next and measured
    // reintroducing this exact leveler's OWN original regression (loudness warble on a held bow
    // note) - Position's tap meaningfully changes the signal's dynamics too, so the leveler still
    // needs to see it, just not what's layered on AFTER it. Tracking `positioned` keeps the
    // leveler doing its original job (taming genuine noise-driven resonant warble, Position tap
    // included) without fighting a deliberate output-stage effect it was never designed to react
    // to.
    // `delaySamples` is the currently-playing note's own period, in samples - see
    // fastOutputTimeSeconds's own comment for why the fast envelope's time constant needs to
    // scale with it (a rectify-then-smooth envelope follower needs several PERIODS to properly
    // resolve amplitude - a fixed constant shorter than a low note's own period goes unstable).
    float levelOutput(float output, float reference, float delaySamples) noexcept
    {
        const auto instantMagnitude = std::abs(reference);

        // max(), not the fixed constant directly - preserves the original fast, responsive
        // behavior exactly for every note whose own period already fits inside
        // fastOutputTimeSeconds (everything above roughly A2, ~110Hz), and only stretches the
        // window for lower notes where the fixed value would otherwise be too short to resolve a
        // full cycle.
        // fastOutputCoeff depends only on delaySamples (the note's own period), which is stable
        // whenever pitch isn't actively gliding/bending (same reasoning as
        // StrikeStringLineChannel::delaySamplesForPitch()'s own cache) - cached so the std::exp
        // call only actually runs when the period changes, not on every sample of a held note.
        if (!hasCachedFastOutputCoeff || !floatBitsEqual(delaySamples, cachedDelaySamplesForFastOutputCoeff))
        {
            const auto periodSeconds = delaySamples / (float) sampleRateHz;
            const auto effectiveFastOutputTimeSeconds = std::max(fastOutputTimeSeconds, periodSeconds * fastOutputPeriodMultiplier);
            cachedFastOutputCoeff = 1.0f - std::exp(-1.0f / (float) (effectiveFastOutputTimeSeconds * sampleRateHz));
            cachedDelaySamplesForFastOutputCoeff = delaySamples;
            hasCachedFastOutputCoeff = true;
        }
        fastOutputEnvelope += cachedFastOutputCoeff * (instantMagnitude - fastOutputEnvelope);
        slowOutputEnvelope += slowOutputCoeff * (instantMagnitude - slowOutputEnvelope);
        const auto levelingGain = std::clamp(slowOutputEnvelope / std::max(fastOutputEnvelope, 0.02f),
                                              minLevelingGain, maxLevelingGain);
        return output * levelingGain;
    }

    Channel lineA;
    Channel lineB;

    // Couple Delay's own state - two directions, since A-into-B and B-into-A each need their own
    // independent delay history (they're delaying DIFFERENT signals). See renderNextSample()'s own
    // comment for the formula and why this needs no new safety ceiling.
    StrikeShortDelay coupleDelayAToB;
    StrikeShortDelay coupleDelayBToA;

    double sampleRateHz = 44100.0;

    int topology = 0; // 0 = Single, 1 = Dual - see setTopology()
    float crossCoupleAmount = 0.0f;
    float coupleDelayMs = 0.0f;
    float detuneAmount01 = 0.0f;

    // 10ms ceiling - a reasoned starting point (not measured), musically wide enough to sweep the
    // coupling's own first notch frequency (sampleRate/(2*delaySamples)) from far above the audible
    // band down into the low-hundreds-of-Hz range, across the full note range this codebase
    // supports - to be confirmed (or retuned) by listening, same convention as every other new-
    // feature constant in this file. No stability implication either way - see the safety comment
    // above.
    static constexpr float maxCoupleDelaySeconds = 0.01f;

    // ~50 cents (raised from an initial ~20 cent starting point, per the user, once they'd heard
    // it and wanted more range) - unlike Cross-Couple, this constant has no stability ceiling at
    // all (Detune only ever affects line B's target pitch, never a loop gain), so raising it is a
    // pure "does it sound good" call, not a safety one. At 50 cents the two lines can sit clearly
    // apart in pitch (not just "alive"/beating) while staying well short of a full semitone.
    static constexpr float maxDetuneSemitones = 0.50f;

    // See renderNextSample()'s Dual-topology comment for why this is 0.5 (plain averaging) rather
    // than the incoherent-sum 1/sqrt(2).
    static constexpr float dualTopologyOutputGain = 0.5f;

    // See renderNextSample()'s own comment for the Couple Delay loudness drop this compensates
    // for. Tuned by measurement: +2.8dB (1.38x) fully closes the observed gap; 0.3ms time constant
    // matches how quickly the real drop saturates (already near-maximum by the smallest tested
    // nonzero delay, 0.5ms).
    static constexpr float coupleDelayCompensationAmount = 0.38f; // +2.8dB at full saturation
    static constexpr float coupleDelayCompensationTimeConstantMs = 0.3f;

    float fastOutputEnvelope = 0.0f;
    float slowOutputEnvelope = 0.0f;

    // Computed once in prepare() - see that call site's own comment for why this one is a true
    // constant (unlike fastOutputCoeff, which depends on the note's own period).
    float slowOutputCoeff = 0.0f;

    // fastOutputCoeff's own cache - see levelOutput()'s own comment.
    float cachedDelaySamplesForFastOutputCoeff = 0.0f;
    float cachedFastOutputCoeff = 0.0f;
    bool hasCachedFastOutputCoeff = false;

    // fastOutputTimeSeconds is now a FLOOR, not the actual time constant used - see
    // levelOutput()'s own comment. A real, measured bug: a rectify-then-one-pole-smooth envelope
    // follower needs a time constant of several PERIODS to properly resolve amplitude (a
    // well-known envelope-follower property, not specific to this codebase) - the flat 15ms was
    // shorter than a single cycle of this instrument's own lowest supported note at the time this
    // was measured (then MIDI 21, 27.5Hz, ~36ms/cycle - the note range was later widened further
    // down to MIDI 0, ~8.18Hz, ~122ms/cycle, making the adaptive fix below even more load-bearing
    // than when it was written), so fastOutputEnvelope couldn't track low notes at all; instead of settling,
    // the fast/slow ratio (levelingGain) went unstable and pinned near its own 3x ceiling for most
    // of the note, producing a real, audible "swell" - reported by the user as low notes "coming
    // up in an unnatural way" as they rang out (and independently observable, smaller, even at
    // Brightness=100% before this fix - Brightness's own loudness-compensation pass, see
    // StrikeExcitation.cpp, made a PRE-EXISTING artifact newly audible by raising Brightness=0's
    // overall level into a normally-audible range, it didn't introduce the artifact itself).
    // fastOutputPeriodMultiplier (measured: 8 cycles) stretches the effective time constant
    // proportionally to how low the note is - at note 60 (period*8 ~= 31ms, above the 15ms floor)
    // it's already a bit slower than the original flat 15ms, but re-measured to still satisfy the
    // existing "held bow loudness stays bounded window-to-window" regression test's own <1.5x
    // ratio requirement; the floor only matters for the very highest notes, where period*8 drops
    // back under 15ms.
    static constexpr float fastOutputTimeSeconds = 0.015f;
    static constexpr float fastOutputPeriodMultiplier = 8.0f;
    static constexpr float slowOutputTimeSeconds = 0.6f;
    static constexpr float minLevelingGain = 0.3f;
    static constexpr float maxLevelingGain = 3.0f;
};
