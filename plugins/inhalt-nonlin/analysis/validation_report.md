# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -10.963 | 16.054 | -32.576 |
| Fall rate error (dB/s) | -60.451 | -31.08 | -83.948 |
| Plateau droop error (dB/s) | 22.643 | -0.298 | 40.995 |
| Build-up error (ms) | -12.071 | -6.145 | -16.812 |
| Onset NED mean error (0-20ms) | -0.048 | -0.015 | -0.075 |
| Onset NED first-window error | -0.17 | -0.148 | -0.187 |
| Time-to-NED=0.9 error (ms) | -204.868 | -200.544 | -208.327 |
| Mixing time error (ms) | -166.19 | -174.195 | -159.787 |
| Mid/side ratio error (dB) | -0.199 | -0.12 | -0.262 |
| Log-spectral distance (dB, unsigned) | 2.463 | 2.208 | 2.668 |
| Crest factor error (dB) | -0.372 | -0.967 | 0.103 |
| Spectral flatness error (dB) | 3.184 | 1.628 | 4.429 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.184 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 1.628 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 4.429 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Knee time error (ms) (High!=0)**: -32.576 exceeds +/-30.0 - the gate's fall doesn't land where the real hardware's does - audible as the wrong overall gate length.

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0539 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0541 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0384 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0293 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0594 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0449 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0499 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0618 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0412 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -25.828 | -179.46 | 69.134 | -15.85 | 4.12 |
| NonLin_0.8s_-3H.wav | -22.88 | -163.17 | 81.131 | -16.893 | 3.69 |
| NonLin_2.2s_0H.wav | -29.864 | -103.38 | 1.453 | -16.893 | 3.32 |
| NonLin_4.8s_0H.wav | -8.821 | -9.29 | 0.169 | -4.83 | 2.11 |
| NonLin_7.0s_-7H.wav | -53.628 | -22.07 | 30.097 | -16.712 | 1.83 |
| NonLin_7.0s_0H.wav | 43.968 | -2.47 | -1.815 | -1.927 | 1.7 |
| NonLin_9.8s_-4H.wav | -29.864 | -33.0 | 9.586 | -17.891 | 1.97 |
| NonLin_9.8s_-9H.wav | -30.68 | -22.04 | 15.029 | -16.712 | 1.73 |
| NonLin_9.8s_0H.wav | 58.934 | -9.18 | -0.998 | -0.93 | 1.7 |