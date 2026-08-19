#pragma once

#include <array>
#include <vector>

// The diffuse reverb core behind Bloom's "slow buildup" character: an 8-line feedback delay
// network (FDN) with Hadamard feedback mixing, per-line one-pole damping in the feedback path,
// and a short series-allpass diffuser on the input ahead of the network. Deliberately not a bank
// of parallel comb filters into series allpasses (classic Schroeder/Moorer) - the Hadamard-mixed
// FDN is easier to keep stable while tuning Diffusion/Feedback/Size against the reference Midiverb
// captures, and produces the same territory of sound.
//
// The buildup itself is not an envelope: it emerges from a bank of short feedback combs the input
// passes through before the main tank (see BurstCombLine below) - the windowed RMS of several
// mutually-prime decaying repeat-trains summed together genuinely rises for a while as more
// repeats overlap, before falling once decay outpaces new overlap. That's a real per-sample
// recursive filter responding to the actual input, not a precomputed gain curve. (An earlier
// version relied solely on the main tank's own echo DENSITY increasing over time, on the theory
// that a passive energy-preserving cross-mix can only ever lose RMS energy from the moment of
// injection - true, but comparison against real Midiverb captures showed the hardware's own RMS
// envelope genuinely swells too, not just its density, hence the burst stage.) No delay-line
// modulation is applied anywhere in this class (no chorus/pitch drift) - this matches the original
// hardware's static, unmodulated, slightly grainy diffusion.
//
// Two independent instances are NOT needed for stereo: a single 8-line network is shared between
// channels (even-indexed lines seeded from L, odd-indexed from R; L output reads the even lines,
// R output reads the odd lines), which is both cheaper than two full networks and produces a more
// correlated, natural stereo image than fully independent L/R diffusers would.
class BloomFDNEngine
{
public:
    BloomFDNEngine() = default;

    void prepare(double sampleRate);
    void reset();

    // 0.3-0.7, centered near 0.5 per the Bloom spec - drives the input allpass diffuser
    // coefficients. This is deliberately NOT an FDN feedback-matrix parameter; a fixed Hadamard
    // matrix already handles the network's internal mixing.
    void setDiffusion(float diffusion);

    // 0-1, mapped internally to a feedback gain safely below unity so the (energy-preserving,
    // orthogonal) Hadamard-mixed network stays convergent - this is the plugin's decay/tail-length
    // control.
    void setFeedback(float feedback01);

    // Multiplier on the base (mutually-prime-ms) delay line lengths. This is Bloom's de facto
    // "attack time" control per the spec: longer lines mean more round trips (samples) before the
    // network reaches full echo density, so the bloom is slower without any separate envelope.
    void setSize(float sizeMultiplier);

    // 0-1, one-pole lowpass coefficient applied to each line's feedback-path signal - controls how
    // quickly high frequencies decay relative to lows.
    void setDamping(float damping01);

    // One-pole lowpass cutoff (Hz) applied to the wet stereo output - the ~15kHz bandwidth-limit
    // lo-fi character from the spec, kept as a tunable parameter rather than a hardcoded constant.
    void setBandwidthHz(float hz);

    // Simulated quantization depth (4-16 bits) applied to the wet stereo output, reintroducing the
    // grain of the original 12-16 bit hardware. 16 is effectively transparent at float32 precision.
    void setBitDepth(float bits);

    // In-place stereo process: L/R in, replaced with the wet signal out. Dry/wet mixing happens in
    // the processor, not here, so this class stays testable as a pure "wet generator."
    void processStereo(float* left, float* right, int numSamples);

private:
    static constexpr int numLines = 8;

    // Mutually prime line lengths (ms) - no shared factors means no periodic reinforcement between
    // lines, which is what avoids the metallic/ringing comb-filter character the spec calls out.
    static constexpr std::array<float, numLines> baseLineLengthsMs {
        19.0f, 23.0f, 29.0f, 31.0f, 37.0f, 41.0f, 43.0f, 47.0f
    };

