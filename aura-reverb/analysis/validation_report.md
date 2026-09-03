# Phase D validation report

Plugin renders (via AuraRenderIR) vs. all 65 reference captures in `ml-toolkit/effects/ambience/captures/`, mapping each reference's Time and High directly (Aura's Time IS the hardware label - see module docstring). Low is passed through the filename but has no DSP effect (unwired - see AuraParameterMap.h), so results are split into Low=0 (fully comparable) and Low!=0 (expected extra error from the unmodeled knob) subsets.

## Overall EQ / tonal balance (the headline answer to "is there a systematic bias")

**All 65 captures** (n=65):
- Low band (20-500Hz):  +0.87 dB average
- Mid band (500-4000Hz): +0.67 dB average
- High band (4000Hz+):   -0.53 dB average
- Log-spectral distance: 2.21 dB average

**Low=0 only (apples-to-apples, Low has no confound here)** (n=21):
- Low band (20-500Hz):  +0.95 dB average
- Mid band (500-4000Hz): +0.70 dB average
- High band (4000Hz+):   -0.55 dB average
- Log-spectral distance: 2.23 dB average

**Low!=0 only (Low's unmodeled effect expected to show up here)** (n=44):
- Low band (20-500Hz):  +0.84 dB average
- Mid band (500-4000Hz): +0.66 dB average
- High band (4000Hz+):   -0.52 dB average
- Log-spectral distance: 2.20 dB average

Positive = plugin has MORE energy than the reference in that band (too bright/present there); negative = less (too dark/thin there). This is the overall EQ character check - distinct from whether turning the Low/High knobs shifts things the right amount, which is what ml-toolkit/effects/ambience/findings.md and cross_validation_report.md already checked on the Python model directly.

## Other metrics (Low=0 subset)

- Envelope correlation: 0.938 (1.0 = identical shape)
- Crest factor diff (plugin - reference): +0.28 dB (negative = plugin more compressed/squashed than the real hardware)
- Spectral flatness diff (plugin - reference): +1.94 dB (negative = plugin tail more tonal/comb-filtered than the reference's diffuse character)

## Per-setting results

| file | Time | Low | High | env corr | LSD (dB) | EQ low/mid/high (dB) | crest diff (dB) | flatness diff (dB) |
|---|---|---|---|---|---|---|---|---|
| Ambience_0.1s_0L-8H.wav | 0.1 | 0 | -8 | 0.864 | 3.66 | +1.4 / +4.4 / +2.7 | -2.2 | -2.94 |
| Ambience_0.5s_0L-8H.wav | 0.5 | 0 | -8 | 0.950 | 2.83 | +3.4 / +1.1 / +0.1 | -3.94 | -1.86 |
| Ambience_1.1s_0L-8H.wav | 1.1 | 0 | -8 | 0.915 | 2.77 | +2.0 / -1.0 / -2.1 | 0.45 | -1.26 |
| Ambience_1.8s_0L-8H.wav | 1.8 | 0 | -8 | 0.930 | 1.87 | +0.6 / -0.4 / -1.6 | 1.69 | 5.34 |
| Ambience_2.3s_0L-8H.wav | 2.3 | 0 | -8 | 0.947 | 1.43 | -0.5 / -0.2 / -1.3 | 1.5 | 6.53 |
| Ambience_3.0s_0L-8H.wav | 3.0 | 0 | -8 | 0.948 | 1.36 | -0.5 / +0.1 / -1.2 | 1.97 | 9.05 |
| Ambience_4.5s_0L-5H.wav | 4.5 | 0 | -5 | 0.961 | 1.44 | -0.3 / -0.0 / -0.8 | 1.31 | 7.28 |
| Ambience_0.1s_0L-4H.wav | 0.1 | 0 | -4 | 0.881 | 3.98 | +2.4 / +4.9 / +2.4 | 0.16 | -2.14 |
| Ambience_0.5s_0L-4H.wav | 0.5 | 0 | -4 | 0.951 | 3.30 | +4.3 / +1.4 / -0.2 | -4.66 | -0.1 |
| Ambience_1.1s_0L-4H.wav | 1.1 | 0 | -4 | 0.914 | 2.93 | +3.0 / -0.7 / -2.3 | -1.07 | 0.35 |
| Ambience_1.8s_0L-4H.wav | 1.8 | 0 | -4 | 0.935 | 2.08 | +1.5 / -0.2 / -1.7 | 1.47 | 4.98 |
| Ambience_2.3s_0L-4H.wav | 2.3 | 0 | -4 | 0.954 | 1.46 | +0.4 / -0.1 / -1.2 | 1.38 | 4.8 |
| Ambience_3.0s_0L-4H.wav | 3.0 | 0 | -4 | 0.957 | 1.41 | +0.5 / +0.1 / -0.9 | 1.35 | 7.08 |
| Ambience_0.1s_0L0H.wav | 0.1 | 0 | 0 | 0.886 | 3.80 | +1.1 / +5.0 / +2.9 | 1.88 | 2.78 |
| Ambience_0.5s_0L0H.wav | 0.5 | 0 | 0 | 0.958 | 2.72 | +3.0 / +1.4 / +0.1 | -1.65 | 1.02 |
| Ambience_1.1s_0L0H.wav | 1.1 | 0 | 0 | 0.925 | 2.65 | +1.6 / -0.7 / -1.9 | -0.22 | 0.32 |
| Ambience_1.8s_0L0H.wav | 1.8 | 0 | 0 | 0.947 | 1.70 | +0.1 / -0.3 / -1.4 | 1.79 | 0.42 |
| Ambience_2.3s_0L0H.wav | 2.3 | 0 | 0 | 0.965 | 1.33 | -0.8 / -0.1 / -1.0 | 1.75 | -0.36 |
| Ambience_3.0s_0L0H.wav | 3.0 | 0 | 0 | 0.970 | 1.26 | -0.7 / +0.0 / -0.8 | 1.06 | 0.37 |
| Ambience_4.5s_0L0H.wav | 4.5 | 0 | 0 | 0.974 | 1.40 | -1.3 / -0.0 / -0.7 | 1.2 | -0.92 |
| Ambience_5.5s_0L0H.wav | 5.5 | 0 | 0 | 0.975 | 1.41 | -1.3 / +0.0 / -0.6 | 0.69 | -0.06 |
| Ambience_0.1s_-8L0H.wav ** | 0.1 | -8 | 0 | 0.883 | 3.74 | +1.0 / +4.9 / +2.8 | 1.72 | 2.77 |
| Ambience_0.5s_-8L0H.wav ** | 0.5 | -8 | 0 | 0.958 | 2.72 | +3.0 / +1.4 / +0.1 | -1.91 | 1.08 |
| Ambience_1.1s_-8L0H.wav ** | 1.1 | -8 | 0 | 0.925 | 2.65 | +1.6 / -0.7 / -2.0 | -0.27 | 0.26 |
| Ambience_1.8s_-8L0H.wav ** | 1.8 | -8 | 0 | 0.948 | 1.69 | +0.2 / -0.3 / -1.4 | 1.99 | 0.15 |
| Ambience_2.3s_-8L0H.wav ** | 2.3 | -8 | 0 | 0.964 | 1.30 | -0.6 / -0.1 / -1.0 | 1.66 | -0.15 |
| Ambience_3.0s_-8L0H.wav ** | 3.0 | -8 | 0 | 0.970 | 1.20 | -0.3 / +0.0 / -0.8 | 1.1 | 0.23 |
| Ambience_5.5s_-7L0H.wav ** | 5.5 | -7 | 0 | 0.976 | 1.27 | -0.4 / +0.0 / -0.6 | 1.01 | -0.03 |
| Ambience_4.5s_-5L0H.wav ** | 4.5 | -5 | 0 | 0.973 | 1.29 | -0.8 / -0.0 / -0.7 | 1.07 | -0.27 |
| Ambience_0.1s_-4L0H.wav ** | 0.1 | -4 | 0 | 0.876 | 3.79 | +1.1 / +4.9 / +2.9 | 1.7 | 0.03 |
| Ambience_0.5s_-4L0H.wav ** | 0.5 | -4 | 0 | 0.959 | 2.72 | +3.0 / +1.4 / +0.1 | -1.04 | 0.94 |
| Ambience_1.1s_-4L0H.wav ** | 1.1 | -4 | 0 | 0.926 | 2.65 | +1.6 / -0.7 / -1.9 | -0.14 | 0.18 |
| Ambience_1.8s_-4L0H.wav ** | 1.8 | -4 | 0 | 0.948 | 1.70 | +0.1 / -0.3 / -1.4 | 1.94 | 0.04 |
| Ambience_2.3s_-4L0H.wav ** | 2.3 | -4 | 0 | 0.965 | 1.31 | -0.8 / -0.1 / -1.0 | 1.94 | -0.69 |
| Ambience_3.0s_-4L0H.wav ** | 3.0 | -4 | 0 | 0.971 | 1.24 | -0.6 / +0.0 / -0.8 | 1.22 | -0.19 |
| Ambience_5.5s_-3L0H.wav ** | 5.5 | -3 | 0 | 0.975 | 1.38 | -1.1 / +0.0 / -0.6 | 0.07 | 0.1 |
| Ambience_0.1s_-2L-2H.wav ** | 0.1 | -2 | -2 | 0.880 | 3.91 | +2.0 / +4.9 / +2.6 | 0.9 | -0.37 |
| Ambience_0.5s_-2L-2H.wav ** | 0.5 | -2 | -2 | 0.956 | 3.10 | +4.0 / +1.4 / -0.1 | -3.25 | 0.42 |
| Ambience_1.1s_-2L-2H.wav ** | 1.1 | -2 | -2 | 0.918 | 2.80 | +2.6 / -0.7 / -2.2 | -1.38 | 0.79 |
| Ambience_1.8s_-2L-2H.wav ** | 1.8 | -2 | -2 | 0.944 | 1.95 | +1.1 / -0.3 / -1.5 | 2.39 | 3.37 |
| Ambience_2.3s_-2L-2H.wav ** | 2.3 | -2 | -2 | 0.958 | 1.39 | +0.0 / -0.2 / -1.1 | 1.87 | 5.12 |
| Ambience_3.0s_-2L-2H.wav ** | 3.0 | -2 | -2 | 0.958 | 1.33 | +0.1 / -0.1 / -0.8 | 1.46 | 9.16 |
| Ambience_4.5s_-2L-2H.wav ** | 4.5 | -2 | -2 | 0.970 | 1.45 | -0.1 / +0.1 / -0.8 | 1.07 | -4.15 |
| Ambience_5.5s_-2L-2H.wav ** | 5.5 | -2 | -2 | 0.972 | 1.45 | -0.1 / +0.2 / -0.7 | 0.72 | -4.72 |
| Ambience_5.5s_+2L-3H.wav ** | 5.5 | 2 | -3 | 0.971 | 1.49 | -0.4 / +0.0 / -0.6 | 0.25 | -4.92 |
| Ambience_0.1s_+3L-5H.wav ** | 0.1 | 3 | -5 | 0.875 | 3.92 | +2.3 / +4.8 / +2.4 | -0.71 | -2.77 |
| Ambience_0.5s_+3L-5H.wav ** | 0.5 | 3 | -5 | 0.951 | 3.26 | +4.2 / +1.4 / -0.2 | -4.42 | -0.63 |
| Ambience_1.1s_+3L-5H.wav ** | 1.1 | 3 | -5 | 0.913 | 2.76 | +1.3 / -1.6 / -1.1 | 0.67 | 2.18 |
| Ambience_1.8s_+3L-5H.wav ** | 1.8 | 3 | -5 | 0.936 | 2.07 | +1.3 / -0.2 / -1.7 | 1.62 | 6.03 |
| Ambience_2.3s_+3L-5H.wav ** | 2.3 | 3 | -5 | 0.952 | 1.45 | +0.2 / -0.1 / -1.3 | 1.6 | 6.07 |
| Ambience_4.5s_+3L-5H.wav ** | 4.5 | 3 | -5 | 0.962 | 1.48 | -0.6 / -0.0 / -0.7 | 1.42 | 7.35 |
| Ambience_0.1s_+5L-8H.wav ** | 0.1 | 5 | -8 | 0.875 | 3.67 | +1.4 / +4.5 / +2.7 | -1.18 | -5.11 |
| Ambience_0.5s_+5L-8H.wav ** | 0.5 | 5 | -8 | 0.950 | 2.83 | +3.4 / +1.1 / +0.1 | -4.02 | -1.87 |
| Ambience_1.1s_+5L-8H.wav ** | 1.1 | 5 | -8 | 0.917 | 2.77 | +2.0 / -1.0 / -2.1 | 0.75 | -1.37 |
| Ambience_1.8s_+5L-8H.wav ** | 1.8 | 5 | -8 | 0.928 | 1.87 | +0.5 / -0.4 / -1.6 | 1.67 | 5.1 |
| Ambience_2.3s_+5L-8H.wav ** | 2.3 | 5 | -8 | 0.947 | 1.46 | -0.8 / -0.2 / -1.3 | 2.22 | 8.99 |
| Ambience_3.0s_+5L-8H.wav ** | 3.0 | 5 | -8 | 0.950 | 1.42 | -0.9 / +0.1 / -1.1 | 2.21 | 10.25 |
| Ambience_4.5_+5L-8H.wav ** | 4.5 | 5 | -8 | 0.955 | 1.66 | -1.9 / +0.1 / -0.9 | 2.2 | 12.55 |
| Ambience_5.5s_+5L-8H.wav ** | 5.5 | 5 | -8 | 0.961 | 1.62 | -1.9 / +0.0 / -0.6 | 1.91 | 11.46 |
| Ambience_1.1s_+6L-7H.wav ** | 1.1 | 6 | -7 | 0.913 | 2.78 | +2.2 / -1.0 / -2.0 | 0.05 | -0.38 |
| Ambience_0.1s_+6L-5H.wav ** | 0.1 | 6 | -5 | 0.883 | 3.92 | +2.3 / +4.8 / +2.4 | -0.22 | -4.01 |
| Ambience_0.5s_+6L-5H.wav ** | 0.5 | 6 | -5 | 0.949 | 3.26 | +4.2 / +1.4 / -0.2 | -4.35 | -0.23 |
| Ambience_1.8s_+6L-5H.wav ** | 1.8 | 6 | -5 | 0.933 | 2.06 | +1.2 / -0.2 / -1.7 | 1.68 | 6.18 |
| Ambience_2.3s_+6L-5H.wav ** | 2.3 | 6 | -5 | 0.953 | 1.46 | -0.1 / -0.1 / -1.3 | 1.93 | 7.44 |
| Ambience_3.0s_+6L-5H.wav ** | 3.0 | 6 | -5 | 0.956 | 1.41 | -0.1 / +0.1 / -1.0 | 1.75 | 9.63 |

** = nonzero Low, so a known-unmodeled variable is in play for that row.
