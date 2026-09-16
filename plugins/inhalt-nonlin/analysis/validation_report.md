# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -16.372 | 8.413 | -36.199 |
| Fall rate error (dB/s) | -135.768 | -100.91 | -163.654 |
| Plateau droop error (dB/s) | -70.535 | -76.615 | -65.671 |
| Build-up error (ms) | -20.806 | -15.782 | -24.825 |
| Time-to-NED=0.9 error (ms) | -175.316 | -122.721 | -245.442 |
| Mixing time error (ms) | -77.245 | -89.728 | -64.762 |
| Mid/side ratio error (dB) | 0.012 | 0.01 | 0.014 |
| Log-spectral distance (dB, unsigned) | 5.064 | 3.885 | 6.008 |
| Crest factor error (dB) | 0.684 | -0.039 | 1.262 |
| Spectral flatness error (dB) | 4.784 | 2.302 | 6.769 |

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0752 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0706 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0431 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0442 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0407 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0487 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.043 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0389 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0489 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -13.016 | -286.27 | -116.033 | -16.009 | 7.34 |
| NonLin_0.8s_-3H.wav | -15.057 | -283.14 | -100.322 | -18.05 | 6.88 |
| NonLin_2.2s_0H.wav | -30.022 | -135.66 | -90.017 | -17.052 | 4.57 |
| NonLin_4.8s_0H.wav | -14.966 | -88.71 | -65.012 | -15.964 | 3.3 |
| NonLin_7.0s_-7H.wav | -61.95 | -79.33 | -28.087 | -30.022 | 5.32 |
| NonLin_7.0s_0H.wav | 35.828 | -85.9 | -76.203 | -16.054 | 3.78 |
| NonLin_9.8s_-4H.wav | -44.988 | -89.37 | -42.505 | -30.022 | 4.73 |
| NonLin_9.8s_-9H.wav | -45.986 | -80.16 | -41.408 | -30.022 | 5.77 |
| NonLin_9.8s_0H.wav | 42.812 | -93.37 | -75.227 | -14.059 | 3.89 |