    // Upper bound on setSize()'s multiplier - buffers are sized for this at prepare() so changing
    // Size at runtime never needs a reallocation (just a change to the modulo length in use).
    static constexpr float maxSizeMultiplier = 4.0f;

    // Fixed short delays (ms) for the 3-stage input diffuser, applied independently to L and R.
    // Deliberately unrelated to the FDN's own line lengths and to each other (no shared factors)
    // so the pre-diffuser doesn't itself introduce periodicity.
    static constexpr std::array<float, 3> allpassDelaysMs { 4.7f, 3.1f, 2.3f };

    // Simple Schroeder allpass: y[n] = -g*x[n] + x[n-D] + g*y[n-D]. Fixed delay length once
    // prepared; only the coefficient g (Diffusion) changes at runtime.
    struct AllpassStage
    {
        std::vector<float> buffer;
        int writePos = 0;
        int delaySamples = 1;
        float coefficient = 0.5f;

        void prepare(int maxDelaySamples);
        void reset();
        float processSample(float x);
    };

    // The actual mechanism behind the audible swell (see the class comment's "On buildup" note):
    // a bank of short, mutually-prime feedback combs the (allpass-smoothed) input passes through
    // before ever reaching the main 8-line tank. Each comb turns the input into its own train of
    // exponentially-decaying repeats; summed across several mutually-prime delays (so no single
    // repeat rate dominates - that would just be a metallic comb filter), the windowed RMS of that
    // sum genuinely rises for a while as more of each line's repeats overlap, before falling once
    // decay outpaces new overlap. This is a real, tunable per-sample recursive filter driven by the
    // actual input, not a precomputed time-keyed gain curve. Its own decay time (independent of the
    // main tank's much longer Feedback-controlled tail) sets how long the buildup itself takes,
    // scaled by Size exactly as the spec calls for.
    struct BurstCombLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        int delaySamples = 1;
        float feedbackGain = 0.0f;

