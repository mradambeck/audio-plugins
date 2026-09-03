#pragma once

#include <array>

#include "../../common/dsp/CircularDelayBuffer.h"
#include "../../common/dsp/OnePoleFilter.h"
#include "../../common/dsp/TiltFilter.h"

// The AMS RMX16 "Ambience" reverb core: an 8-line Hadamard-mixed FDN, direct hand-port of
// ml-toolkit/effects/ambience/model.py's AmbienceFDN (the model actually fit against 65 real
// captures - see ml-toolkit/effects/ambience/findings.md), not a re-derivation from scratch.
//
// Deliberately simpler than IntruderFDNEngine/ShieldsFDNEngine: no input diffuser, no envelope
// shaper/gate-retrigger, no delay-line wobble. findings.md found Ambience needs none of those -
// short-Time captures show individually-resolvable early echoes and longer-Time captures a dense
// wash, both consistent with one fixed-topology FDN's natural behavior at different feedback
// gains, not a separate early-reflection or gating mechanism (unlike Intruder's Non-Lin 2, which
// genuinely needed a hold/decay envelope stage - see IntruderFDNEngine.h's own class comment).
//
// Signal flow per line, matching AmbienceFDN.forward()'s frequency-domain derivation exactly
// (see core/dsp_primitives.py's module docstring for why the Python side fits via frequency-
// sampling rather than simulating this same per-sample loop - fitting speed only, this engine IS
// the per-sample simulation):
//   read (per-line delay) -> per-line HF damping (OnePoleFilter, one shared weight, independent
//   state per line) -> Hadamard mix -> per-line low/high-band feedback shelf (TiltFilter, one
//   shared low/high gain pair, independent state per line - this is what AmbienceFDN's
//   `feedback_response` shelf becomes in the time domain) -> write back with new input injected.
//
// highBandGain/lowBandGain/dampingWeight are DSP-ready coefficients from
// AuraParameterMap::mapTimeAndHighToBandGains(), not raw Time/High values - same separation of
// concerns as Intruder/Shields (the engine has no measured-data/interpolation logic of its own).
//
// One acknowledged, not-yet-independently-validated deviation from the fitted model: the Python
// fit was against MONO-mixed captures (core/io.py's load_audio mixes L+R down before fitting, and
// AmbienceFDN.forward() renders a single mono waveform), but this engine splits the 8 lines
// across stereo (4 lines feeding each of L/R, even/odd convention) for a proper stereo image,
// matching every other reverb in this catalog (Shields, Intruder). Overall decay time shouldn't
// meaningfully change (both halves keep a similar geometric spread of delay lengths and the same
// gain/damping values), but each channel's echo density is now lower than what B8's cross-
// validation actually measured (which compared mono renders throughout). Check this specifically
// in Phase D - if the real plugin's per-channel density reads noticeably sparser than the
// reference captures, feeding all 8 lines from a mono sum of the dry input (at the cost of stereo
// width) is the fallback.
// Real-unit specs (supplied by Adam, 2026-09-02) and what this engine does about each:
//
//   - 20Hz-18kHz +-0.25dB, 18kHz audio bandwidth. The 18kHz bandwidth is REAL and measurable in
//     the captures - relative to their own 1-4kHz level they are flat to ~15kHz then fall -2.3dB
//     at 17-18k, -3.8dB at 18-19k, -6.5dB at 19-20k and -14.4dB at 20-22k. That cliff is a
//     converter-grade elliptic response. DELIBERATELY NOT MODELLED: at 44.1kHz you cannot build
//     it from one-poles (four cascaded at 18kHz gives -12dB AT 18kHz and darkens 6-15kHz by
//     2-5dB), and a single gentle pole standing in for it was measured to make things worse, not
//     better - it consumed the last of the in-loop damping headroom, driving Time>=3.0s settings
//     too dark with damping already pinned at its 0.99 ceiling (tonal balance 8.89dB vs the
//     9.31dB target at Time=5.5s). Since 18-22kHz is inaudible, paying an audible-band cost to
//     approximate an inaudible cliff is a bad trade. Doing it properly needs a high-order
//     elliptic/biquad cascade, which common/dsp/ has no primitive for yet.
//
//     Note the +-0.25dB flatness spec ALSO means the low-frequency rolloff modelled by
//     inputHighPassL below is NOT the unit's I/O response (an earlier version of that comment
//     claimed it was - wrong, corrected). The rolloff is genuinely present in the captures but
//     its origin is unconfirmed: most likely the Ambience program's own low-frequency behaviour,
//     possibly the capture chain. The filter stays because it's what measurably matches; only the
//     stated cause was wrong.
//
//   - 16-bit converters, 18-bit quantisation, ~90dB dynamic range. MODELLED as a player-facing
//     Bit Depth knob (setBitDepth(), 8-24 range, default 16) rather than baked in - copied from
//     ShieldsFDNEngine's own bit-depth quantiser (same formula, same output-stage placement).
//     Measured directly against the captures (findings.md's Quantization section, 2026-09-02):
//     every long-decay capture's exponential tail bottoms out at almost exactly -90dB relative to
//     its own peak before dropping into the capture chain's own (quieter, confirmed-lower) noise
//     floor - that ceiling matches the supplied ~90dB spec and is what a real 16-bit-class
//     converter delivers in practice (short of the ~98dB theoretical limit), hence the default of
//     16. A literal step-lattice test on the raw 24-bit samples came back negative (GCD=1) - these
//     are analog re-captures through the unit's own D/A and the interface's own A/D, which erases
//     a literal digital step pattern even where one existed upstream - so this default is
//     calibrated to the measured dynamic-range ceiling, not to directly-observed quantization
//     grain.
//
//   - THD <0.03% (about -70dB). NOT modelled. An impulse response barely reveals THD, so there is
//     nothing in the current capture set to calibrate a saturation stage against.
//
class AuraFDNEngine
{
public:
    AuraFDNEngine() = default;

