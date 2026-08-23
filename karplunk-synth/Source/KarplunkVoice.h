#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "KarplunkRingModulator.h"
#include "KarplunkStringLine.h"
#include "KarplunkWaveshaper.h"

namespace
{
    constexpr float pi = 3.14159265358979323846f;
}

// A generic, reusable cascaded first-order Schroeder allpass primitive - deliberately has no
// notion of "structure" or "dispersion" (the Structure knob's mapping to a gain, and the
// resulting group-delay compensation, live in KarplunkStringLineChannel::renderChannelSample(),
// not here), matching how KarplunkStringLine itself doesn't know about MIDI notes.
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
class KarplunkDispersionFilter
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
// KarplunkVoice::renderNextSample()'s Dual-topology coupling comment) - deliberately NOT
// KarplunkStringLine: that class is sized for a note's own pitch period, uses fractional
// interpolation, and has bulk-priming semantics tied to Karplus-Strong noteOn, none of which apply
// here. Couple Delay shapes the COUPLING PATH's own frequency response, not a note's pitch, so
// sub-sample precision buys nothing - this is just a plain ring buffer, sized once in prepare().
class KarplunkShortDelay
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

// KarplunkStringLineChannel: everything that is genuinely PER-STRING - one delay line, one loop
// filter, one excitation, one dispersion filter, one full set of Waveshapers/Ring Modulator, and
// all the pitch/glide/silence-tracking state that goes with a single resonating line. This used
// to BE the whole voice (as `SingleLineKarplunkVoice`) before the Feedback Topology seam grew a
// second option: a dual cross-coupled topology needs two of everything in this class, so this is
// now the reusable "one string" building block, and `KarplunkVoice` below is the orchestrator
// that owns one or two of these and decides how they're wired together.
//
// The split is a pure refactor for the Single-topology case: `renderChannelSample()` is today's
// former `renderNextSample()` body up to (not including) `stringLine.write()`, which moved out
// into `writeBack()` so a voice-level orchestrator can intercept the value between "computed" and
// "written back" (that interception point is exactly where cross-coupling happens - see
// `KarplunkVoice::renderNextSample()`). Same operations, same order, same formulas - Single
// topology's output is bit-exact with what this whole file produced before this split existed.
template <typename Excitation, typename LoopFilter, typename InterpolationType = LinearInterpolator>
class KarplunkStringLineChannel
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
    // prepare(). A dual-topology voice needs two of these buffers, not one - still trivial (a
    // few tens of KB across the whole 8-voice pool at worst).
    static int requiredCapacitySamples(double sampleRate) noexcept
    {
        constexpr double lowestSupportedHz = 27.5;
        constexpr double headroomFactor = 1.15;
        constexpr int interpolationPad = 8;
        return (int) std::ceil(sampleRate / lowestSupportedHz * headroomFactor) + interpolationPad;
    }

    // Allocates (via KarplunkStringLine::prepare) - only ever call this from
    // PluginProcessor::prepareToPlay, never from the audio thread.
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = sampleRate;
        capacitySamples = requiredCapacitySamples(sampleRate);

        excitation.prepare(sampleRate);
        loopFilter.prepare(sampleRate);
        stringLine.prepare(sampleRate, capacitySamples);
        dispersionFilter.prepare();
        waveFolder.prepare(sampleRate);
        fuzz.prepare(sampleRate);
        saturator.prepare(sampleRate);
        bitCrush.prepare(sampleRate);
        ringMod.prepare(sampleRate);

        silenceHoldSamples = (int) (sampleRate * 0.05); // ~50ms

        reset();
    }

    void reset() noexcept
    {
        excitation.reset();
        loopFilter.reset();
        stringLine.reset();
        dispersionFilter.reset();
        waveFolder.reset();
        fuzz.reset();
        saturator.reset();
        bitCrush.reset();
        ringMod.reset();
        active = false;
        silenceRunSamples = 0;
        dispersionNoise = 0.0f; // dispersionRngState deliberately NOT reset - see its own comment
    }

    // Gives this channel's Excitation and dispersion-noise generator a genuinely different noise
    // sequence from another channel's default state - called exactly once, by KarplunkVoice's
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
    // `semitoneOffset` (0 by default) is Detune's own doing - KarplunkVoice's second line passes
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
    // bowAmount-dependent time rather than being cut off instantly (see KarplunkExcitation.h). A
    // plucked string (bowAmount at or near 0) physically continues ringing after release
    // regardless, via the loop filter's own decay, same mechanism a bowed string then uses too -
    // physically correct (a released bow doesn't instantly silence the string). No
    // real-time-safety implication (pure arithmetic state change, no allocation).
    void noteOff() noexcept { excitation.noteOff(); }

    void setDamping(float amount01) noexcept
    {
        loopFilter.setDamping(amount01);
    }

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

    // Unlike setBrightness(), this DOES take effect immediately, live, while a note rings - see
    // renderChannelSample(), which queries the excitation every sample for as long as the note is
    // held. PluginProcessor smooths this the same way it smooths Decay, not latched like
    // Brightness.
    void setBowAmount(float amount01) noexcept
    {
        excitation.setBowAmount(amount01);
    }

    // Live, every-sample, same convention as setBowAmount()/setDamping() - see renderChannelSample().
    void setStructure(float amount01) noexcept { structure = amount01; }
    void setPosition(float amount01) noexcept { position = amount01; }

    // Live, every-sample - see renderChannelSample()'s waveshaping step. amount01=0 is a bit-exact
    // no-op (neither waveshaper is ever even called in that case), matching Structure's own
    // precedent for a "new control defaults to unchanged behavior" convention.
    void setWaveshapeAmount(float amount01) noexcept { waveshapeAmount = amount01; }

    // Runtime selector between the four concrete waveshapers (0 = Fold, 1 = Fuzz, 2 = Saturate,
    // 3 = BitCrush) - see KarplunkWaveshaper.h's own comment for why this seam is a runtime choice
    // rather than a compile-time template parameter like the other three.
    void setWaveshaperType(int type) noexcept { waveshaperType = type; }

    // Live, every-sample - see renderChannelSample()'s ring modulation step (applied in-loop,
    // after the Waveshaper). amount01=0 is a bit-exact no-op (the oscillator isn't even advanced
    // in that case), same convention as Waveshape.
    void setRingModAmount(float amount01) noexcept { ringModAmount = amount01; }
    void setRingModFrequency(float hz) noexcept { ringModFrequency = hz; }

    // What renderNextSample() used to compute in one pass: the value about to be written back
    // into the loop (`filtered`) and the value about to be heard (`forOutput`) - see
    // KarplunkWaveFolder's own comment for why those two are deliberately different signals.
    // Deliberately does NOT write to `stringLine` or touch the silence-tracking/Position-tap
    // state - see writeBack()/positionOutput() below, and KarplunkVoice::renderNextSample() for
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
        // KarplunkDispersionFilter's own comment for why a cascade of small stages replaced the
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
        // Karplunk's own Brightness knob (a different control, the excitation's own tone) would
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
            const auto totalPhase = (float) KarplunkDispersionFilter::numStages * stagePhase;
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

        auto filtered = loopFilter.processSample(delayed);

        // Unified excitation injection: unconditional, every tick (no more `held` gate) - an idle
        // (never-triggered) or fully-released excitation just returns ~0 on its own, see
        // KarplunkExcitation.h. One bowAmount-dependent gain curve, not two independently-tuned
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
        const auto fullBowGain = continuousLevelAnalog * std::sqrt(1.0f - loopFilter.getLoopGain());
        const auto injectionGain = 1.0f + excitation.getBowAmount() * (fullBowGain - 1.0f);
        filtered += std::tanh(excitation.nextExcitationSample(noteVelocity) * injectionGain);

        // Waveshaper: nonlinearly reshapes the COMBINED signal (recirculating loop content plus
        // this tick's freshly injected excitation) right before it's written back - so the
        // distortion becomes part of what the string is actually resonating with, compounding
        // every pass around the loop, not a one-shot effect applied only to the output. See
        // KarplunkWaveshaper.h for why this is a runtime choice between concrete classes rather
        // than a template parameter like the other three seams, and why amount01 is passed
        // directly per-call rather than cached via a setter.
        //
        // Two SEPARATE calls per waveshaper, not one shared value, despite both starting from the
        // same pre-waveshape `filtered` - discovered by measuring, not planned upfront, that "safe
        // to feed back into the loop" and "sounds right on the output" are genuinely different
        // requirements at high drive (see KarplunkWaveFolder's own comment for the full story).
        // The write-back call always uses full drive compensation (driveCompensation=1, the
        // safety-critical default) since it's what actually recirculates; the output call uses
        // much less, tuned per-waveshaper (foldOutputDriveCompensation/fuzzOutputDriveCompensation)
        // by rendering and measuring loudness parity, since output is never fed back and full
        // compensation was measured crushing the fold's own audible character almost to silence at
        // high drive.
        const auto preWaveshapeSignal = filtered;
        float waveshapedForOutput = filtered;
        if (waveshapeAmount > 0.0f)
        {
            // Per-type knob range compression: the user found the full 0-100% Waveshape turn
            // pushed each waveshaper past a musically usable point well before reaching 100%, at
            // different rates per type. The knob's displayed 0-100% is unchanged (still what
            // PluginProcessor reads/smooths/shows) - only how far that maps into each
            // waveshaper's own amount01 range is rescaled, per type, so the full physical turn
            // stays useful across its whole travel instead of the musically relevant part being
            // crammed into the first fraction of it. amount01=0 is unaffected either way (this
            // whole block is already skipped above at waveshapeAmount=0).
            if (waveshaperType == 0)
            {
                const auto effectiveAmount = waveshapeAmount * foldMaxAmountFraction;
                filtered = waveFolder.process(preWaveshapeSignal, effectiveAmount, 1.0f);
                waveshapedForOutput = waveFolder.process(preWaveshapeSignal, effectiveAmount, foldOutputDriveCompensation);
            }
            else if (waveshaperType == 1)
            {
                // updateFilter() computes and lowpasses the shaped value once (shared by both
                // calls below) - see KarplunkFuzz's own comment for why this differs from
                // KarplunkWaveFolder's shape (Fuzz has real per-sample filter state; Fold doesn't).
                const auto effectiveAmount = waveshapeAmount * fuzzMaxAmountFraction;
                fuzz.updateFilter(preWaveshapeSignal, effectiveAmount);
                filtered = fuzz.process(effectiveAmount, 1.0f);
                waveshapedForOutput = fuzz.process(effectiveAmount, fuzzOutputDriveCompensation);
            }
            else if (waveshaperType == 2)
            {
                const auto effectiveAmount = waveshapeAmount * saturatorMaxAmountFraction;
                saturator.updateFilter(preWaveshapeSignal, effectiveAmount);
                filtered = saturator.process(effectiveAmount, 1.0f);
                waveshapedForOutput = saturator.process(effectiveAmount, saturatorOutputDriveCompensation);
            }
            else
            {
                // KarplunkBitCrush's process() takes no amount/driveCompensation parameters at
                // all - quantization/sample-hold can't amplify a signal, so there's no loop-safety
                // or output-loudness split to make (see its own class comment) - the identical
                // crushed value is used for both the recirculating and output paths.
                const auto effectiveAmount = waveshapeAmount * bitCrushMaxAmountFraction;
                bitCrush.updateFilter(preWaveshapeSignal, effectiveAmount);
                filtered = bitCrush.process();
                waveshapedForOutput = filtered;
            }
        }

        // Ring Modulator: applied in-loop, after the Waveshaper, right before writing back - so
        // the modulated signal itself becomes part of what's actually resonating (the user's
        // explicit choice - see KarplunkRingModulator.h's own comment for the alternative
        // considered, output-only, and why this needed its own area rather than being a fifth
        // Waveshaper Type). updateOscillator() advances the shared phase exactly once per sample;
        // both process() calls below read the SAME oscillator sample so the recirculating and
        // audible paths stay in sync - no driveCompensation-style split needed at all (ring
        // modulation can only ever shrink or invert a signal, never amplify it, see process()'s
        // own comment).
        if (ringModAmount > 0.0f)
        {
            ringMod.updateOscillator(ringModFrequency);
            filtered = ringMod.process(filtered, ringModAmount);
            waveshapedForOutput = ringMod.process(waveshapedForOutput, ringModAmount);
        }

        return { filtered, waveshapedForOutput };
    }

    // Writes the (possibly cross-coupled - see KarplunkVoice::renderNextSample()) value back into
    // this channel's own delay line, and updates this channel's silence tracking - split out from
    // renderChannelSample() specifically so a voice-level orchestrator can substitute a different
    // value than the one this channel itself computed (Dual topology's whole point). At Single
    // topology, KarplunkVoice always calls this with EXACTLY the value renderChannelSample() just
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