        void prepare(int maxDelaySamples);
        void reset();
        float processSample(float x);
    };

    // 8x8 Hadamard matrix (+-1 entries, Sylvester construction), normalised by 1/sqrt(8) at use
    // time - an orthogonal (energy-preserving) mix, which keeps overall network stability governed
    // purely by the scalar feedback gain and the damping coefficient rather than by the matrix.
    static const std::array<std::array<float, numLines>, numLines> hadamard;

    // Standard RBJ Audio EQ Cookbook biquad (direct form 1). Used for the fixed output low-shelf
    // below - see that member's comment for why it exists.
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

        void setLowShelf(float freqHz, float gainDb, double sampleRateHz);
        void setHighShelf(float freqHz, float gainDb, double sampleRateHz);
        void setPeak(float freqHz, float gainDb, float q, double sampleRateHz);
        void reset();
        float processSample(float x);
    };

    double sampleRateHz = 44100.0;

    std::array<std::vector<float>, numLines> lineBuffers;
    std::array<int, numLines> writePos {};
    std::array<int, numLines> delaySamples {};
    std::array<float, numLines> dampingState {};

    std::array<AllpassStage, 3> allpassL, allpassR;

    static constexpr int numBurstLines = 6;

    // Mutually prime (no shared factors, so no single repeat rate dominates into a metallic ring),
    // spread across roughly the same order of magnitude as baseAttackMs below - short lines alone
    // saturate (all their repeats overlap) almost immediately, which pulls the RMS peak far earlier
    // than the attack duration actually calls for; spreading lengths out this way is what makes the
    // peak-timing controllable via baseAttackMs at all. Scaled by sizeMultiplier like the main tank.
    static constexpr std::array<float, numBurstLines> baseBurstLengthsMs {
        13.0f, 37.0f, 61.0f, 89.0f, 113.0f, 149.0f
    };

    // How long the burst (attack/buildup) takes at sizeMultiplier == 1, in ms - calibrated against
    // the real Midiverb reference IRs' observed rise-to-peak time. Scaled by Size exactly like the
    // main tank's lines, so Size remains the single "how slow is the bloom" control.
    static constexpr float baseAttackMs = 650.0f;

    // Fraction of a burst line's initial amplitude it should have decayed to by baseAttackMs *
    // sizeMultiplier - i.e. how "used up" the attack is considered by the time the main tail takes
    // over. Lower = a more clearly bounded attack window; higher = a longer-lingering burst tail.
    static constexpr float burstFloor = 0.1f;

    std::array<BurstCombLine, numBurstLines> burstL, burstR;

    void updateBurstLines();

    float sizeMultiplier = 1.0f;
    float feedbackGain = 0.85f;
    float dampingCoefficient = 0.35f;
    float diffusionCoefficient = 0.5f;

    // Cascaded one-pole stages for the output bandwidth limiter, per channel. A single one-pole is
    // only 6dB/octave - comparison against the reference IRs (tools/compare_irs.py's frequency-
    // resolved spectral-difference plot) showed the real hardware's high end falls off much more
    // sharply than that lets a single stage reproduce, so this cascades numBandwidthStages of them.
    // Each stage uses the SAME coefficient (they're identical), but that coefficient is computed
    // for a HIGHER per-stage cutoff than the requested Bandwidth Hz - cascading N identical
    // one-poles pulls the cascade's overall -3dB point below any individual stage's own -3dB point,
    // so setBandwidthHz() compensates for that shift (see its implementation).
    static constexpr int numBandwidthStages = 4;
    std::array<float, numBandwidthStages> bandwidthStateL {}, bandwidthStateR {};
    float bandwidthCoefficient = 0.0f; // recomputed by setBandwidthHz() - same value for every stage

    // Fixed (not user-exposed) output shelving pair, always active - corrects a broadband tonal
    // gap found by comparing against the reference IRs. The raw tank+burst output measured ~8dB
    // LIGHT in BOTH the 20-500Hz and 500-4000Hz bands relative to the real hardware, but ~4-5dB
    // EXCESS above 4kHz - i.e. not really a "missing bass" problem specifically, more a broad tilt
    // where everything below ~2-3kHz is relatively too quiet and everything above is relatively too
    // loud. A high-shelf CUT does most of that correction in one stage (pulling the excess top end
    // down brings the rest up in relative terms); the low-shelf boost on top of it targets the
    // extra sub-100Hz-specific dip the spectral-difference plot showed beyond that broader tilt.
    // Neither is exposed as its own parameter since both compensate for an inherent character gap
    // between this topology and the real unit, not something a player would want to sweep - same
    // rationale as the fixed Hadamard matrix or the allpass diffuser delays.
    static constexpr float lowShelfFreqHz = 350.0f;
    static constexpr float lowShelfGainDb = 7.0f;
    static constexpr float highShelfFreqHz = 7000.0f;
    static constexpr float highShelfGainDb = -5.0f;

    // No mid-band correction needed: once the spectral comparison was fixed to compare 1/3-octave-
    // smoothed energy (see tools/compare_irs.py's smooth_to_fractional_octave()) instead of raw FFT
    // bins, the apparent ~10dB mid-band gap mostly turned out to be comb-filtering/resonance
    // misalignment noise between this topology's and the real hardware's differently-spaced modes,
    // not a genuine colour difference - the smoothed comparison put it at ~3dB, close enough to
    // leave alone. Kept as a stage (at 0dB, i.e. inactive) rather than deleted, in case future
    // reference IRs reveal a real mid-band gap worth addressing this way.
    static constexpr float midPeakFreqHz = 1200.0f;
    static constexpr float midPeakGainDb = 0.0f;
    static constexpr float midPeakQ = 0.7f;

    Biquad lowShelfL, lowShelfR;
    Biquad highShelfL, highShelfR;
    Biquad midPeakL, midPeakR;

    float bitDepthLevels = 32768.0f; // recomputed by setBitDepth() - matches setBitDepth(16.0f)

    void updateLineLengths();
};
