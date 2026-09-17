# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -15.17 | 10.317 | -35.56 |
| Fall rate error (dB/s) | -94.114 | -70.973 | -112.628 |
| Plateau droop error (dB/s) | -5.671 | -33.529 | 16.615 |
| Build-up error (ms) | -14.837 | -11.134 | -17.8 |
| Onset NED mean error (0-20ms) | -0.049 | -0.01 | -0.08 |
| Onset NED first-window error | -0.168 | -0.146 | -0.184 |
| Time-to-NED=0.9 error (ms) | -203.537 | -197.551 | -208.327 |
| Mixing time error (ms) | -161.872 | -164.218 | -159.995 |
| Mid/side ratio error (dB) | -0.178 | -0.117 | -0.227 |
| Log-spectral distance (dB, unsigned) | 2.932 | 2.748 | 3.08 |
| Crest factor error (dB) | -0.27 | -0.597 | -0.008 |
| Spectral flatness error (dB) | 3.632 | 2.131 | 4.833 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.632 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 2.131 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 4.833 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Knee time error (ms) (High!=0)**: -35.56 exceeds +/-30.0 - the gate's fall doesn't land where the real hardware's does - audible as the wrong overall gate length.

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0577 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0579 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0366 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0307 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0498 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0288 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0449 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0547 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0296 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -34.807 | -194.08 | 61.825 | -15.85 | 4.52 |
| NonLin_0.8s_-3H.wav | -29.864 | -201.62 | 49.861 | -17.891 | 4.31 |
| NonLin_2.2s_0H.wav | -29.864 | -92.09 | 12.038 | -16.893 | 3.08 |
| NonLin_4.8s_0H.wav | -31.768 | -55.08 | -25.001 | -17.801 | 3.14 |
| NonLin_7.0s_-7H.wav | -52.812 | -52.02 | 2.618 | -17.891 | 2.16 |
| NonLin_7.0s_0H.wav | 43.968 | -64.04 | -60.518 | -5.918 | 2.37 |
| NonLin_9.8s_-4H.wav | -29.864 | -62.4 | -18.555 | -18.888 | 2.37 |
| NonLin_9.8s_-9H.wav | -30.453 | -53.02 | -12.676 | -18.48 | 2.04 |
| NonLin_9.8s_0H.wav | 58.934 | -72.68 | -60.635 | -3.923 | 2.4 |