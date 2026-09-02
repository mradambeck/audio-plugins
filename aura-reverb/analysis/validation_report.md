# Phase D validation report

Plugin renders (via AuraRenderIR) vs. all 65 reference captures in `ml-toolkit/effects/ambience/captures/`, mapping each reference's Time and High directly (Aura's Time IS the hardware label - see module docstring). Low is passed through the filename but has no DSP effect (unwired - see AuraParameterMap.h), so results are split into Low=0 (fully comparable) and Low!=0 (expected extra error from the unmodeled knob) subsets.

## Overall EQ / tonal balance (the headline answer to "is there a systematic bias")

**All 65 captures** (n=65):
- Low band (20-500Hz):  -0.24 dB average
- Mid band (500-4000Hz): -1.50 dB average
- High band (4000Hz+):   +0.64 dB average
- Log-spectral distance: 3.43 dB average

**Low=0 only (apples-to-apples, Low has no confound here)** (n=21):
- Low band (20-500Hz):  -0.28 dB average
- Mid band (500-4000Hz): -1.59 dB average
- High band (4000Hz+):   +0.70 dB average
- Log-spectral distance: 3.46 dB average

**Low!=0 only (Low's unmodeled effect expected to show up here)** (n=44):
- Low band (20-500Hz):  -0.23 dB average
- Mid band (500-4000Hz): -1.45 dB average
- High band (4000Hz+):   +0.61 dB average
- Log-spectral distance: 3.41 dB average

Positive = plugin has MORE energy than the reference in that band (too bright/present there); negative = less (too dark/thin there). This is the overall EQ character check - distinct from whether turning the Low/High knobs shifts things the right amount, which is what ml-toolkit/effects/ambience/findings.md and cross_validation_report.md already checked on the Python model directly.

## Other metrics (Low=0 subset)

- Envelope correlation: 0.939 (1.0 = identical shape)
- Crest factor diff (plugin - reference): -0.07 dB (negative = plugin more compressed/squashed than the real hardware)
- Spectral flatness diff (plugin - reference): +2.56 dB (negative = plugin tail more tonal/comb-filtered than the reference's diffuse character)

## Per-setting results

| file | Time | Low | High | env corr | LSD (dB) | EQ low/mid/high (dB) | crest diff (dB) | flatness diff (dB) |
|---|---|---|---|---|---|---|---|---|
| Ambience_0.1s_0L-8H.wav | 0.1 | 0 | -8 | 0.894 | 3.04 | -2.1 / -1.5 / +2.9 | -0.96 | -1.06 |
| Ambience_0.5s_0L-8H.wav | 0.5 | 0 | -8 | 0.951 | 4.49 | +0.8 / -2.5 / +2.6 | -2.6 | 0.16 |
| Ambience_1.1s_0L-8H.wav | 1.1 | 0 | -8 | 0.910 | 5.05 | -1.4 / -4.7 / +0.9 | 1.0 | 3.08 |
| Ambience_1.8s_0L-8H.wav | 1.8 | 0 | -8 | 0.919 | 4.11 | -2.3 / -3.6 / +1.1 | 1.84 | 3.91 |
| Ambience_2.3s_0L-8H.wav | 2.3 | 0 | -8 | 0.944 | 4.22 | -4.2 / -4.3 / +2.1 | 1.86 | 13.47 |
| Ambience_3.0s_0L-8H.wav | 3.0 | 0 | -8 | 0.949 | 4.10 | -4.2 / -3.6 / +2.4 | 1.83 | 10.94 |
| Ambience_4.5s_0L-5H.wav | 4.5 | 0 | -5 | 0.946 | 3.64 | -4.7 / -3.2 / +1.6 | 0.78 | 8.4 |
| Ambience_0.1s_0L-4H.wav | 0.1 | 0 | -4 | 0.905 | 2.28 | +0.3 / +0.7 / +1.8 | -0.02 | -3.97 |
| Ambience_0.5s_0L-4H.wav | 0.5 | 0 | -4 | 0.951 | 3.78 | +3.0 / -0.8 / +1.4 | -4.14 | -0.19 |
| Ambience_1.1s_0L-4H.wav | 1.1 | 0 | -4 | 0.926 | 4.41 | +0.6 / -3.2 / -0.4 | -0.43 | 2.09 |
| Ambience_1.8s_0L-4H.wav | 1.8 | 0 | -4 | 0.931 | 3.46 | -0.8 / -2.3 / -0.2 | 1.2 | 2.17 |
| Ambience_2.3s_0L-4H.wav | 2.3 | 0 | -4 | 0.951 | 3.53 | -3.1 / -3.6 / +0.8 | 1.1 | 10.71 |
| Ambience_3.0s_0L-4H.wav | 3.0 | 0 | -4 | 0.938 | 3.54 | -3.7 / -3.2 / +1.1 | 0.79 | 11.2 |
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
| Ambience_0.1s_-2L-2H.wav ** | 0.1 | -2 | -2 | 0.902 | 2.61 | +2.0 / +2.1 / +1.1 | -0.02 | -2.98 |
| Ambience_0.5s_-2L-2H.wav ** | 0.5 | -2 | -2 | 0.955 | 3.75 | +4.7 / +0.5 / +0.7 | -4.63 | -1.42 |
| Ambience_1.1s_-2L-2H.wav ** | 1.1 | -2 | -2 | 0.931 | 4.07 | +2.3 / -2.0 / -1.1 | -1.76 | 0.95 |
| Ambience_1.8s_-2L-2H.wav ** | 1.8 | -2 | -2 | 0.940 | 3.17 | +0.9 / -1.2 / -0.9 | 0.67 | -4.81 |
| Ambience_2.3s_-2L-2H.wav ** | 2.3 | -2 | -2 | 0.953 | 2.98 | -1.6 / -2.7 / -0.1 | 1.0 | 9.37 |
| Ambience_3.0s_-2L-2H.wav ** | 3.0 | -2 | -2 | 0.936 | 2.94 | -2.2 / -2.5 / +0.2 | 0.64 | 12.09 |
| Ambience_4.5s_-2L-2H.wav ** | 4.5 | -2 | -2 | 0.953 | 2.54 | -2.8 / -2.2 / +0.2 | 0.26 | -2.5 |
| Ambience_5.5s_-2L-2H.wav ** | 5.5 | -2 | -2 | 0.952 | 2.50 | -2.9 / -1.9 / +0.2 | 0.06 | -3.89 |
| Ambience_5.5s_+2L-3H.wav ** | 5.5 | 2 | -3 | 0.954 | 3.04 | -4.0 / -2.5 / +0.8 | -0.42 | -4.62 |
| Ambience_0.1s_+3L-5H.wav ** | 0.1 | 3 | -5 | 0.901 | 2.34 | -0.3 / +0.1 / +2.0 | -0.54 | -3.54 |
| Ambience_0.5s_+3L-5H.wav ** | 0.5 | 3 | -5 | 0.951 | 3.92 | +2.4 / -1.3 / +1.6 | -3.61 | -0.15 |
| Ambience_1.1s_+3L-5H.wav ** | 1.1 | 3 | -5 | 0.922 | 5.05 | -1.5 / -4.6 / +1.1 | 1.34 | 4.64 |
| Ambience_1.8s_+3L-5H.wav ** | 1.8 | 3 | -5 | 0.931 | 3.60 | -1.2 / -2.7 / +0.1 | 1.07 | 1.83 |
| Ambience_2.3s_+3L-5H.wav ** | 2.3 | 3 | -5 | 0.953 | 3.74 | -3.5 / -3.8 / +1.1 | 1.53 | 12.53 |
| Ambience_4.5s_+3L-5H.wav ** | 4.5 | 3 | -5 | 0.947 | 3.75 | -5.0 / -3.2 / +1.6 | 0.83 | 6.38 |
| Ambience_0.1s_+5L-8H.wav ** | 0.1 | 5 | -8 | 0.905 | 3.04 | -2.1 / -1.5 / +2.9 | -0.37 | -4.48 |
| Ambience_0.5s_+5L-8H.wav ** | 0.5 | 5 | -8 | 0.951 | 4.49 | +0.8 / -2.5 / +2.6 | -2.68 | 0.15 |
| Ambience_1.1s_+5L-8H.wav ** | 1.1 | 5 | -8 | 0.913 | 5.06 | -1.4 / -4.7 / +0.9 | 0.88 | 2.8 |
| Ambience_1.8s_+5L-8H.wav ** | 1.8 | 5 | -8 | 0.916 | 4.13 | -2.5 / -3.6 / +1.1 | 2.58 | 5.58 |
| Ambience_2.3s_+5L-8H.wav ** | 2.3 | 5 | -8 | 0.945 | 4.29 | -4.4 / -4.2 / +2.1 | 2.4 | 15.78 |
| Ambience_3.0s_+5L-8H.wav ** | 3.0 | 5 | -8 | 0.951 | 4.20 | -4.6 / -3.6 / +2.4 | 1.9 | 8.86 |
| Ambience_4.5_+5L-8H.wav ** | 4.5 | 5 | -8 | 0.956 | 4.24 | -5.5 / -3.3 / +2.7 | 1.86 | 7.19 |
| Ambience_5.5s_+5L-8H.wav ** | 5.5 | 5 | -8 | 0.962 | 4.08 | -5.1 / -3.0 / +2.9 | 1.54 | 0.73 |
| Ambience_1.1s_+6L-7H.wav ** | 1.1 | 6 | -7 | 0.913 | 4.95 | -1.1 / -4.5 / +0.7 | 1.23 | 3.35 |
| Ambience_0.1s_+6L-5H.wav ** | 0.1 | 6 | -5 | 0.908 | 2.34 | -0.3 / +0.1 / +2.0 | -0.23 | -6.31 |
| Ambience_0.5s_+6L-5H.wav ** | 0.5 | 6 | -5 | 0.950 | 3.92 | +2.5 / -1.3 / +1.6 | -3.64 | 0.36 |
| Ambience_1.8s_+6L-5H.wav ** | 1.8 | 6 | -5 | 0.929 | 3.62 | -1.3 / -2.7 / +0.1 | 1.63 | 4.24 |
| Ambience_2.3s_+6L-5H.wav ** | 2.3 | 6 | -5 | 0.954 | 3.80 | -3.7 / -3.8 / +1.1 | 1.7 | 13.69 |
| Ambience_3.0s_+6L-5H.wav ** | 3.0 | 6 | -5 | 0.948 | 3.79 | -4.2 / -3.3 / +1.4 | 1.44 | 13.93 |

** = nonzero Low, so a known-unmodeled variable is in play for that row.