private:
    // xorshift32, same technique/rationale as NoiseExcitation::nextNoiseSample() (deterministic,
    // allocation-free, no JUCE dependency) - a separate RNG/state from the excitation's own noise,
    // since this drives delay-length FM (pitch), not the injected excitation signal itself. Never
    // reset in reset()/noteOn() (matching NoiseExcitation's own convention), so consecutive notes
    // on the same voice get non-repeating noise while a freshly-constructed processor stays fully
    // deterministic - relied on by KarplunkProcessorTests' "two fresh processors render bit-
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
    float delaySamplesForPitch(float midiNoteFloat) const noexcept
    {
        const auto frequencyHz = 440.0 * std::pow(2.0, ((double) midiNoteFloat - 69.0) / 12.0);
        const auto delaySamples = (float) (sampleRateHz / frequencyHz);

        // Clamps interpolation-quality floor above kHighestSupportedMidiNote - not a
        // real-time-safety concern, just protects fractional-delay accuracy at very short delays.
        return std::max(8.0f, delaySamples);
    }

    Excitation excitation;
    LoopFilter loopFilter;
    KarplunkStringLine<InterpolationType> stringLine;
    KarplunkDispersionFilter dispersionFilter;
    // Runtime-selectable, not a template parameter - see KarplunkWaveshaper.h's own comment for
    // why this one seam works differently from Excitation/Loop Filter/Delay Tuning. All four
    // concrete types live here unconditionally (no polymorphism/vtable), selected per-sample by
    // `waveshaperType` in renderChannelSample().
    KarplunkWaveFolder waveFolder;
    KarplunkFuzz fuzz;
    KarplunkSaturator saturator;
    KarplunkBitCrush bitCrush;

    // Its own area, not a fifth Waveshaper type - see KarplunkRingModulator.h's own comment.
    KarplunkRingModulator ringMod;

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
    int waveshaperType = 0; // 0 = Fold, 1 = Fuzz, 2 = Saturate, 3 = BitCrush - see setWaveshaperType()
    float ringModAmount = 0.0f;
    float ringModFrequency = 200.0f; // matches PluginProcessor's own default

    // See renderChannelSample()'s comment on the two separate waveshaper calls per type - how much
    // drive compensation the OUTPUT-only path gets (0 = none/loudest, 1 = full/matches the
    // recirculating path). Tuned by measurement, per waveshaper (they saturate differently, so
    // there's no reason to expect the same number would suit both) - placeholder pending real
    // render/measure iteration for KarplunkFuzz and KarplunkSaturator specifically.
    static constexpr float foldOutputDriveCompensation = 0.0f;
    static constexpr float fuzzOutputDriveCompensation = 0.0f;
    static constexpr float saturatorOutputDriveCompensation = 0.0f;

    // Waveshape knob range compression - see renderChannelSample()'s own comment. Tuned directly
    // by the user (not measured/derived), one per waveshaper type since each one reaches "too
    // much" at a different point on the knob's travel.
    static constexpr float foldMaxAmountFraction = 0.59f;
    static constexpr float fuzzMaxAmountFraction = 0.20f;
    static constexpr float saturatorMaxAmountFraction = 0.30f;
    static constexpr float bitCrushMaxAmountFraction = 1.0f; // not yet tuned by ear - full range for now

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

    // Scales the subtracted tap in `forOutput - positionOutputGain * positionTap` - see
    // positionOutput()'s own comment for why subtraction (not addition) gives the
    // physically-correct even-harmonic cancellation at Position = 50%. Tuned empirically, not
    // guessed.
    static constexpr float positionOutputGain = 1.0f;

    // Structure's per-stage allpass gain at full Structure (100%) - see
    // KarplunkDispersionFilter's own comment and renderChannelSample()'s Structure comment for how
    // this and numStages were chosen via closed-form arithmetic, not by feel.
    static constexpr float maxDispersionGain = 0.5f;

    // Structure's noise-driven delay-FM state (see renderChannelSample()'s comment) - dispersionNoise
    // is the running lowpassed noise value; dispersionRngState is nextDispersionUniformNoise()'s
    // own xorshift32 state, seeded to a fixed non-zero constant (xorshift is degenerate at 0) -
    // overridable via setNoiseSeed() (see its own comment) for a second, independently-noisy line.
    uint32_t dispersionRngState = 1;
    float dispersionNoise = 0.0f;

    // Fixed stand-in for Rings' brightness-derived noise_filter (SemitonesToRatio((brightness-1)*
    // 48) - deliberately not tied to Karplunk's own Brightness knob, a different, independent
    // control (excitation tone). 0.25 matches Rings' own noise_filter value at ITS default
    // brightness (0.5) - a reasoned starting point, not a final tuning; see git history if this
    // gets revisited after listening.
    static constexpr float dispersionNoiseFilterCoeff = 0.25f;
};