    void prepare(double sampleRate);
    void reset();

    void setPreDelayMs(float ms);

    // Feedback-path coefficients (already mapped from Time/High via AuraParameterMap) - clamped
    // here to a safe ceiling below 1.0 regardless of what the caller passes in. Not just paranoia:
    // FittedCurve1D linearly extrapolates beyond the 0.1-5.5s/-8..0dB measured range, and the UI's
    // own Time range extends past 5.5s (see PluginProcessor.cpp) - an extrapolated gain that
    // crept past unity would stop the signal decaying entirely rather than just decaying slowly,
    // the same class of bug the model-fitting side hit and fixed with a bounded
    // reparameterization (see AmbienceFDN's class comment) - this is that same ceiling's
    // real-time-engine equivalent.
    void setBandGains(float highBandGain, float lowBandGain);
    void setDampingWeight(float weight);

    // Input-stage tilt (High's onset-tone effect - see the class comment's "Real bug found" note
    // below). tiltDb is a DSP-ready coefficient (delta from the engine's own neutral baseline,
    // matching IntruderParameterMap::mapTiltDbFromH's convention exactly), not a raw High value -
    // AuraParameterMap::mapInputTiltDb() does that mapping.
    void setInputTilt(float tiltDb);

    // Corner frequency of the fixed input high-pass modelling the real unit's own I/O rolloff
    // (see inputHighPassL's comment). Exposed only so the calibration probe can sweep it; the
    // processor never calls this - the default from prepare() is the calibrated value.
    void setInputHighPassHz(float hz);

