# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -20.602 | 15.51 | -49.492 |
| Fall rate error (dB/s) | -90.824 | -76.1 | -102.604 |
| Plateau droop error (dB/s) | 4.072 | -45.893 | 44.044 |
| Build-up error (ms) | -12.62 | -8.186 | -16.167 |
| Onset NED mean error (0-20ms) | -0.059 | -0.06 | -0.058 |
| Onset NED first-window error | -0.224 | -0.239 | -0.212 |
| Time-to-NED=0.9 error (ms) | -190.234 | -175.102 | -202.34 |
| Mixing time error (ms) | -148.68 | -141.474 | -154.444 |
| Mid/side ratio error (dB) | -0.124 | -0.022 | -0.205 |
| Log-spectral distance (dB, unsigned) | 2.679 | 2.452 | 2.86 |
| Crest factor error (dB) | -0.23 | -0.37 | -0.118 |
| Spectral flatness error (dB) | 3.346 | 1.984 | 4.434 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.346 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 1.984 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 4.434 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Knee time error (ms) (High!=0)**: -49.492 exceeds +/-30.0 - the gate's fall doesn't land where the real hardware's does - audible as the wrong overall gate length.

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0613 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0616 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0464 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0483 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0659 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0443 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0579 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0727 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0455 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -55.76 | -176.52 | 106.845 | -14.852 | 4.48 |
| NonLin_0.8s_-3H.wav | -63.787 | -174.22 | 140.426 | -16.893 | 4.35 |
| NonLin_2.2s_0H.wav | -25.873 | -84.23 | 8.667 | -14.898 | 2.44 |
| NonLin_4.8s_0H.wav | -2.834 | -82.5 | -75.478 | -6.826 | 3.03 |
| NonLin_7.0s_-7H.wav | -48.64 | -51.19 | 1.455 | -15.714 | 1.79 |
| NonLin_7.0s_0H.wav | 40.385 | -67.45 | -58.795 | -6.508 | 2.15 |
| NonLin_9.8s_-4H.wav | -38.662 | -62.24 | -18.139 | -16.712 | 1.94 |
| NonLin_9.8s_-9H.wav | -40.612 | -48.85 | -10.368 | -16.666 | 1.74 |
| NonLin_9.8s_0H.wav | 50.363 | -70.22 | -57.967 | -4.513 | 2.19 |