// KarplunkVoice: the Feedback Topology seam's orchestrator. Owns one or two
// KarplunkStringLineChannel instances and decides how they're wired together - Single topology
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
// strings, not a mistuning trick. `setNoiseSeed()` (see KarplunkStringLineChannel's own comment)
// is how this class gives lineB that independence, at prepare() time.
template <typename Excitation, typename LoopFilter, typename InterpolationType = LinearInterpolator>
class KarplunkVoice
{
public:
    using Channel = KarplunkStringLineChannel<Excitation, LoopFilter, InterpolationType>;

    // Mirrors Channel's own constants exactly (both must stay in sync - a compile-time assertion
    // isn't practical across two independently-instantiable templates, so this is a documented
    // invariant, not an enforced one) - kept here too since existing call sites (PluginProcessor,
    // every isolated-voice test) reference `Voice::kLowestSupportedMidiNote` directly, not
    // `Voice::Channel::kLowestSupportedMidiNote`.
    static constexpr int kLowestSupportedMidiNote = 21;
    static constexpr int kHighestSupportedMidiNote = 108;

    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = sampleRate;
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
    void setBrightness(float amount01) noexcept { lineA.setBrightness(amount01); lineB.setBrightness(amount01); }
    void setBowAmount(float amount01) noexcept { lineA.setBowAmount(amount01); lineB.setBowAmount(amount01); }
    void setStructure(float amount01) noexcept { lineA.setStructure(amount01); lineB.setStructure(amount01); }
    void setPosition(float amount01) noexcept { lineA.setPosition(amount01); lineB.setPosition(amount01); }
    void setWaveshapeAmount(float amount01) noexcept { lineA.setWaveshapeAmount(amount01); lineB.setWaveshapeAmount(amount01); }
    void setWaveshaperType(int type) noexcept { lineA.setWaveshaperType(type); lineB.setWaveshaperType(type); }
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
    // bit-exact match for the original (undelayed) coupling formula - see KarplunkShortDelay's own
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
            // Single topology: bit-exact with this file's pre-refactor behavior - lineB is never
            // touched this tick at all (not rendered, not written back, not read from), so it
            // costs nothing and cannot perturb lineA in any way.
            const auto a = lineA.renderChannelSample();
            lineA.writeBack(a.filtered);
            return levelOutput(lineA.positionOutput(a.forOutput));
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
        //     lines get the IDENTICAL setDamping() call every sample (see setDamping() above), so
        //     their loop gains g are EXACTLY equal, always - not approximately. Common-mode
        //     round-trip gain per pass is exactly g (unaffected by coupling, already proven safe by
        //     every existing Single-topology test); differential-mode gain is g*(1-2c), and since
        //     (1-2c) in [-1,1] for c in [0,1], |g*(1-2c)| <= g always. TwoPointAverageLoopFilter
        //     hard-clamps g to [0.90, 0.9995] (see KarplunkLoopFilter.h), so BOTH modes stay
        //     strictly contractive (< 1) for every Damping setting and the ENTIRE Cross-Couple
        //     range - a strictly stronger guarantee than Structure needed, since cross-coupling is
        //     incapable, by construction, of raising either mode's gain above what an uncoupled
        //     line already safely has.
        //
        // Couple Delay inserts a short, fixed integer-sample delay into EACH direction of the
        // coupling path (KarplunkShortDelay - a plain ring buffer, no fractional interpolation
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
        // headroom choice, not the incoherent-power-sum 1/sqrt(2) polyHeadroomGain uses - the two
        // coupled lines aren't statistically independent the way pooled voices are, so the more
        // conservative constant was chosen deliberately; flagged for level-matching against Single
        // topology by ear, same as polyHeadroomGain itself was originally reasoned rather than
        // measured.
        const auto outputA = lineA.positionOutput(a.forOutput);
        const auto outputB = lineB.positionOutput(b.forOutput);
        return levelOutput(dualTopologyOutputGain * (outputA + outputB));
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
    float levelOutput(float output) noexcept
    {
        const auto instantMagnitude = std::abs(output);
        const auto fastOutputCoeff = 1.0f - std::exp(-1.0f / (float) (fastOutputTimeSeconds * sampleRateHz));
        const auto slowOutputCoeff = 1.0f - std::exp(-1.0f / (float) (slowOutputTimeSeconds * sampleRateHz));
        fastOutputEnvelope += fastOutputCoeff * (instantMagnitude - fastOutputEnvelope);
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
    KarplunkShortDelay coupleDelayAToB;
    KarplunkShortDelay coupleDelayBToA;

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

    float fastOutputEnvelope = 0.0f;
    float slowOutputEnvelope = 0.0f;
    static constexpr float fastOutputTimeSeconds = 0.015f;
    static constexpr float slowOutputTimeSeconds = 0.6f;
    static constexpr float minLevelingGain = 0.3f;
    static constexpr float maxLevelingGain = 3.0f;
};
