# Phase D validation report

Plugin renders (via AuraRenderIR) vs. all 65 reference captures in `ml-toolkit/effects/ambience/captures/`, mapping each reference's Time and High directly (Aura's Time IS the hardware label - see module docstring). Low is passed through the filename but has no DSP effect (unwired - see AuraParameterMap.h), so results are split into Low=0 (fully comparable) and Low!=0 (expected extra error from the unmodeled knob) subsets.

## Overall EQ / tonal balance (the headline answer to "is there a systematic bias")

**All 65 captures** (n=65):
- Low band (20-500Hz):  +2.99 dB average
- Mid band (500-4000Hz): +0.61 dB average
- High band (4000Hz+):   -0.60 dB average
- Log-spectral distance: 2.99 dB average

**Low=0 only (apples-to-apples, Low has no confound here)** (n=21):
- Low band (20-500Hz):  +3.11 dB average
- Mid band (500-4000Hz): +0.63 dB average
- High band (4000Hz+):   -0.63 dB average
- Log-spectral distance: 3.04 dB average

**Low!=0 only (Low's unmodeled effect expected to show up here)** (n=44):
- Low band (20-500Hz):  +2.93 dB average
- Mid band (500-4000Hz): +0.60 dB average
- High band (4000Hz+):   -0.59 dB average
- Log-spectral distance: 2.97 dB average

Positive = plugin has MORE energy than the reference in that band (too bright/present there); negative = less (too dark/thin there). This is the overall EQ character check - distinct from whether turning the Low/High knobs shifts things the right amount, which is what ml-toolkit/effects/ambience/findings.md and cross_validation_report.md already checked on the Python model directly.

## Other metrics (Low=0 subset)

- Envelope correlation: 0.937 (1.0 = identical shape)
- Crest factor diff (plugin - reference): -0.50 dB (negative = plugin more compressed/squashed than the real hardware)
- Spectral flatness diff (plugin - reference): +0.21 dB (negative = plugin tail more tonal/comb-filtered than the reference's diffuse character)

## Per-setting results

| file | Time | Low | High | env corr | LSD (dB) | EQ low/mid/high (dB) | crest diff (dB) | flatness diff (dB) |
|---|---|---|---|---|---|---|---|---|
| Ambience_0.1s_0L-8H.wav | 0.1 | 0 | -8 | 0.884 | 3.38 | +4.7 / +3.2 / +0.5 | -2.95 | -6.48 |
| Ambience_0.5s_0L-8H.wav | 0.5 | 0 | -8 | 0.946 | 4.56 | +7.2 / +1.8 / -0.1 | -4.12 | -5.04 |
| Ambience_1.1s_0L-8H.wav | 1.1 | 0 | -8 | 0.930 | 3.81 | +4.9 / -0.5 / -1.9 | -0.14 | -2.1 |
| Ambience_1.8s_0L-8H.wav | 1.8 | 0 | -8 | 0.927 | 3.13 | +3.4 / +0.1 / -2.2 | 1.26 | -1.72 |
| Ambience_2.3s_0L-8H.wav | 2.3 | 0 | -8 | 0.946 | 2.17 | +1.9 / -0.3 / -1.0 | 1.31 | 8.29 |
| Ambience_3.0s_0L-8H.wav | 3.0 | 0 | -8 | 0.938 | 2.06 | +1.5 / +0.1 / -1.0 | 1.42 | 5.52 |
| Ambience_4.5s_0L-5H.wav | 4.5 | 0 | -5 | 0.929 | 1.62 | +0.3 / -0.0 / -0.5 | 0.62 | 5.52 |
| Ambience_0.1s_0L-4H.wav | 0.1 | 0 | -4 | 0.899 | 3.93 | +5.3 / +4.1 / +0.6 | -0.94 | -7.23 |
| Ambience_0.5s_0L-4H.wav | 0.5 | 0 | -4 | 0.947 | 4.95 | +7.9 / +2.4 / +0.1 | -4.89 | -2.67 |
| Ambience_1.1s_0L-4H.wav | 1.1 | 0 | -4 | 0.932 | 3.86 | +5.5 / -0.0 / -1.7 | -0.87 | -0.13 |
| Ambience_1.8s_0L-4H.wav | 1.8 | 0 | -4 | 0.929 | 3.11 | +3.8 / +0.6 / -1.8 | 0.85 | -0.44 |
| Ambience_2.3s_0L-4H.wav | 2.3 | 0 | -4 | 0.947 | 2.15 | +1.8 / -0.3 / -0.6 | 0.98 | 8.83 |
| Ambience_3.0s_0L-4H.wav | 3.0 | 0 | -4 | 0.927 | 2.05 | +1.1 / -0.1 / -0.4 | 0.68 | 9.22 |
| Ambience_0.1s_0L0H.wav | 0.1 | 0 | 0 | 0.907 | 3.53 | +4.0 / +4.0 / +0.9 | 0.57 | -1.31 |
| Ambience_0.5s_0L0H.wav | 0.5 | 0 | 0 | 0.957 | 4.54 | +6.7 / +2.2 / +0.3 | -4.12 | -1.75 |
| Ambience_1.1s_0L0H.wav | 1.1 | 0 | 0 | 0.938 | 3.66 | +4.2 / -0.3 / -1.5 | -1.6 | -0.62 |
| Ambience_1.8s_0L0H.wav | 1.8 | 0 | 0 | 0.947 | 2.95 | +2.9 / +0.5 / -1.3 | 0.46 | -5.27 |
| Ambience_2.3s_0L0H.wav | 2.3 | 0 | 0 | 0.965 | 2.32 | +0.4 / -1.1 / -0.7 | 0.45 | 1.76 |
| Ambience_3.0s_0L0H.wav | 3.0 | 0 | 0 | 0.961 | 2.31 | -0.1 / -0.9 / -0.5 | 0.14 | 1.33 |
| Ambience_4.5s_0L0H.wav | 4.5 | 0 | 0 | 0.961 | 1.90 | -1.1 / -1.0 / -0.3 | 0.16 | -0.78 |
| Ambience_5.5s_0L0H.wav | 5.5 | 0 | 0 | 0.962 | 1.86 | -1.1 / -0.9 / -0.3 | 0.17 | -0.5 |
| Ambience_0.1s_-8L0H.wav ** | 0.1 | -8 | 0 | 0.903 | 3.47 | +3.9 / +3.9 / +0.8 | 0.4 | -1.32 |
| Ambience_0.5s_-8L0H.wav ** | 0.5 | -8 | 0 | 0.956 | 4.55 | +6.7 / +2.2 / +0.3 | -4.18 | -1.56 |
| Ambience_1.1s_-8L0H.wav ** | 1.1 | -8 | 0 | 0.938 | 3.66 | +4.2 / -0.3 / -1.5 | -1.77 | -0.67 |
| Ambience_1.8s_-8L0H.wav ** | 1.8 | -8 | 0 | 0.948 | 2.94 | +3.0 / +0.5 / -1.3 | 0.4 | -6.1 |
| Ambience_2.3s_-8L0H.wav ** | 2.3 | -8 | 0 | 0.965 | 2.30 | +0.7 / -1.1 / -0.8 | 0.4 | 1.87 |
| Ambience_3.0s_-8L0H.wav ** | 3.0 | -8 | 0 | 0.962 | 2.27 | +0.4 / -0.9 / -0.5 | 0.06 | 1.27 |
| Ambience_5.5s_-7L0H.wav ** | 5.5 | -7 | 0 | 0.962 | 1.84 | -0.2 / -0.8 / -0.3 | 0.11 | -0.49 |
| Ambience_4.5s_-5L0H.wav ** | 4.5 | -5 | 0 | 0.961 | 1.89 | -0.6 / -1.0 / -0.3 | 0.15 | -0.19 |
| Ambience_0.1s_-4L0H.wav ** | 0.1 | -4 | 0 | 0.898 | 3.53 | +4.0 / +3.9 / +0.9 | 0.46 | -2.93 |
| Ambience_0.5s_-4L0H.wav ** | 0.5 | -4 | 0 | 0.958 | 4.55 | +6.7 / +2.2 / +0.3 | -3.94 | -2.26 |
| Ambience_1.1s_-4L0H.wav ** | 1.1 | -4 | 0 | 0.939 | 3.66 | +4.2 / -0.3 / -1.5 | -1.64 | -0.75 |
| Ambience_1.8s_-4L0H.wav ** | 1.8 | -4 | 0 | 0.949 | 2.94 | +2.9 / +0.5 / -1.3 | 0.35 | -6.35 |
| Ambience_2.3s_-4L0H.wav ** | 2.3 | -4 | 0 | 0.966 | 2.31 | +0.5 / -1.1 / -0.7 | 0.52 | 1.73 |
| Ambience_3.0s_-4L0H.wav ** | 3.0 | -4 | 0 | 0.962 | 2.31 | +0.1 / -1.0 / -0.5 | 0.08 | 1.05 |
| Ambience_5.5s_-3L0H.wav ** | 5.5 | -3 | 0 | 0.962 | 1.85 | -0.9 / -0.8 / -0.3 | 0.08 | -0.3 |
| Ambience_0.1s_-2L-2H.wav ** | 0.1 | -2 | -2 | 0.899 | 3.80 | +5.0 / +4.0 / +0.7 | -0.43 | -4.24 |
| Ambience_0.5s_-2L-2H.wav ** | 0.5 | -2 | -2 | 0.954 | 4.82 | +7.6 / +2.3 / +0.1 | -4.82 | -2.44 |
| Ambience_1.1s_-2L-2H.wav ** | 1.1 | -2 | -2 | 0.934 | 3.77 | +5.2 / -0.2 / -1.6 | -1.79 | 0.11 |
| Ambience_1.8s_-2L-2H.wav ** | 1.8 | -2 | -2 | 0.939 | 3.00 | +3.6 / +0.5 / -1.5 | 0.58 | -5.9 |
| Ambience_2.3s_-2L-2H.wav ** | 2.3 | -2 | -2 | 0.952 | 2.19 | +1.4 / -0.8 / -0.6 | 0.99 | 8.82 |
| Ambience_3.0s_-2L-2H.wav ** | 3.0 | -2 | -2 | 0.934 | 2.15 | +0.8 / -0.7 / -0.3 | 0.64 | 11.51 |
| Ambience_4.5s_-2L-2H.wav ** | 4.5 | -2 | -2 | 0.950 | 1.67 | +0.1 / -0.4 / -0.3 | 0.24 | -3.13 |
| Ambience_5.5s_-2L-2H.wav ** | 5.5 | -2 | -2 | 0.948 | 1.65 | -0.0 / -0.2 / -0.3 | 0.07 | -4.58 |
| Ambience_5.5s_+2L-3H.wav ** | 5.5 | 2 | -3 | 0.946 | 1.63 | -0.2 / -0.1 / -0.3 | -0.42 | -6.02 |
| Ambience_0.1s_+3L-5H.wav ** | 0.1 | 3 | -5 | 0.893 | 3.86 | +5.3 / +3.9 / +0.5 | -1.73 | -7.21 |
| Ambience_0.5s_+3L-5H.wav ** | 0.5 | 3 | -5 | 0.946 | 4.92 | +7.9 / +2.3 / -0.0 | -4.53 | -3.32 |
| Ambience_1.1s_+3L-5H.wav ** | 1.1 | 3 | -5 | 0.930 | 3.72 | +3.9 / -1.0 / -0.6 | 0.73 | 1.69 |
| Ambience_1.8s_+3L-5H.wav ** | 1.8 | 3 | -5 | 0.930 | 3.15 | +3.8 / +0.5 / -2.0 | 0.69 | -1.59 |
| Ambience_2.3s_+3L-5H.wav ** | 2.3 | 3 | -5 | 0.949 | 2.16 | +1.8 / -0.3 / -0.7 | 1.27 | 9.85 |
| Ambience_4.5s_+3L-5H.wav ** | 4.5 | 3 | -5 | 0.931 | 1.61 | -0.0 / +0.0 / -0.5 | 0.68 | 3.48 |
| Ambience_0.1s_+5L-8H.wav ** | 0.1 | 5 | -8 | 0.895 | 3.39 | +4.7 / +3.2 / +0.5 | -1.92 | -10.44 |
| Ambience_0.5s_+5L-8H.wav ** | 0.5 | 5 | -8 | 0.946 | 4.56 | +7.2 / +1.8 / -0.1 | -4.2 | -5.04 |
| Ambience_1.1s_+5L-8H.wav ** | 1.1 | 5 | -8 | 0.932 | 3.82 | +4.9 / -0.5 / -1.9 | -0.17 | -2.48 |
| Ambience_1.8s_+5L-8H.wav ** | 1.8 | 5 | -8 | 0.926 | 3.13 | +3.3 / +0.1 / -2.2 | 1.92 | -0.03 |
| Ambience_2.3s_+5L-8H.wav ** | 2.3 | 5 | -8 | 0.947 | 2.15 | +1.6 / -0.3 / -1.0 | 1.85 | 10.54 |
| Ambience_3.0s_+5L-8H.wav ** | 3.0 | 5 | -8 | 0.941 | 2.08 | +1.2 / +0.1 / -1.0 | 1.54 | 3.41 |
| Ambience_4.5_+5L-8H.wav ** | 4.5 | 5 | -8 | 0.941 | 1.62 | -0.0 / +0.1 / -0.9 | 1.55 | 1.66 |
| Ambience_5.5s_+5L-8H.wav ** | 5.5 | 5 | -8 | 0.946 | 1.61 | +0.1 / +0.2 / -0.9 | 1.25 | -5.08 |
| Ambience_1.1s_+6L-7H.wav ** | 1.1 | 6 | -7 | 0.929 | 3.81 | +4.9 / -0.5 / -1.7 | 0.2 | -1.03 |
| Ambience_0.1s_+6L-5H.wav ** | 0.1 | 6 | -5 | 0.900 | 3.86 | +5.3 / +3.9 / +0.5 | -1.25 | -10.41 |
| Ambience_0.5s_+6L-5H.wav ** | 0.5 | 6 | -5 | 0.945 | 4.92 | +7.9 / +2.3 / -0.0 | -4.66 | -2.72 |
| Ambience_1.8s_+6L-5H.wav ** | 1.8 | 6 | -5 | 0.928 | 3.15 | +3.7 / +0.5 / -1.9 | 1.17 | 0.9 |
| Ambience_2.3s_+6L-5H.wav ** | 2.3 | 6 | -5 | 0.950 | 2.15 | +1.6 / -0.2 / -0.7 | 1.47 | 10.98 |
| Ambience_3.0s_+6L-5H.wav ** | 3.0 | 6 | -5 | 0.936 | 2.07 | +1.0 / +0.0 / -0.5 | 1.23 | 11.15 |

** = nonzero Low, so a known-unmodeled variable is in play for that row.
