#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "KarplunkStringLine.h"
#include "KarplunkWaveshaper.h"

namespace
{
    constexpr float pi = 3.14159265358979323846f;
}

// A generic, reusable cascaded first-order Schroeder allpass primitive - deliberately has no
// notion of "structure" or "dispersion" (the Structure knob's mapping to a gain, and the
// resulting group-delay compensation, live in SingleLineKarplunkVoice::renderNextSample(), not
// here), matching how KarplunkStringLine itself doesn't know about MIDI notes.
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
// renderNextSample()), unlike the large-D case. No delay-line/ring-buffer needed any more - each
// stage is just one float of state.
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
    static constexpr int numStages = 8; // see renderNextSample()'s own comment for how this was chosen

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
        active = false;
        silenceRunSamples = 0;
        fastOutputEnvelope = 0.0f;
        slowOutputEnvelope = 0.0f;
        dispersionNoise = 0.0f; // dispersionRngState deliberately NOT reset - see its own comment
    }

    // Always retriggers - each MIDI note-on is physically a fresh pluck (or bow stroke): the
    // excitation/loop filter/dispersion filter/string content are always fully reset below, no
    // exceptions. `glideTimeSeconds` (0 by default, matching every existing call site - Poly mode
    // and every isolated-Voice test never touch it) affects ONLY the PITCH's approach to its new
    // target, not whether a fresh pluck happens - see the class comment above renderNextSample()'s
    // glide step for why glide-the-pitch-but-still-repluck (not a true legato "no new attack"
    // glide) was the chosen design for Mono. Only actually glides if this voice was already
    // active right before this call (a legato retrigger - there's a previous pitch to glide
    // from); a fresh note struck from silence always snaps straight to its own pitch, matching
    // standard "auto-glide" convention on hardware mono synths.
    void noteOn(int midiNoteNumber, float velocity01, float glideTimeSeconds = 0.0f) noexcept
    {
        const auto wasActive = active; // captured before reset() clears it

        reset();

        const auto clampedNote = std::clamp(midiNoteNumber, kLowestSupportedMidiNote, kHighestSupportedMidiNote);
        targetPitchMidi = (float) clampedNote;

        if (wasActive && glideTimeSeconds > 0.0f)
        {
            // currentPitchMidi deliberately keeps its PRE-reset value here (not touched by
            // reset() - same convention as currentDelaySamples always following the same rule)
            // as the glide's starting point; renderNextSample()'s glide step carries it toward
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
        // renderNextSample()) provides its own energy from t=0. This is mechanically equivalent
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
    // renderNextSample(), which queries the excitation every sample for as long as the note is
    // held. PluginProcessor smooths this the same way it smooths Decay, not latched like
    // Brightness.
    void setBowAmount(float amount01) noexcept
    {
        excitation.setBowAmount(amount01);
    }

    // Live, every-sample, same convention as setBowAmount()/setDamping() - see renderNextSample().
    void setStructure(float amount01) noexcept { structure = amount01; }
    void setPosition(float amount01) noexcept { position = amount01; }

    // Live, every-sample - see renderNextSample()'s waveshaping step. amount01=0 is a bit-exact
    // no-op (neither waveshaper is ever even called in that case), matching Structure's own
    // precedent for a "new control defaults to unchanged behavior" convention.
    void setWaveshapeAmount(float amount01) noexcept { waveshapeAmount = amount01; }

    // Runtime selector between the two concrete waveshapers (0 = Fold, 1 = Fuzz) - see
    // KarplunkWaveshaper.h's own comment for why this seam is a runtime choice rather than a
    // compile-time template parameter like the other three.
    void setWaveshaperType(int type) noexcept { waveshaperType = type; }

    float renderNextSample() noexcept
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
        // KarplunkWaveshaper.h for why this is a runtime choice between two concrete classes
        // (waveFolder/fuzz) rather than a template parameter like the other three seams, and why
        // amount01 is passed directly per-call rather than cached via a setter.
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
            if (waveshaperType == 0)
            {
                filtered = waveFolder.process(preWaveshapeSignal, waveshapeAmount, 1.0f);
                waveshapedForOutput = waveFolder.process(preWaveshapeSignal, waveshapeAmount, foldOutputDriveCompensation);
            }
            else
            {
                // updateFilter() computes and lowpasses the shaped value once (shared by both
                // calls below) - see KarplunkFuzz's own comment for why this differs from
                // KarplunkWaveFolder's shape (Fuzz has real per-sample filter state; Fold doesn't).
                fuzz.updateFilter(preWaveshapeSignal, waveshapeAmount);
                filtered = fuzz.process(waveshapeAmount, 1.0f);
                waveshapedForOutput = fuzz.process(waveshapeAmount, fuzzOutputDriveCompensation);
            }
        }

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

        // Position: an independent, non-recursive second read of the same string, combined with
        // the OUTPUT only - never written back (stringLine.write() above already used the
        // original `filtered` signal, so the loop's own pitch/decay/stability is untouched). This
        // is the classic physical-modeling "excite/listen at a different point along the string"
        // effect: a real string excited/read at position p has zero energy at every harmonic n
        // where n*p is an integer (a node falls exactly there) - at p=0.5 (the exact midpoint),
        // every EVEN harmonic is missing, giving the "hollow, square-wave-like" character the
        // Position control is meant to produce. clampedPosition folds the knob symmetrically into
        // roughly [0.01, 0.5] (matches Mutable Instruments Rings' own formula, the reference this
        // is modeled on) - avoiding a degenerate near-zero-length tap while keeping the effect
        // symmetric around the string's exact midpoint.
        //
        // The cancellation only exists in the INTERFERENCE between filtered and positionTap, not
        // in positionTap alone - a single read of a periodic signal at any phase has identical
        // harmonic MAGNITUDES to any other read of it (phase-shifting can't remove energy from a
        // harmonic, only rotate its phase), confirmed by measuring harmonic content of positionTap
        // alone across the whole Position range and finding it literally unchanged. Combining the
        // two - filtered's k-th harmonic component summed with a copy phase-shifted by
        // clampedPosition's own fraction of a cycle - is what creates real magnitude cancellation.
        // The SIGN matters: `filtered + positionTap` (tried first) cancels ODD harmonics at
        // p=0.5 (confirmed by the same math, then measured), the wrong polarity; `filtered -
        // positionTap` cancels EVEN harmonics there, matching the physically-correct, documented
        // behavior - verified by measurement, not assumed. positionOutputGain scales the
        // subtracted tap - tuned empirically (see git history for the measured numbers), not
        // guessed.
        const auto clampedPosition = 0.5f - 0.98f * std::abs(position - 0.5f);
        const auto positionTap = stringLine.readAt(currentDelaySamples * clampedPosition);
        const auto output = waveshapedForOutput - positionOutputGain * positionTap;

        // Loudness leveling: tame the natural, audible loudness "warble" a noise-driven resonant
        // loop produces - raw noise circulating in a high-Q feedback loop has energy that
        // fluctuates on a timescale set by the LOOP's own ring/decay time, not by the noise's own
        // instantaneous variance (measured ~50% window-to-window RMS swings at high Decay/Bow with
        // every parameter held perfectly still - confirmed empirically that this is not something
        // turning the Bow knob introduces, the actual root cause behind a user report of "the
        // volume goes up and down drastically" while doing so). A fast/slow envelope-ratio
        // leveler, output-only - it operates on a copy of `output`, never feeding back into
        // `stringLine` (which already wrote the un-leveled `filtered` above), so it cannot alter
        // the loop's own decay/stability/character. fastOutputEnvelope tracks roughly "how loud
        // right now"; slowOutputEnvelope tracks "the recent sustained average" - long enough to
        // average out the fluctuation, short enough to still track (not fight) an intentionally
        // decaying pluck, whose own decay happens on a much longer timescale at any musically
        // useful Decay setting.
        const auto instantMagnitude = std::abs(output);
        const auto fastOutputCoeff = 1.0f - std::exp(-1.0f / (float) (fastOutputTimeSeconds * sampleRateHz));
        const auto slowOutputCoeff = 1.0f - std::exp(-1.0f / (float) (slowOutputTimeSeconds * sampleRateHz));
        fastOutputEnvelope += fastOutputCoeff * (instantMagnitude - fastOutputEnvelope);
        slowOutputEnvelope += slowOutputCoeff * (instantMagnitude - slowOutputEnvelope);
        const auto levelingGain = std::clamp(slowOutputEnvelope / std::max(fastOutputEnvelope, 0.02f),
                                              minLevelingGain, maxLevelingGain);

        return output * levelingGain;
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
    // why this one seam works differently from Excitation/Loop Filter/Delay Tuning. Both concrete
    // types live here unconditionally (no polymorphism/vtable), selected per-sample by
    // `waveshaperType` in renderNextSample().
    KarplunkWaveFolder waveFolder;
    KarplunkFuzz fuzz;

    int capacitySamples = 0;
    double sampleRateHz = 44100.0;
    float currentDelaySamples = 0.0f;

    // Glide state (see noteOn()'s and renderNextSample()'s own comments) - none of these are
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
    int waveshaperType = 0; // 0 = Fold, 1 = Fuzz - see setWaveshaperType()

    // See renderNextSample()'s comment on the two separate waveshaper calls per type - how much
    // drive compensation the OUTPUT-only path gets (0 = none/loudest, 1 = full/matches the
    // recirculating path). Tuned by measurement, per waveshaper (they saturate differently, so
    // there's no reason to expect the same number would suit both) - placeholder pending real
    // render/measure iteration for KarplunkFuzz specifically.
    static constexpr float foldOutputDriveCompensation = 0.0f;
    static constexpr float fuzzOutputDriveCompensation = 0.0f;

    // Defaults to the string's midpoint (clampedPosition = 0.5, the maximum tap fraction), not 0
    // - 0 folds to clampedPosition = 0.01, a near-zero-length tap that's most correlated with
    // `filtered` and closest to doubling the output; 0.5 is where the tap is least correlated
    // with the main signal (the classic "plucking a string at its middle" partial-cancellation
    // point), matching the intended UI default and avoiding an accidental worst-case default for
    // anything (like these tests) that constructs a Voice without ever calling setPosition().
    float position = 0.5f;

    // Injected-signal loudness at full bow, applied on top of the sqrt(1-loopGain) compensation
    // above - tuned empirically by rendering and measuring actual steady-state RMS against a
    // plucked note's peak level (not by reasoning about the loop's theoretical gain alone - see
    // this class's own renderNextSample() comment, and git history/PR discussion for the
    // measured numbers this was calibrated against).
    static constexpr float continuousLevelAnalog = 4.0f;

    // Scales the subtracted tap in `filtered - positionOutputGain * positionTap` - see
    // renderNextSample()'s own comment for why subtraction (not addition) gives the
    // physically-correct even-harmonic cancellation at Position = 50%. Tuned empirically, not
    // guessed.
    static constexpr float positionOutputGain = 1.0f;

    // Structure's per-stage allpass gain at full Structure (100%) - see
    // KarplunkDispersionFilter's own comment and renderNextSample()'s Structure comment for how
    // this and numStages were chosen via closed-form arithmetic, not by feel.
    static constexpr float maxDispersionGain = 0.5f;

    // Structure's noise-driven delay-FM state (see renderNextSample()'s comment) - dispersionNoise
    // is the running lowpassed noise value; dispersionRngState is nextDispersionUniformNoise()'s
    // own xorshift32 state, seeded to a fixed non-zero constant (xorshift is degenerate at 0).
    uint32_t dispersionRngState = 1;
    float dispersionNoise = 0.0f;

    // Fixed stand-in for Rings' brightness-derived noise_filter (SemitonesToRatio((brightness-1)*
    // 48) - deliberately not tied to Karplunk's own Brightness knob, a different, independent
    // control (excitation tone). 0.25 matches Rings' own noise_filter value at ITS default
    // brightness (0.5) - a reasoned starting point, not a final tuning; see git history if this
    // gets revisited after listening.
    static constexpr float dispersionNoiseFilterCoeff = 0.25f;

    // See renderNextSample()'s loudness-leveling comment.
    float fastOutputEnvelope = 0.0f;
    float slowOutputEnvelope = 0.0f;
    static constexpr float fastOutputTimeSeconds = 0.015f;
    static constexpr float slowOutputTimeSeconds = 0.6f;
    static constexpr float minLevelingGain = 0.3f;
    static constexpr float maxLevelingGain = 3.0f;
};