    // Player-facing "Low Cut" utility control (0-300Hz, default 0 = off) - NOT the fixed algorithm
    // rolloff above. This used to be the real hardware's own "Low" knob, carried on the plugin
    // unwired (see findings.md's Low section): direct measurement found it has no onset-tone
    // effect and only a small, sign-inconsistent decay effect conditional on High, nothing safe to
    // hardcode into a formula. Repurposed into a genuinely useful control instead. Applied to the
    // DRY input, before pre-delay and everything else in the tank - see the class comment's
    // "Signal flow" note - so it trims content before it ever reaches the reverb, same placement
    // reasoning as ShieldsFDNEngine::setLowCutHz's own "so the player can trim rumble/mud before it
    // ever reaches the reverb rather than filtering the already-diffused wet output." Reuses the
    // shared OnePoleFilter's x-lowpass(x) idiom already used for inputHighPassL/R just below,
    // rather than Shields' resonant Biquad - that low-Q shape was a deliberate tonal-calibration
    // choice specific to Shields' own algorithm, not a general convention; this is a plain utility
    // cut with no reference measurement to match. 0Hz is a GENUINE bypass (see lowCutActive's
    // comment), same explicit-bypass contract as setBitDepth()'s top-of-range and Shields' own
    // lowCutActive floor.
    void setLowCutHz(float hz);

    // Simulated quantization depth (8-24 bits) applied to the wet stereo output, reproducing the
    // real unit's measured ~90dB dynamic-range ceiling (see the class comment's "16-bit
    // converters" note) - same formula and output-stage placement as ShieldsFDNEngine::
    // setBitDepth(). 24 is a GENUINE bypass (see bitDepthActive's comment), not just a
    // near-transparent setting - float32's own mantissa is 24 bits, so quantizing to 24 "levels
    // per bit" is not bit-identical to skipping the stage, and this class guarantees bit-identical
    // output at the top of the range the same way ShieldsFDNEngine::lowCutActive guarantees a
    // genuine no-op at its own floor.
    void setBitDepth(float bits);

    // In-place stereo process: L/R in, replaced with the wet signal out. Dry/wet mixing and
    // input/output gain happen in the processor, not here - same convention as Shields/Intruder.
    void processStereo(float* left, float* right, int numSamples);

private:
    // 8 lines. A 16-line variant was built and fully validated during the Phase D bass-excess
    // investigation (2026-09-02) and REJECTED on measurement. The reasoning that motivated it was
    // sound - with 8 delays spanning 30-76ms, low-frequency mode spacing is coarse enough that a
    // band's measured decay is dominated by whichever few modes ring longest rather than the
    // network average, and doubling the line count did measurably improve low-band accuracy
    // (+1.96dB -> +1.11dB) - but it made the tail too DENSE at short Time settings, where the real
    // captures are genuinely sparse: crest-factor error went from -0.38dB to -2.30dB (-7.1dB at
    // Time=0.5s alone) and envelope correlation dropped 0.938 -> 0.912, for only 0.11dB of
    // log-spectral distance in return. The actual dominant cause of the bass excess turned out to
    // be the missing input high-pass below, not modal density; once that was in, the extra lines
    // bought almost nothing and cost real density accuracy. Don't re-add lines without re-checking
    // crest factor at short Time specifically - the aggregate LSD will not show this.
    static constexpr int numLines = 8;

    // Mutually non-simple-ratio, non-whole-millisecond line lengths spanning 30-76ms - the same
    // convention Shields/Intruder use to avoid periodic reinforcement at 1kHz multiples.
    //
    // NOTE: these match ml-toolkit/effects/ambience/model.py's _BASE_DELAY_SAMPLES_AT_44K, but
    // that's now incidental rather than load-bearing: decayGain, dampingWeight and the input tilt
    // are all directly calibrated against the real captures (AuraDecayGainData.h,
    // AuraOnsetTiltData.h) rather than taken from the Python fit. AuraDecayGainData.h IS calibrated
    // against this exact topology though - re-run that calibration if these ever change.
    static constexpr std::array<float, numLines> baseLineLengthsMs {
        30.0907f, 35.3515f, 41.0658f, 47.5964f, 55.2608f, 60.8390f, 68.0045f, 75.8050f
    };

    static const std::array<std::array<float, numLines>, numLines> hadamard;

