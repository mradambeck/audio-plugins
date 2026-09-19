#include "InhaltIRSynth.h"

#include <cmath>

namespace inhalt
{

// Two disjoint, mutually non-simple-ratio delay sets - directly from
// ml-toolkit/effects/nonlin/model.py's LEFT_DELAY_SAMPLES_AT_44K / RIGHT_DELAY_SAMPLES_AT_44K
// (481,513,561,657,695,805,813,829 and 721,763,799,969,1079,1089,1133,1561 samples @44.1kHz),
// converted to milliseconds here so they scale to any session rate the same way every other
// engine in this catalog scales its own ms delay table (e.g. AuraFDNEngine::baseLineLengthsMs).
//
// DELIBERATELY ASYMMETRIC ranges (left mean ~15.2ms, right ~23.0ms), not two similar-range sets
// - a real, measured, and chosen trade-off, not an oversight. Empirically swept (see
// ml-toolkit's own diagnostic scripts from that session): two independent 8-line tanks with
// SIMILAR delay ranges (the original ~10-25ms/~11-26ms pair, and 30 other random pairs in that
// same range) floor out around IACC~0.045-0.05 no matter which specific values are chosen -
// more lines in the same range measured WORSE (16 lines: 0.051; 64 lines: 0.063), and removing
// the shared gate envelope entirely barely moved it (0.044 vs 0.039) - so neither line count nor
// the gate was the cause. Only genuinely NON-OVERLAPPING delay RANGES reduced it further. Real
// hardware measures 0.006-0.04.
//
// REVISED TWICE since that original sweep (this is the third delay-line set this engine has
// shipped, not the second - both prior revisions were measured, not just proposed, and the first
// revision was itself caught and discarded after a real regression, not silently superseded):
//
//   1st revision (discarded): after a real, ear-caught tonal complaint ("convolution feels
//   beefier, more going on around ~400Hz" than this engine's own render), direct measurement
//   found a genuine tank-modal notch around 128-323Hz (worst at 161Hz, -3 to -9dB relative to
//   neighboring bands, present even with the tilt and direct tap disabled - a property of the
//   ORIGINAL delay-line set's own modal structure). A randomized search over notch depth + IACC
//   closed the notch and improved IACC - but ONLY tested against a bare tank, without the input
//   diffuser feeding it or the tilt. Built, re-fit, and validated for real: the notch was
//   genuinely closed, but IACC at Time=9.8/High=-9 got WORSE (0.0712 vs the original set's own
//   ~0.0352) - a real regression Adam separately caught by ear ("the stereo spread feels pretty
//   different, even when adjusting the width"). Traced to an interaction between the
//   diffuser-fed-into-the-tank stage and that candidate's own delays under the tilt's strong
//   low-frequency boost at negative High - reproduced in a Python re-test that added the
//   diffuser stage (not present in the original search), confirming the search methodology
//   itself was the gap, not bad luck.
//
//   2nd revision (this set): re-ran the same search, this time scoring the FULL chain (diffuser
//   feeding the tank + the real LTAS-calibrated tilt applied, not just the bare tank) at
//   Time=9.8/High=-9 specifically (the worst real case) alongside the neutral case. Verified
//   robust across 5 real Time/High settings, not just the one it was searched against: notch
//   +0.19 to +0.31dB (essentially closed) and IACC 0.026-0.032 at every one, including High=-9 -
//   no negative-High regression, and better than the ORIGINAL set's own IACC everywhere checked.
const std::array<float, InhaltIRSynth::numLines> InhaltIRSynth::leftDelaysMs { {
    10.907029f, 11.632653f, 12.721088f, 14.897959f, 15.759637f, 18.253968f, 18.435374f, 18.798186f,
} };
const std::array<float, InhaltIRSynth::numLines> InhaltIRSynth::rightDelaysMs { {
    16.349206f, 17.301587f, 18.117914f, 21.972789f, 24.467120f, 24.693878f, 25.691610f, 35.396825f,
} };

// Input diffuser delays - directly from ml-toolkit/effects/nonlin/model.py's
// DIFFUSER_DELAY_SAMPLES_AT_44K (53,79,115 samples @44.1kHz), converted to ms here for the same
// any-session-rate scaling reason leftDelaysMs/rightDelaysMs are in ms. Empirically swept (see
// that module's own comment) against the real captures' onset NED trajectory in a throwaway
// Python prototype before being adopted as this chain's fixed topology - 3 stages at these delays
// tracked the real captures' onset density shape far better than 1, 2, or 4 stages tried at the
// same task.
const std::array<float, InhaltIRSynth::numDiffuserStages> InhaltIRSynth::leftDiffuserDelaysMs { {
    1.201814f, 1.791383f, 2.607710f,
} };

// A second, DIFFERENT diffuser delay set for the right channel (61, 97, 149 samples @44.1kHz) -
// added alongside Params::directGain (see that field's own comment): each channel's diffuser
// output is now also tapped directly for an early-arrival component, so a shared left/right
// diffuser would have correlated that tap between channels, undoing the asymmetric-tank
// decorrelation work already done. Same non-simple-ratio-delay convention as every other delay
// set in this engine, chosen close in magnitude to the left set (so both channels' early taps
// arrive in a comparable window) but genuinely different values, not a scaled copy.
const std::array<float, InhaltIRSynth::numDiffuserStages> InhaltIRSynth::rightDiffuserDelaysMs { {
    1.383220f, 2.199546f, 3.378685f,
} };

// Same fixed 8x8 Hadamard matrix as AuraFDNEngine.h/ShieldsFDNEngine.h - Sylvester construction,
// normalized by 1/sqrt(8) at the point of use (see processSample()).
const std::array<std::array<float, InhaltIRSynth::numLines>, InhaltIRSynth::numLines> InhaltIRSynth::hadamard { {
    { 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, -1, 1, -1, 1, -1, 1, -1 },
    { 1, 1, -1, -1, 1, 1, -1, -1 },
    { 1, -1, -1, 1, 1, -1, -1, 1 },
    { 1, 1, 1, 1, -1, -1, -1, -1 },
    { 1, -1, 1, -1, -1, 1, -1, 1 },
    { 1, 1, -1, -1, -1, -1, 1, 1 },
    { 1, -1, -1, 1, -1, 1, 1, -1 },
} };

void InhaltIRSynth::Tank::prepare(const std::array<float, numLines>& delayMs, double sampleRate)
{
    for (int i = 0; i < numLines; ++i)
    {
        const auto maxDelaySamples = (int) std::ceil(delayMs[(size_t) i] * 0.001 * sampleRate) + 4;
        lineBuffers[(size_t) i].setSize(maxDelaySamples);
        lineDelaySamples[(size_t) i] = (int) std::round(delayMs[(size_t) i] * 0.001 * sampleRate);
        dampingFilter[(size_t) i].reset();
    }
}

void InhaltIRSynth::Tank::reset() noexcept
{
    for (int i = 0; i < numLines; ++i)
    {
        lineBuffers[(size_t) i].reset();
        dampingFilter[(size_t) i].reset();
    }
}

float InhaltIRSynth::Tank::processSample(float input, float feedbackGain, float dampingWeight) noexcept
{
    std::array<float, numLines> lineOut {};
    for (int i = 0; i < numLines; ++i)
        lineOut[(size_t) i] = lineBuffers[(size_t) i].read(lineDelaySamples[(size_t) i] - 1);

    std::array<float, numLines> damped {};
    for (int i = 0; i < numLines; ++i)
    {
        dampingFilter[(size_t) i].setWeight(dampingWeight);
        damped[(size_t) i] = dampingFilter[(size_t) i].processSample(lineOut[(size_t) i]);
    }

    std::array<float, numLines> mixed {};
    constexpr float hadamardNorm = 0.353553390593f; // 1/sqrt(8)
    for (int i = 0; i < numLines; ++i)
    {
        float sum = 0.0f;
        for (int j = 0; j < numLines; ++j)
            sum += hadamard[(size_t) i][(size_t) j] * damped[(size_t) j];
        mixed[(size_t) i] = sum * hadamardNorm * feedbackGain;
    }

    for (int i = 0; i < numLines; ++i)
    {
        const auto lineIn = mixed[(size_t) i] + input;
        // NaN/Inf guard at the recirculation point, same convention as every other FDN engine in
        // this catalog (ShieldsFDNEngine/IntruderFDNEngine/AuraFDNEngine) - a poisoned sample must
        // not be allowed to circulate forever.
        lineBuffers[(size_t) i].write(std::isfinite(lineIn) ? lineIn : 0.0f);
    }

    // Taps the RAW delay-line output (read at the top of this call, before this sample's own
    // damping/mixing/feedback are computed for the NEXT round trip) - same convention
    // AuraFDNEngine.cpp uses for its own tankL/tankR sum, not the damped+mixed value. NOTE: this
    // means the audible tap isn't bit-identical to model.py's _render_tank(), which sums the
    // DAMPED+MIXED spectrum instead (Hmix @ DampY).sum(...) - the same gap exists between
    // AmbienceFDN.forward() and AuraFDNEngine.cpp's hand-port and was absorbed by re-calibrating
    // decayGain/dampingWeight against the C++ engine itself rather than transplanting the Python
    // fit's raw numbers (see AuraDecayGainData.h's own history). Treat this engine's output the
    // same way: verify and calibrate against ITS OWN behaviour (see InhaltCalibrateProbe), don't
    // assume the Python fit's numbers transfer unchanged.
    float sum = 0.0f;
    for (int i = 0; i < numLines; ++i)
        sum += lineOut[(size_t) i];
    constexpr float tankNorm = 0.353553390593f; // 1/sqrt(8) - 8 lines summed per channel
    return sum * tankNorm;
}

void InhaltIRSynth::Diffuser::prepare(const std::array<float, numDiffuserStages>& delayMs, double sampleRate)
{
    for (int i = 0; i < numDiffuserStages; ++i)
    {
        const auto maxDelaySamples = (int) std::ceil(delayMs[(size_t) i] * 0.001 * sampleRate) + 4;
        stageBuffers[(size_t) i].setSize(maxDelaySamples);
        stageDelaySamples[(size_t) i] = std::max(1, (int) std::round(delayMs[(size_t) i] * 0.001 * sampleRate));
        stageBuffers[(size_t) i].reset();
    }
}

void InhaltIRSynth::Diffuser::reset() noexcept
{
    for (auto& buffer : stageBuffers)
        buffer.reset();
}

float InhaltIRSynth::Diffuser::processSample(float input, float gain) noexcept
{
    // Standard Schroeder allpass, chained in series: y = -g*(x + g*bufOut) + bufOut,
    // buffer <- x + g*bufOut. Unity gain at every frequency (a diffuser, not a tone-shaping
    // filter) - matches core.dsp_primitives.allpass_chain_transfer_function's per-stage transfer
    // function A(z) = (-g + z^-D)/(1 - g*z^-D) exactly.
    float x = input;
    for (int i = 0; i < numDiffuserStages; ++i)
    {
        const auto bufOut = stageBuffers[(size_t) i].read(stageDelaySamples[(size_t) i] - 1);
        const auto v = x + gain * bufOut;
        const auto y = -gain * v + bufOut;
        stageBuffers[(size_t) i].write(std::isfinite(v) ? v : 0.0f);
        x = y;
    }
    return x;
}

namespace
{
    // Numerically-stable softplus: log(1+exp(x)), computed to avoid overflow for large x (same
    // shape PyTorch's own F.softplus uses) - matches model.py's F_torch.softplus() exactly, so
    // the fitted t_knee_ms/tau_k_ms/fall_rate_db_per_s values render identically in both places.
    float softplus(float x) noexcept
    {
        if (x > 20.0f)
            return x;
        return std::log1p(std::exp(x));
    }

