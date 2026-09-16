# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -11.816 | 15.351 | -33.551 |
| Fall rate error (dB/s) | -147.524 | -112.098 | -175.866 |
| Plateau droop error (dB/s) | -81.7 | -84.419 | -79.524 |
| Build-up error (ms) | -23.124 | -18.572 | -26.766 |
| Time-to-NED=0.9 error (ms) | -218.076 | -184.082 | -263.401 |
| Mixing time error (ms) | -150.925 | -181.474 | -126.485 |
| Mid/side ratio error (dB) | 0.013 | 0.091 | -0.05 |
| Log-spectral distance (dB, unsigned) | 5.739 | 4.597 | 6.652 |
| Crest factor error (dB) | 0.666 | -0.564 | 1.649 |
| Spectral flatness error (dB) | 4.645 | 2.183 | 6.615 |

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.1179 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.1062 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0602 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0525 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0438 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0472 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0456 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0439 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0472 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -1.587 | -308.28 | -146.528 | -15.555 | 7.63 |
| NonLin_0.8s_-3H.wav | -3.628 | -293.45 | -128.58 | -17.596 | 7.18 |
| NonLin_2.2s_0H.wav | -3.628 | -152.83 | -110.46 | -17.596 | 4.93 |
| NonLin_4.8s_0H.wav | -5.533 | -95.76 | -67.975 | -19.501 | 3.95 |
| NonLin_7.0s_-7H.wav | -67.483 | -90.88 | -31.071 | -33.56 | 6.18 |
| NonLin_7.0s_0H.wav | 30.295 | -98.12 | -79.662 | -19.592 | 4.7 |
| NonLin_9.8s_-4H.wav | -47.528 | -98.17 | -46.31 | -33.56 | 5.62 |
| NonLin_9.8s_-9H.wav | -47.528 | -88.55 | -45.129 | -33.56 | 6.65 |
| NonLin_9.8s_0H.wav | 40.272 | -101.68 | -79.581 | -17.597 | 4.81 |