    // Same shelf pivot as model.py's _SHELF_PIVOT_HZ.
    static constexpr float shelfPivotHz = 1000.0f;

    // See setBandGains()'s comment.
    static constexpr float maxFeedbackGain = 0.985f;
    static constexpr float maxDampingWeight = 0.99f;

    // Independent low/high-band gain shelf, one-pole split - same structure as
    // common/dsp/TiltFilter.h, but with two independently-settable linear gains instead of one
    // symmetric tiltDb (TiltFilter always moves the two bands by equal-and-opposite dB amounts,
    // which doesn't fit here: model.py's fitted high_band_gain/low_band_gain are independent
    // values, not a symmetric tilt around a center). Not added as a new TiltFilter capability -
    // per AGENTS.md's LookAndFeel-extension convention applied the same way to common/dsp/, a
    // new shared capability gets added to shared code once a second plugin needs it, not
    // speculatively for the first one that does.
    struct BandShelf
    {
        wildjag::dsp::OnePoleFilter lowpass;
        float lowGain = 1.0f;
        float highGain = 1.0f;

        void reset() noexcept { lowpass.reset(); }
        void setPivotHz(float hz, double sampleRate) noexcept { lowpass.setCutoffHz(hz, sampleRate); }

        float processSample(float x) noexcept
        {
            const auto low = lowpass.processSample(x);
            const auto high = x - low;
            return low * lowGain + high * highGain;
        }
    };

    double sampleRateHz = 44100.0;
    bool prepared = false;

    std::array<wildjag::dsp::CircularDelayBuffer, numLines> lineBuffers;
    std::array<int, numLines> lineDelaySamples {};
    std::array<wildjag::dsp::OnePoleFilter, numLines> dampingFilter;
    std::array<BandShelf, numLines> feedbackShelf;

    // Player Low Cut - see setLowCutHz()'s comment. Applied first, ahead of pre-delay (order vs.
    // pre-delay is functionally irrelevant for a linear filter and a linear delay - they commute -
    // but "before the effect chain" reads most naturally as before pre-delay too). lowCutActive
    // mirrors ShieldsFDNEngine::lowCutActive: skip the filter entirely at 0Hz rather than rely on
    // the math happening to converge to identity there, guaranteeing a genuine bit-identical
    // bypass at the default.
    wildjag::dsp::OnePoleFilter lowCutL, lowCutR;
    bool lowCutActive = false;

    wildjag::dsp::CircularDelayBuffer preDelayBufferL, preDelayBufferR;
    int preDelaySamples = 0;
    int maxPreDelaySamples = 1;

    // Applied right after pre-delay, before injection into the tank - same placement/reasoning as
    // IntruderFDNEngine's own input TiltFilter: "colors EVERYTHING downstream, matching
    // findings.md's measurement that H's effect is present from the very first energy in the
    // signal, not just the tank's tail." Pivot at 2kHz, matching analyze_irs.py's spectral-tilt
    // measurement split, so the plugin's tilt lines up with what was actually measured.
    //
    // Real bug found via Phase D validation (2026-09-02): feedbackShelf alone produced ZERO
    // difference in onset tilt between High=0 and High=-8 (measured identical, 9.906dB, on real
    // renders) - it only touches the signal AFTER at least one feedback round trip, but the
    // dry input is injected into each line unshelved on its first pass (`shelved + injected`,
    // shelved comes from reading/processing the PREVIOUS state, injected is added raw). The very
    // first tank arrivals - which dominate what gets measured as "onset tilt" - are pure unshelved
    // injected signal, so a purely in-loop tilt mechanism structurally cannot affect onset content
    // no matter how strong its gains are. This input-stage TiltFilter is the fix.
    wildjag::dsp::TiltFilter inputTiltL, inputTiltR;
    static constexpr float inputTiltPivotHz = 2000.0f;