    // Takes plateauDroopDbPerSec/fallRateDbPerSec explicitly rather than reading them off `p`
    // directly - called once per decay band (low/mid/high, see InhaltIRSynth.h's own comment on
    // why the decay rate is the one thing that genuinely varies by band), sharing every other
    // field (buildUpMs/kneeTimeMs/kneeSoftnessMs/earlyExcessDb) from `p` unchanged across all
    // three calls.
    float gateEnvelopeDb(float tSeconds, const InhaltIRSynth::Params& p,
                         float plateauDroopDbPerSec, float fallRateDbPerSec) noexcept
    {
        const auto tauA = std::max(p.buildUpMs, 0.001f) * 0.001f;
        const auto tauK = std::max(p.kneeSoftnessMs, 0.001f) * 0.001f;
        const auto tKnee = p.kneeTimeMs * 0.001f;
        const auto tauEarly = std::max(p.earlyExcessTauMs, 0.001f) * 0.001f;

        const auto attackLin = std::max(1.0f - std::exp(-tSeconds / tauA), 1e-6f);
        const auto attackDb = 20.0f * std::log10(attackLin);
        // plateauDroopDbPerSec*t is deliberately UNBOUNDED past the knee for the (always-negative
        // in practice) broadband/single-rate case - the fall_rate_db_per_s calibration is built on
        // exactly that (see build_measured_gate_curves.py's own _build_fall_rate_curves docstring:
        // what's written subtracts this Time's own target plateau droop specifically because this
        // term keeps contributing after the knee). A per-band POSITIVE plateauDroopDbPerSec (real,
        // needed when this engine's own tank over-damps a band far more than real hardware does -
        // see InhaltIRSynth.h's own comment on plateauDroopHighDbPerSec) breaks that assumption in
        // a way the broadband case never could: an ever-GROWING positive term swamps
        // fallRateDbPerSec's own decay for the entire remaining render, not just the plateau.
        // Clamped to stop growing past the knee ONLY when positive, leaving the established
        // negative-droop relationship (and its own calibration) completely unchanged.
        const auto plateauDb = plateauDroopDbPerSec >= 0.0f
            ? plateauDroopDbPerSec * std::min(tSeconds, tKnee)
            : plateauDroopDbPerSec * tSeconds;
        const auto kneeDb = fallRateDbPerSec * tauK * softplus((tSeconds - tKnee) / tauK);
        // Zero for t < tKnee (the max(...,0) guard keeps the exponent from blowing up there),
        // saturating smoothly to earlyExcessDb for t well past the knee - see this field's own
        // comment in InhaltIRSynth.h.
        const auto earlyExcessDb = p.earlyExcessDb
            * (1.0f - std::exp(-std::max(tSeconds - tKnee, 0.0f) / tauEarly));

        return attackDb + plateauDb + kneeDb + earlyExcessDb;
    }
} // namespace

void InhaltIRSynth::render(const Params& params, double sampleRate, int numSamples,
                            std::vector<float>& left, std::vector<float>& right)
{
    left.assign((size_t) std::max(numSamples, 0), 0.0f);
    right.assign((size_t) std::max(numSamples, 0), 0.0f);
    if (numSamples <= 0)
        return;

    Tank leftTank, rightTank;
    leftTank.prepare(leftDelaysMs, sampleRate);
    rightTank.prepare(rightDelaysMs, sampleRate);

    Diffuser leftDiffuser, rightDiffuser;
    leftDiffuser.prepare(leftDiffuserDelaysMs, sampleRate);
    rightDiffuser.prepare(rightDiffuserDelaysMs, sampleRate);
    const auto diffuserGain = std::min(std::max(params.diffuserGain, 0.0f), 0.9f);
    const auto directGain = std::max(params.directGain, 0.0f);

    const auto feedbackGain = std::min(std::max(params.feedbackGain, 0.0f), maxFeedbackGain);
    const auto dampingWeight = std::min(std::max(params.dampingWeight, 1e-6f), maxDampingWeight);

    wildjag::dsp::BandShelf tiltL, tiltR;
    tiltL.lowGain = tiltR.lowGain = params.tiltLowGain;
    tiltL.highGain = tiltR.highGain = params.tiltHighGain;
    tiltL.setPivotHz(params.tiltPivotHz, sampleRate);
    tiltR.setPivotHz(params.tiltPivotHz, sampleRate);

    // Per-band gate split - cascaded one-pole complementary splits per channel (same
    // low+high=input-exactly technique as wildjag::dsp::BandShelf above, just STEEPER and applied
    // three times: once to carve subLow+low off the bottom, once to carve mid off what's left,
    // and the remainder is high). The complementary reconstruction (high = input - low)
    // stays EXACT regardless of how the "low" estimate is computed, so cascading N one-pole stages
    // for a steeper (~6*N dB/octave) roll-off is safe on its own - a first attempt at this used
    // splitPoleStages one-pole stages at the crossover frequency ITSELF as each stage's own
    // cutoff, and measurably WORSENED per-band accuracy: cascading N identical one-pole stages
    // shifts the CASCADE's own effective -3dB point to fc*sqrt(2^(1/N)-1), well below the nominal
    // fc each stage was individually set to (verified: at splitPoleStages=4 the bands shifted far
    // enough to misalign with the analysis bands this engine calibrates against entirely). Fixed
    // by compensating each stage's own cutoff so the CASCADE's own -3dB point lands exactly on
    // lowMidCrossoverHz/midHighCrossoverHz - splitPoleCompensation is 1/sqrt(2^(1/N)-1) for that N.
    // N=2 (not 4) specifically to keep the compensated high crossover safely under Nyquist at
    // ordinary session rates (4 stages' own compensation factor would push midHighCrossoverHz's
    // compensated cutoff above 22kHz Nyquist at 44.1kHz - not just steeper, actively invalid).
    static constexpr int splitPoleStages = 2;
    const auto splitPoleCompensation = 1.0f / std::sqrt(std::pow(2.0f, 1.0f / (float) splitPoleStages) - 1.0f);
    std::array<wildjag::dsp::OnePoleFilter, splitPoleStages> splitSubLowL, splitSubLowR, splitLowL, splitLowR, splitHighL, splitHighR;
    for (auto& f : splitSubLowL) f.setCutoffHz(InhaltIRSynth::subLowLowCrossoverHz * splitPoleCompensation, sampleRate);
    for (auto& f : splitSubLowR) f.setCutoffHz(InhaltIRSynth::subLowLowCrossoverHz * splitPoleCompensation, sampleRate);
    for (auto& f : splitLowL) f.setCutoffHz(InhaltIRSynth::lowMidCrossoverHz * splitPoleCompensation, sampleRate);
    for (auto& f : splitLowR) f.setCutoffHz(InhaltIRSynth::lowMidCrossoverHz * splitPoleCompensation, sampleRate);
    for (auto& f : splitHighL) f.setCutoffHz(InhaltIRSynth::midHighCrossoverHz * splitPoleCompensation, sampleRate);
    for (auto& f : splitHighR) f.setCutoffHz(InhaltIRSynth::midHighCrossoverHz * splitPoleCompensation, sampleRate);

    auto cascadedLowpass = [](std::array<wildjag::dsp::OnePoleFilter, splitPoleStages>& stages, float x) noexcept
    {
        auto y = x;
        for (auto& stage : stages)
            y = stage.processSample(y);
        return y;
    };

    for (int n = 0; n < numSamples; ++n)
    {
        const auto impulse = (n == 0) ? 1.0f : 0.0f;
        const auto diffusedImpulseL = leftDiffuser.processSample(impulse, diffuserGain);
        const auto diffusedImpulseR = rightDiffuser.processSample(impulse, diffuserGain);

        const auto tankL = leftTank.processSample(diffusedImpulseL, feedbackGain, dampingWeight);
        const auto tankR = rightTank.processSample(diffusedImpulseR, feedbackGain, dampingWeight);

        // Direct/early-arrival tap - see Params::directGain's own comment: the tank alone is
        // exactly zero until its shortest delay line's first round trip (~10ms), a true silence
        // gap the real hardware doesn't have. Each channel's OWN diffuser output (already spread
        // over the first few ms, decorrelated from the other channel by construction) stands in
        // for that immediate response.
        const auto combinedL = tankL + diffusedImpulseL * directGain;
        const auto combinedR = tankR + diffusedImpulseR * directGain;

        // Input tilt - see the Params struct's own comment on why this is applied here, to the
        // closed-loop tank output, rather than recirculated inside the loop.
        const auto tiltedL = tiltL.processSample(combinedL);
        const auto tiltedR = tiltR.processSample(combinedR);

        const auto lowAllL = cascadedLowpass(splitLowL, tiltedL);
        const auto restL = tiltedL - lowAllL;
        const auto subLowL = cascadedLowpass(splitSubLowL, lowAllL);
        const auto lowL = lowAllL - subLowL;
        const auto midL = cascadedLowpass(splitHighL, restL);
        const auto highL = restL - midL;

        const auto lowAllR = cascadedLowpass(splitLowR, tiltedR);
        const auto restR = tiltedR - lowAllR;
        const auto subLowR = cascadedLowpass(splitSubLowR, lowAllR);
        const auto lowR = lowAllR - subLowR;
        const auto midR = cascadedLowpass(splitHighR, restR);
        const auto highR = restR - midR;

        const auto tSeconds = (float) n / (float) sampleRate;
        const auto gateSubLowLin = std::pow(10.0f, gateEnvelopeDb(
            tSeconds, params, params.plateauDroopSubLowDbPerSec, params.fallRateSubLowDbPerSec) / 20.0f);
        const auto gateLowLin = std::pow(10.0f, gateEnvelopeDb(
            tSeconds, params, params.plateauDroopLowDbPerSec, params.fallRateLowDbPerSec) / 20.0f);
        const auto gateMidLin = std::pow(10.0f, gateEnvelopeDb(
            tSeconds, params, params.plateauDroopMidDbPerSec, params.fallRateMidDbPerSec) / 20.0f);
        const auto gateHighLin = std::pow(10.0f, gateEnvelopeDb(
            tSeconds, params, params.plateauDroopHighDbPerSec, params.fallRateHighDbPerSec) / 20.0f);

        left[(size_t) n] = subLowL * gateSubLowLin + lowL * gateLowLin + midL * gateMidLin + highL * gateHighLin;
        right[(size_t) n] = subLowR * gateSubLowLin + lowR * gateLowLin + midR * gateMidLin + highR * gateHighLin;
    }
}

} // namespace inhalt
