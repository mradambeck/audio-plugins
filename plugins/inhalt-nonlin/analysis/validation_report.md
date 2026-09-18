# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -13.29 | -25.351 | -3.642 |
| Fall rate error (dB/s) | -14.43 | -12.838 | -15.704 |
| Plateau droop error (dB/s) | 37.667 | 50.506 | 27.397 |
| Build-up error (ms) | -11.96 | -5.895 | -16.812 |
| Onset NED mean error (0-20ms) | -0.048 | -0.014 | -0.075 |
| Onset NED first-window error | -0.168 | -0.143 | -0.187 |
| Time-to-NED=0.9 error (ms) | -204.868 | -200.544 | -208.327 |
| Mixing time error (ms) | -166.19 | -174.195 | -159.787 |
| Mid/side ratio error (dB) | -0.203 | -0.147 | -0.248 |
| Log-spectral distance (dB, unsigned) | 2.449 | 2.217 | 2.634 |
| Crest factor error (dB) | -0.61 | -0.976 | -0.317 |
| Spectral flatness error (dB) | 3.215 | 1.752 | 4.386 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.215 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 1.752 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 4.386 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0543 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0547 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0383 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0284 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0583 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0396 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0496 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0611 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0405 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | 2.109 | -32.22 | 34.811 | -15.85 | 4.11 |
| NonLin_0.8s_-3H.wav | 1.066 | -47.78 | 56.155 | -16.893 | 3.69 |
| NonLin_2.2s_0H.wav | -89.728 | -47.04 | 201.238 | -16.893 | 3.36 |
| NonLin_4.8s_0H.wav | -5.827 | -1.62 | -1.049 | -4.83 | 2.11 |
| NonLin_7.0s_-7H.wav | -15.715 | -2.66 | 25.765 | -16.712 | 1.73 |
| NonLin_7.0s_0H.wav | -2.925 | -1.56 | 0.162 | -1.927 | 1.67 |
| NonLin_9.8s_-4H.wav | 2.064 | 1.91 | 7.191 | -17.891 | 1.94 |
| NonLin_9.8s_-9H.wav | -7.732 | 2.23 | 13.061 | -16.712 | 1.7 |
| NonLin_9.8s_0H.wav | -2.925 | -1.13 | 1.673 | 0.068 | 1.73 |