    // Fixed first-order high-pass on the input, modelling the real RMX16's own I/O rolloff -
    // NOT part of the reverb algorithm, which is exactly why it belongs here (static, outside the
    // feedback loop) rather than in the tank: on the real unit this is converter/analog-coupling
    // behaviour, and putting it in the loop would couple it to decay time the way the tilt bug
    // did. Measured directly on the captures (2026-09-02): relative to its own 300-800Hz level,
    // the reference sits about -3dB at 65-95Hz, -5 to -6dB at 45-65Hz and -6 to -9dB at 20-30Hz,
    // while this engine was flat to 20Hz - a 4-9dB sub-bass excess that no amount of feedback-gain
    // or damping calibration could remove, since it isn't a decay-rate problem at all. Implemented
    // as (x - lowpass(x)) using the shared OnePoleFilter, corner calibrated below.
    //
    // KNOWN LIMIT: this fixes sub-bass LEVEL, not sub-bass DECAY RATE. Being a static filter
    // outside the feedback loop it cannot change how long anything rings, and the 20-120Hz band
    // still decays slower than the rest of the spectrum (6.83s vs 6.12s for 120-500Hz at
    // Time=5.5s; reference is flat at 5.44 vs 5.30s). That residual is the modal-density limit
    // described in numLines' comment - at long decay times each mode is far narrower than the
    // mode spacing, so the low band's measured decay reflects its longest-lived individual modes
    // rather than the network average. More lines is the known fix and was measured to be a bad
    // trade here (see numLines). Left as a documented limitation rather than papered over.
    //
    // SECOND ATTEMPT, ALSO REJECTED (2026-09-02): an asymmetric 16-line topology (original 8 plus
    // 8 new, longer [91-189ms] lines carrying their own extra damping, meant to add low-frequency
    // modal density without the short-Time density regression the earlier uniform-doubling
    // attempt hit). Calibration found a harder problem than density: the new lines' own transit
    // time put a hard FLOOR on achievable decay time, independent of feedback gain - even at
    // near-zero gain, measured RT60 couldn't go below ~0.6s, well above the 0.1s setting's own
    // ~0.5s target, because a single pass through a 189ms line alone takes that long. Reverted
    // (Source/Tools/CalibrateProbe.cpp, a reusable raw-coefficient calibration probe, was kept -
    // it isn't specific to this attempt). A real fix would need the extra lines' participation
    // gated by Time rather than just darkened - a bigger change than a recalibration, not
    // attempted. Don't re-try longer lines without a plan for that gating.
    wildjag::dsp::OnePoleFilter inputHighPassL, inputHighPassR;
    // 70Hz: best least-squares match to the measured reference shape across the 20-140Hz bands
    // (swept 40/55/70/85/100Hz against it). The reference's own curve is slightly non-monotonic
    // down there (-5.1dB at 30-45Hz vs -6.1dB at 45-65Hz) which no first-order high-pass can
    // track - that wobble is modal noise in a region where the captures have few modes and little
    // energy, not a shape worth chasing with a higher-order filter.
    static constexpr float defaultInputHighPassHz = 70.0f;

    // Output-stage quantizer (see setBitDepth()'s comment). Deliberately NOT in the feedback loop:
    // quantizing inside a recirculating path risks limit cycles (a tail settling into a nonzero
    // ±1-LSB pattern that never reaches true silence instead of decaying away) rather than just
    // adding grain. The output-only placement still reproduces the audible character, since the
    // tail decays *through* this stage on its way out and picks up the same LSB structure - see
    // AuraFDNEngineTests.cpp's "tail still decays to silence" test, which guards this specifically
    // in case in-loop quantization is ever reconsidered. No dither: an undithered decaying tail is
    // itself the era-correct grain this control exists to add, and dither wasn't standard practice
    // on hardware of this vintage.
    bool bitDepthActive = false;      // false <=> setBitDepth(24) or never called: genuine bypass
    float bitDepthLevels = 8388608.0f; // 2^(24-1), matches setBitDepth(24.0f)'s levels if ever used

    void updateLineLengths();
};
