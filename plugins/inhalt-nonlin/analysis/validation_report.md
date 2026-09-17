# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -13.069 | -12.381 | -13.619 |
| Fall rate error (dB/s) | -60.304 | -31.387 | -83.438 |
| Plateau droop error (dB/s) | 21.921 | 0.758 | 38.852 |
| Build-up error (ms) | -11.96 | -5.895 | -16.812 |
| Onset NED mean error (0-20ms) | -0.048 | -0.014 | -0.076 |
| Onset NED first-window error | -0.17 | -0.145 | -0.19 |
| Time-to-NED=0.9 error (ms) | -204.868 | -200.544 | -208.327 |
| Mixing time error (ms) | -166.19 | -174.195 | -159.787 |
| Mid/side ratio error (dB) | -0.204 | -0.148 | -0.249 |
| Log-spectral distance (dB, unsigned) | 2.447 | 2.215 | 2.632 |
| Crest factor error (dB) | -0.33 | -0.798 | 0.045 |
| Spectral flatness error (dB) | 3.211 | 1.756 | 4.375 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.211 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 1.756 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 4.375 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.054 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0541 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0384 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0289 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0581 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0392 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0495 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.061 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0397 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -25.828 | -179.59 | 69.132 | -15.85 | 4.12 |
| NonLin_0.8s_-3H.wav | -22.88 | -163.12 | 81.134 | -16.893 | 3.69 |
| NonLin_2.2s_0H.wav | -29.864 | -103.4 | 1.454 | -16.893 | 3.32 |
| NonLin_4.8s_0H.wav | -13.809 | -9.72 | 0.082 | -4.83 | 2.14 |
| NonLin_7.0s_-7H.wav | 5.238 | -21.95 | 23.488 | -16.712 | 1.73 |
| NonLin_7.0s_0H.wav | -4.921 | -2.49 | -0.043 | -1.927 | 1.66 |
| NonLin_9.8s_-4H.wav | -11.904 | -30.63 | 7.793 | -17.891 | 1.93 |
| NonLin_9.8s_-9H.wav | -12.721 | -21.9 | 12.714 | -16.712 | 1.69 |
| NonLin_9.8s_0H.wav | -0.93 | -9.94 | 1.537 | 0.068 | 1.74 |