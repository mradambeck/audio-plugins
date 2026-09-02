# Phase D validation report

Plugin renders (via AuraRenderIR) vs. all 65 reference captures in `ml-toolkit/effects/ambience/captures/`, mapping each reference's Time and High directly (Aura's Time IS the hardware label - see module docstring). Low is passed through the filename but has no DSP effect (unwired - see AuraParameterMap.h), so results are split into Low=0 (fully comparable) and Low!=0 (expected extra error from the unmodeled knob) subsets.

## Overall EQ / tonal balance (the headline answer to "is there a systematic bias")

**All 65 captures** (n=65):
- Low band (20-500Hz):  +4.92 dB average
- Mid band (500-4000Hz): +0.64 dB average
- High band (4000Hz+):   -0.38 dB average
- Log-spectral distance: 3.54 dB average

**Low=0 only (apples-to-apples, Low has no confound here)** (n=21):
- Low band (20-500Hz):  +5.02 dB average
- Mid band (500-4000Hz): +0.67 dB average
- High band (4000Hz+):   -0.40 dB average
- Log-spectral distance: 3.59 dB average

**Low!=0 only (Low's unmodeled effect expected to show up here)** (n=44):
- Low band (20-500Hz):  +4.88 dB average
- Mid band (500-4000Hz): +0.63 dB average
- High band (4000Hz+):   -0.38 dB average
- Log-spectral distance: 3.51 dB average

Positive = plugin has MORE energy than the reference in that band (too bright/present there); negative = less (too dark/thin there). This is the overall EQ character check - distinct from whether turning the Low/High knobs shifts things the right amount, which is what ml-toolkit/effects/ambience/findings.md and cross_validation_report.md already checked on the Python model directly.

## Other metrics (Low=0 subset)

- Envelope correlation: 0.937 (1.0 = identical shape)
- Crest factor diff (plugin - reference): -0.32 dB (negative = plugin more compressed/squashed than the real hardware)
- Spectral flatness diff (plugin - reference): +2.17 dB (negative = plugin tail more tonal/comb-filtered than the reference's diffuse character)

## Per-setting results

| file | Time | Low | High | env corr | LSD (dB) | EQ low/mid/high (dB) | crest diff (dB) | flatness diff (dB) |
|---|---|---|---|---|---|---|---|---|
| Ambience_0.1s_0L-8H.wav | 0.1 | 0 | -8 | 0.847 | 6.41 | +9.1 / +4.7 / +3.3 | -2.07 | -1.86 |
| Ambience_0.5s_0L-8H.wav | 0.5 | 0 | -8 | 0.948 | 4.68 | +7.9 / +1.2 / +0.5 | -3.93 | -0.56 |
| Ambience_1.1s_0L-8H.wav | 1.1 | 0 | -8 | 0.922 | 3.98 | +5.3 / -1.0 / -1.9 | -0.49 | 0.04 |
| Ambience_1.8s_0L-8H.wav | 1.8 | 0 | -8 | 0.933 | 3.19 | +4.1 / -0.5 / -1.6 | 0.92 | 5.17 |
| Ambience_2.3s_0L-8H.wav | 2.3 | 0 | -8 | 0.947 | 2.33 | +2.7 / -0.3 / -1.4 | 0.83 | 4.97 |
| Ambience_3.0s_0L-8H.wav | 3.0 | 0 | -8 | 0.943 | 2.20 | +2.7 / -0.0 / -1.4 | 1.12 | 2.74 |
| Ambience_4.5s_0L-5H.wav | 4.5 | 0 | -5 | 0.958 | 2.19 | +3.0 / -0.0 / -0.9 | 0.33 | 3.87 |
| Ambience_0.1s_0L-4H.wav | 0.1 | 0 | -4 | 0.866 | 6.99 | +10.1 / +5.2 / +3.1 | 0.41 | -0.68 |
| Ambience_0.5s_0L-4H.wav | 0.5 | 0 | -4 | 0.950 | 5.15 | +8.8 / +1.5 / +0.2 | -4.44 | 1.03 |
| Ambience_1.1s_0L-4H.wav | 1.1 | 0 | -4 | 0.922 | 4.21 | +6.3 / -0.7 / -2.1 | -1.03 | 1.51 |
| Ambience_1.8s_0L-4H.wav | 1.8 | 0 | -4 | 0.938 | 3.53 | +5.1 / -0.2 / -1.6 | 0.77 | 5.97 |
| Ambience_2.3s_0L-4H.wav | 2.3 | 0 | -4 | 0.954 | 2.63 | +3.6 / -0.2 / -1.3 | 0.49 | 5.36 |
| Ambience_3.0s_0L-4H.wav | 3.0 | 0 | -4 | 0.954 | 2.58 | +3.7 / +0.1 / -1.0 | 0.33 | 6.74 |
| Ambience_0.1s_0L0H.wav | 0.1 | 0 | 0 | 0.873 | 6.47 | +8.8 / +5.2 / +3.5 | 2.28 | 3.83 |
| Ambience_0.5s_0L0H.wav | 0.5 | 0 | 0 | 0.957 | 4.56 | +7.4 / +1.4 / +0.4 | -3.03 | 2.04 |
| Ambience_1.1s_0L0H.wav | 1.1 | 0 | 0 | 0.932 | 3.81 | +4.9 / -0.8 / -1.8 | -1.17 | 1.42 |
| Ambience_1.8s_0L0H.wav | 1.8 | 0 | 0 | 0.949 | 2.94 | +3.6 / -0.4 / -1.3 | 0.77 | 1.49 |
| Ambience_2.3s_0L0H.wav | 2.3 | 0 | 0 | 0.965 | 2.13 | +2.3 / -0.3 / -1.0 | 0.5 | 0.55 |
| Ambience_3.0s_0L0H.wav | 3.0 | 0 | 0 | 0.969 | 2.02 | +2.4 / -0.2 / -0.8 | 0.19 | 1.19 |
| Ambience_4.5s_0L0H.wav | 4.5 | 0 | 0 | 0.973 | 1.70 | +1.8 / -0.2 / -0.7 | 0.27 | 0.08 |
| Ambience_5.5s_0L0H.wav | 5.5 | 0 | 0 | 0.975 | 1.70 | +1.8 / -0.2 / -0.6 | 0.27 | 0.67 |
| Ambience_0.1s_-8L0H.wav ** | 0.1 | -8 | 0 | 0.869 | 6.39 | +8.7 / +5.1 / +3.4 | 2.11 | 3.82 |
| Ambience_0.5s_-8L0H.wav ** | 0.5 | -8 | 0 | 0.956 | 4.57 | +7.4 / +1.4 / +0.4 | -3.07 | 2.04 |
| Ambience_1.1s_-8L0H.wav ** | 1.1 | -8 | 0 | 0.932 | 3.81 | +4.9 / -0.8 / -1.8 | -1.34 | 1.39 |
| Ambience_1.8s_-8L0H.wav ** | 1.8 | -8 | 0 | 0.950 | 2.96 | +3.8 / -0.4 / -1.3 | 0.71 | 1.32 |
| Ambience_2.3s_-8L0H.wav ** | 2.3 | -8 | 0 | 0.965 | 2.19 | +2.6 / -0.3 / -1.0 | 0.47 | 0.71 |
| Ambience_3.0s_-8L0H.wav ** | 3.0 | -8 | 0 | 0.970 | 2.14 | +2.9 / -0.2 / -0.8 | 0.12 | 1.15 |
| Ambience_5.5s_-7L0H.wav ** | 5.5 | -7 | 0 | 0.975 | 2.06 | +2.8 / -0.1 / -0.7 | 0.23 | 0.73 |
| Ambience_4.5s_-5L0H.wav ** | 4.5 | -5 | 0 | 0.973 | 1.89 | +2.4 / -0.2 / -0.7 | 0.23 | 0.53 |
| Ambience_0.1s_-4L0H.wav ** | 0.1 | -4 | 0 | 0.862 | 6.43 | +8.8 / +5.1 / +3.5 | 2.0 | 0.77 |
| Ambience_0.5s_-4L0H.wav ** | 0.5 | -4 | 0 | 0.958 | 4.57 | +7.4 / +1.4 / +0.4 | -2.77 | 2.13 |
| Ambience_1.1s_-4L0H.wav ** | 1.1 | -4 | 0 | 0.932 | 3.81 | +4.9 / -0.8 / -1.8 | -1.22 | 1.32 |
| Ambience_1.8s_-4L0H.wav ** | 1.8 | -4 | 0 | 0.950 | 2.95 | +3.7 / -0.4 / -1.3 | 0.64 | 1.23 |
| Ambience_2.3s_-4L0H.wav ** | 2.3 | -4 | 0 | 0.966 | 2.15 | +2.4 / -0.3 / -1.0 | 0.55 | 0.31 |
| Ambience_3.0s_-4L0H.wav ** | 3.0 | -4 | 0 | 0.970 | 2.05 | +2.5 / -0.2 / -0.8 | 0.12 | 0.85 |
| Ambience_5.5s_-3L0H.wav ** | 5.5 | -3 | 0 | 0.974 | 1.75 | +2.0 / -0.2 / -0.6 | 0.18 | 0.77 |
| Ambience_0.1s_-2L-2H.wav ** | 0.1 | -2 | -2 | 0.865 | 6.85 | +9.8 / +5.2 / +3.2 | 1.31 | 0.66 |
| Ambience_0.5s_-2L-2H.wav ** | 0.5 | -2 | -2 | 0.955 | 4.95 | +8.5 / +1.4 / +0.2 | -3.99 | 1.61 |
| Ambience_1.1s_-2L-2H.wav ** | 1.1 | -2 | -2 | 0.925 | 4.04 | +5.9 / -0.8 / -2.0 | -1.71 | 1.88 |
| Ambience_1.8s_-2L-2H.wav ** | 1.8 | -2 | -2 | 0.946 | 3.36 | +4.7 / -0.4 / -1.5 | 0.59 | 4.08 |
| Ambience_2.3s_-2L-2H.wav ** | 2.3 | -2 | -2 | 0.958 | 2.43 | +3.2 / -0.3 / -1.1 | 0.79 | 6.06 |
| Ambience_3.0s_-2L-2H.wav ** | 3.0 | -2 | -2 | 0.955 | 2.35 | +3.3 / -0.2 / -0.8 | 0.52 | 10.07 |
| Ambience_4.5s_-2L-2H.wav ** | 4.5 | -2 | -2 | 0.970 | 2.26 | +3.1 / +0.0 / -0.8 | 0.12 | -3.43 |
| Ambience_5.5s_-2L-2H.wav ** | 5.5 | -2 | -2 | 0.972 | 2.25 | +3.2 / +0.1 / -0.7 | -0.09 | -4.13 |
| Ambience_5.5s_+2L-3H.wav ** | 5.5 | 2 | -3 | 0.971 | 2.14 | +2.9 / +0.0 / -0.7 | -0.66 | -6.19 |
| Ambience_0.1s_+3L-5H.wav ** | 0.1 | 3 | -5 | 0.859 | 6.91 | +10.0 / +5.1 / +3.1 | -0.32 | -1.57 |
| Ambience_0.5s_+3L-5H.wav ** | 0.5 | 3 | -5 | 0.949 | 5.11 | +8.8 / +1.5 / +0.2 | -4.19 | 0.56 |
| Ambience_1.1s_+3L-5H.wav ** | 1.1 | 3 | -5 | 0.921 | 3.88 | +4.6 / -1.7 / -0.9 | 0.42 | 3.4 |
| Ambience_1.8s_+3L-5H.wav ** | 1.8 | 3 | -5 | 0.939 | 3.48 | +4.9 / -0.3 / -1.7 | 0.45 | 6.16 |
| Ambience_2.3s_+3L-5H.wav ** | 2.3 | 3 | -5 | 0.953 | 2.52 | +3.4 / -0.2 / -1.3 | 0.72 | 6.21 |
| Ambience_4.5s_+3L-5H.wav ** | 4.5 | 3 | -5 | 0.959 | 2.03 | +2.6 / -0.0 / -0.9 | 0.32 | 1.69 |
| Ambience_0.1s_+5L-8H.wav ** | 0.1 | 5 | -8 | 0.859 | 6.44 | +9.1 / +4.7 / +3.3 | -0.97 | -3.63 |
| Ambience_0.5s_+5L-8H.wav ** | 0.5 | 5 | -8 | 0.948 | 4.68 | +7.9 / +1.2 / +0.5 | -4.01 | -0.56 |
| Ambience_1.1s_+5L-8H.wav ** | 1.1 | 5 | -8 | 0.925 | 3.98 | +5.3 / -1.0 / -1.9 | -0.54 | -0.15 |
| Ambience_1.8s_+5L-8H.wav ** | 1.8 | 5 | -8 | 0.930 | 3.17 | +4.0 / -0.5 / -1.6 | 1.58 | 5.75 |
| Ambience_2.3s_+5L-8H.wav ** | 2.3 | 5 | -8 | 0.948 | 2.26 | +2.4 / -0.3 / -1.4 | 1.37 | 7.11 |
| Ambience_3.0s_+5L-8H.wav ** | 3.0 | 5 | -8 | 0.945 | 2.14 | +2.3 / +0.0 / -1.3 | 1.22 | 0.32 |
| Ambience_4.5_+5L-8H.wav ** | 4.5 | 5 | -8 | 0.950 | 1.67 | +1.2 / +0.0 / -1.1 | 1.33 | -0.31 |
| Ambience_5.5s_+5L-8H.wav ** | 5.5 | 5 | -8 | 0.956 | 1.64 | +1.3 / +0.0 / -0.9 | 1.02 | -6.13 |
| Ambience_1.1s_+6L-7H.wav ** | 1.1 | 6 | -7 | 0.921 | 4.01 | +5.5 / -1.0 / -1.8 | -0.13 | 0.87 |
| Ambience_0.1s_+6L-5H.wav ** | 0.1 | 6 | -5 | 0.867 | 6.91 | +10.0 / +5.1 / +3.1 | 0.07 | -2.22 |
| Ambience_0.5s_+6L-5H.wav ** | 0.5 | 6 | -5 | 0.948 | 5.11 | +8.8 / +1.5 / +0.2 | -4.27 | 0.9 |
| Ambience_1.8s_+6L-5H.wav ** | 1.8 | 6 | -5 | 0.936 | 3.45 | +4.8 / -0.3 / -1.7 | 1.02 | 7.07 |
| Ambience_2.3s_+6L-5H.wav ** | 2.3 | 6 | -5 | 0.954 | 2.47 | +3.2 / -0.2 / -1.3 | 0.96 | 6.97 |
| Ambience_3.0s_+6L-5H.wav ** | 3.0 | 6 | -5 | 0.953 | 2.35 | +3.1 / +0.1 / -1.1 | 0.87 | 8.47 |

** = nonzero Low, so a known-unmodeled variable is in play for that row.
