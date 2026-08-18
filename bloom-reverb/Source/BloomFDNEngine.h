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
// The buildup itself is not an envelope: it emerges from echo density increasing as the impulse
// takes more round trips through the 8-way Hadamard mix before the network's output reaches full
// density. No delay-line modulation is applied anywhere in this class (no chorus/pitch drift) -
// this matches the original hardware's static, unmodulated, slightly grainy diffusion.
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

    // 8x8 Hadamard matrix (+-1 entries, Sylvester construction), normalised by 1/sqrt(8) at use
    // time - an orthogonal (energy-preserving) mix, which keeps overall network stability governed
    // purely by the scalar feedback gain and the damping coefficient rather than by the matrix.
    static const std::array<std::array<float, numLines>, numLines> hadamard;

    double sampleRateHz = 44100.0;

    std::array<std::vector<float>, numLines> lineBuffers;
    std::array<int, numLines> writePos {};
    std::array<int, numLines> delaySamples {};
    std::array<float, numLines> dampingState {};

    std::array<AllpassStage, 3> allpassL, allpassR;

    float sizeMultiplier = 1.0f;
    float feedbackGain = 0.85f;
    float dampingCoefficient = 0.35f;
    float diffusionCoefficient = 0.5f;

    // One-pole state for the output bandwidth limiter, per channel.
    float bandwidthStateL = 0.0f, bandwidthStateR = 0.0f;
    float bandwidthCoefficient = 0.0f; // recomputed by setBandwidthHz()

    float bitDepthLevels = 32768.0f; // recomputed by setBitDepth() - matches setBitDepth(16.0f)

    void updateLineLengths();
};
