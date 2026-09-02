# Phase D validation report

Plugin renders (via AuraRenderIR) vs. all 65 reference captures in `ml-toolkit/effects/ambience/captures/`, mapping each reference's Time and High directly (Aura's Time IS the hardware label - see module docstring). Low is passed through the filename but has no DSP effect (unwired - see AuraParameterMap.h), so results are split into Low=0 (fully comparable) and Low!=0 (expected extra error from the unmodeled knob) subsets.

## Overall EQ / tonal balance (the headline answer to "is there a systematic bias")

**All 65 captures** (n=65):
- Low band (20-500Hz):  +1.86 dB average
- Mid band (500-4000Hz): +0.69 dB average
- High band (4000Hz+):   -0.54 dB average
- Log-spectral distance: 2.46 dB average

**Low=0 only (apples-to-apples, Low has no confound here)** (n=21):
- Low band (20-500Hz):  +1.96 dB average
- Mid band (500-4000Hz): +0.71 dB average
- High band (4000Hz+):   -0.56 dB average
- Log-spectral distance: 2.49 dB average

**Low!=0 only (Low's unmodeled effect expected to show up here)** (n=44):
- Low band (20-500Hz):  +1.81 dB average
- Mid band (500-4000Hz): +0.68 dB average
- High band (4000Hz+):   -0.52 dB average
- Log-spectral distance: 2.45 dB average

Positive = plugin has MORE energy than the reference in that band (too bright/present there); negative = less (too dark/thin there). This is the overall EQ character check - distinct from whether turning the Low/High knobs shifts things the right amount, which is what ml-toolkit/effects/ambience/findings.md and cross_validation_report.md already checked on the Python model directly.

## Other metrics (Low=0 subset)

- Envelope correlation: 0.938 (1.0 = identical shape)
- Crest factor diff (plugin - reference): -0.38 dB (negative = plugin more compressed/squashed than the real hardware)
- Spectral flatness diff (plugin - reference): +1.06 dB (negative = plugin tail more tonal/comb-filtered than the reference's diffuse character)

## Per-setting results

| file | Time | Low | High | env corr | LSD (dB) | EQ low/mid/high (dB) | crest diff (dB) | flatness diff (dB) |
|---|---|---|---|---|---|---|---|---|
| Ambience_0.1s_0L-8H.wav | 0.1 | 0 | -8 | 0.861 | 4.71 | +5.9 / +4.5 / +2.6 | -2.22 | -3.2 |
| Ambience_0.5s_0L-8H.wav | 0.5 | 0 | -8 | 0.949 | 3.35 | +4.7 / +1.1 / +0.1 | -3.98 | -2.11 |
| Ambience_1.1s_0L-8H.wav | 1.1 | 0 | -8 | 0.915 | 2.82 | +2.3 / -1.0 / -2.1 | -0.4 | -1.48 |
| Ambience_1.8s_0L-8H.wav | 1.8 | 0 | -8 | 0.931 | 1.99 | +1.1 / -0.4 / -1.7 | 0.96 | 3.84 |
| Ambience_2.3s_0L-8H.wav | 2.3 | 0 | -8 | 0.947 | 1.43 | -0.4 / -0.2 / -1.4 | 0.87 | 4.11 |
| Ambience_3.0s_0L-8H.wav | 3.0 | 0 | -8 | 0.947 | 1.36 | -0.4 / +0.1 / -1.2 | 1.15 | 2.04 |
| Ambience_4.5s_0L-5H.wav | 4.5 | 0 | -5 | 0.961 | 1.44 | -0.2 / -0.0 / -0.8 | 0.31 | 3.14 |
| Ambience_0.1s_0L-4H.wav | 0.1 | 0 | -4 | 0.879 | 5.27 | +6.9 / +5.0 / +2.4 | 0.11 | -2.37 |
| Ambience_0.5s_0L-4H.wav | 0.5 | 0 | -4 | 0.951 | 3.83 | +5.7 / +1.4 / -0.2 | -4.54 | -0.24 |
| Ambience_1.1s_0L-4H.wav | 1.1 | 0 | -4 | 0.914 | 3.00 | +3.2 / -0.7 / -2.3 | -1.05 | 0.28 |
| Ambience_1.8s_0L-4H.wav | 1.8 | 0 | -4 | 0.935 | 2.21 | +2.0 / -0.2 / -1.7 | 0.79 | 4.7 |
| Ambience_2.3s_0L-4H.wav | 2.3 | 0 | -4 | 0.954 | 1.47 | +0.6 / -0.1 / -1.2 | 0.47 | 4.37 |
| Ambience_3.0s_0L-4H.wav | 3.0 | 0 | -4 | 0.957 | 1.43 | +0.6 / +0.1 / -0.9 | 0.32 | 5.84 |
| Ambience_0.1s_0L0H.wav | 0.1 | 0 | 0 | 0.885 | 4.85 | +5.7 / +5.1 / +2.9 | 1.97 | 2.74 |
| Ambience_0.5s_0L0H.wav | 0.5 | 0 | 0 | 0.958 | 3.26 | +4.4 / +1.4 / +0.1 | -3.1 | 0.98 |
| Ambience_1.1s_0L0H.wav | 1.1 | 0 | 0 | 0.925 | 2.69 | +1.9 / -0.7 / -1.9 | -1.37 | 0.29 |
| Ambience_1.8s_0L0H.wav | 1.8 | 0 | 0 | 0.947 | 1.77 | +0.7 / -0.3 / -1.4 | 0.69 | 0.37 |
| Ambience_2.3s_0L0H.wav | 2.3 | 0 | 0 | 0.965 | 1.31 | -0.7 / -0.1 / -1.0 | 0.45 | -0.38 |
| Ambience_3.0s_0L0H.wav | 3.0 | 0 | 0 | 0.970 | 1.24 | -0.6 / +0.0 / -0.8 | 0.11 | 0.36 |
| Ambience_4.5s_0L0H.wav | 4.5 | 0 | 0 | 0.974 | 1.38 | -1.1 / -0.0 / -0.7 | 0.19 | -0.94 |
| Ambience_5.5s_0L0H.wav | 5.5 | 0 | 0 | 0.975 | 1.38 | -1.1 / +0.0 / -0.6 | 0.23 | -0.07 |
| Ambience_0.1s_-8L0H.wav ** | 0.1 | -8 | 0 | 0.882 | 4.77 | +5.6 / +5.0 / +2.8 | 1.81 | 2.72 |
| Ambience_0.5s_-8L0H.wav ** | 0.5 | -8 | 0 | 0.957 | 3.27 | +4.4 / +1.4 / +0.1 | -3.14 | 1.04 |
| Ambience_1.1s_-8L0H.wav ** | 1.1 | -8 | 0 | 0.925 | 2.69 | +1.9 / -0.7 / -2.0 | -1.55 | 0.23 |
| Ambience_1.8s_-8L0H.wav ** | 1.8 | -8 | 0 | 0.948 | 1.77 | +0.8 / -0.2 / -1.4 | 0.65 | 0.08 |
| Ambience_2.3s_-8L0H.wav ** | 2.3 | -8 | 0 | 0.964 | 1.29 | -0.4 / -0.1 / -1.0 | 0.42 | -0.17 |
| Ambience_3.0s_-8L0H.wav ** | 3.0 | -8 | 0 | 0.970 | 1.20 | -0.1 / +0.0 / -0.8 | 0.04 | 0.21 |
| Ambience_5.5s_-7L0H.wav ** | 5.5 | -7 | 0 | 0.976 | 1.27 | -0.2 / +0.0 / -0.6 | 0.19 | -0.04 |
| Ambience_4.5s_-5L0H.wav ** | 4.5 | -5 | 0 | 0.973 | 1.28 | -0.6 / -0.0 / -0.7 | 0.16 | -0.28 |
| Ambience_0.1s_-4L0H.wav ** | 0.1 | -4 | 0 | 0.875 | 4.83 | +5.7 / +5.0 / +2.9 | 1.81 | 0.0 |
| Ambience_0.5s_-4L0H.wav ** | 0.5 | -4 | 0 | 0.959 | 3.27 | +4.4 / +1.4 / +0.1 | -2.9 | 0.88 |
| Ambience_1.1s_-4L0H.wav ** | 1.1 | -4 | 0 | 0.926 | 2.69 | +1.9 / -0.7 / -1.9 | -1.42 | 0.15 |
| Ambience_1.8s_-4L0H.wav ** | 1.8 | -4 | 0 | 0.948 | 1.77 | +0.7 / -0.2 / -1.4 | 0.58 | -0.02 |
| Ambience_2.3s_-4L0H.wav ** | 2.3 | -4 | 0 | 0.965 | 1.30 | -0.6 / -0.1 / -1.0 | 0.49 | -0.73 |
| Ambience_3.0s_-4L0H.wav ** | 3.0 | -4 | 0 | 0.971 | 1.22 | -0.4 / +0.0 / -0.8 | 0.02 | -0.22 |
| Ambience_5.5s_-3L0H.wav ** | 5.5 | -3 | 0 | 0.975 | 1.36 | -1.0 / +0.0 / -0.6 | 0.13 | 0.1 |
| Ambience_0.1s_-2L-2H.wav ** | 0.1 | -2 | -2 | 0.878 | 5.18 | +6.6 / +5.0 / +2.6 | 0.89 | -0.48 |
| Ambience_0.5s_-2L-2H.wav ** | 0.5 | -2 | -2 | 0.956 | 3.64 | +5.3 / +1.4 / -0.1 | -4.13 | 0.3 |
| Ambience_1.1s_-2L-2H.wav ** | 1.1 | -2 | -2 | 0.918 | 2.87 | +2.9 / -0.7 / -2.2 | -1.73 | 0.75 |
| Ambience_1.8s_-2L-2H.wav ** | 1.8 | -2 | -2 | 0.944 | 2.08 | +1.6 / -0.3 / -1.5 | 0.5 | 2.16 |
| Ambience_2.3s_-2L-2H.wav ** | 2.3 | -2 | -2 | 0.958 | 1.39 | +0.2 / -0.2 / -1.1 | 0.76 | 4.99 |
| Ambience_3.0s_-2L-2H.wav ** | 3.0 | -2 | -2 | 0.958 | 1.34 | +0.2 / -0.1 / -0.8 | 0.46 | 9.06 |
| Ambience_4.5s_-2L-2H.wav ** | 4.5 | -2 | -2 | 0.970 | 1.45 | +0.1 / +0.1 / -0.8 | 0.06 | -4.52 |
| Ambience_5.5s_-2L-2H.wav ** | 5.5 | -2 | -2 | 0.972 | 1.45 | +0.0 / +0.2 / -0.7 | -0.1 | -5.13 |
| Ambience_5.5s_+2L-3H.wav ** | 5.5 | 2 | -3 | 0.971 | 1.47 | -0.3 / +0.0 / -0.6 | -0.67 | -7.08 |
| Ambience_0.1s_+3L-5H.wav ** | 0.1 | 3 | -5 | 0.873 | 5.19 | +6.8 / +4.9 / +2.4 | -0.69 | -3.0 |
| Ambience_0.5s_+3L-5H.wav ** | 0.5 | 3 | -5 | 0.950 | 3.79 | +5.6 / +1.4 / -0.2 | -4.28 | -0.79 |
| Ambience_1.1s_+3L-5H.wav ** | 1.1 | 3 | -5 | 0.913 | 2.80 | +1.6 / -1.6 / -1.1 | 0.46 | 2.09 |
| Ambience_1.8s_+3L-5H.wav ** | 1.8 | 3 | -5 | 0.936 | 2.19 | +1.9 / -0.2 / -1.7 | 0.49 | 4.56 |
| Ambience_2.3s_+3L-5H.wav ** | 2.3 | 3 | -5 | 0.952 | 1.45 | +0.3 / -0.1 / -1.3 | 0.74 | 5.24 |
| Ambience_4.5s_+3L-5H.wav ** | 4.5 | 3 | -5 | 0.962 | 1.47 | -0.5 / -0.0 / -0.8 | 0.29 | 0.89 |
| Ambience_0.1s_+5L-8H.wav ** | 0.1 | 5 | -8 | 0.872 | 4.73 | +5.9 / +4.5 / +2.6 | -1.13 | -5.47 |
| Ambience_0.5s_+5L-8H.wav ** | 0.5 | 5 | -8 | 0.949 | 3.35 | +4.7 / +1.1 / +0.1 | -4.06 | -2.11 |
| Ambience_1.1s_+5L-8H.wav ** | 1.1 | 5 | -8 | 0.918 | 2.82 | +2.3 / -1.0 / -2.1 | -0.48 | -1.79 |
| Ambience_1.8s_+5L-8H.wav ** | 1.8 | 5 | -8 | 0.928 | 1.99 | +0.9 / -0.4 / -1.6 | 1.65 | 4.64 |
| Ambience_2.3s_+5L-8H.wav ** | 2.3 | 5 | -8 | 0.947 | 1.45 | -0.7 / -0.2 / -1.3 | 1.42 | 6.27 |
| Ambience_3.0s_+5L-8H.wav ** | 3.0 | 5 | -8 | 0.950 | 1.41 | -0.8 / +0.1 / -1.1 | 1.23 | -0.47 |
| Ambience_4.5_+5L-8H.wav ** | 4.5 | 5 | -8 | 0.955 | 1.64 | -1.9 / +0.1 / -0.9 | 1.3 | -0.88 |
| Ambience_5.5s_+5L-8H.wav ** | 5.5 | 5 | -8 | 0.960 | 1.58 | -1.9 / +0.0 / -0.6 | 0.99 | -6.53 |
| Ambience_1.1s_+6L-7H.wav ** | 1.1 | 6 | -7 | 0.913 | 2.84 | +2.4 / -1.0 / -2.0 | -0.07 | -0.51 |
| Ambience_0.1s_+6L-5H.wav ** | 0.1 | 6 | -5 | 0.880 | 5.19 | +6.8 / +4.9 / +2.4 | -0.22 | -4.33 |
| Ambience_0.5s_+6L-5H.wav ** | 0.5 | 6 | -5 | 0.949 | 3.79 | +5.6 / +1.4 / -0.2 | -4.35 | -0.38 |
| Ambience_1.8s_+6L-5H.wav ** | 1.8 | 6 | -5 | 0.933 | 2.19 | +1.8 / -0.2 / -1.7 | 1.06 | 5.85 |
| Ambience_2.3s_+6L-5H.wav ** | 2.3 | 6 | -5 | 0.953 | 1.46 | +0.1 / -0.1 / -1.3 | 0.99 | 5.95 |
| Ambience_3.0s_+6L-5H.wav ** | 3.0 | 6 | -5 | 0.956 | 1.41 | +0.0 / +0.1 / -1.0 | 0.88 | 7.61 |

** = nonzero Low, so a known-unmodeled variable is in play for that row.
