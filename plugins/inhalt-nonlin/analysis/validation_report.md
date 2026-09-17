# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -37.569 | -27.597 | -45.546 |
| Fall rate error (dB/s) | -10.829 | -9.322 | -12.034 |
| Plateau droop error (dB/s) | 7.559 | 47.073 | -24.053 |
| Build-up error (ms) | -11.96 | -5.895 | -16.812 |
| Onset NED mean error (0-20ms) | -0.048 | -0.013 | -0.076 |
| Onset NED first-window error | -0.17 | -0.145 | -0.19 |
| Time-to-NED=0.9 error (ms) | -204.868 | -200.544 | -208.327 |
| Mixing time error (ms) | -166.19 | -174.195 | -159.787 |
| Mid/side ratio error (dB) | -0.204 | -0.147 | -0.249 |
| Log-spectral distance (dB, unsigned) | 2.423 | 2.192 | 2.608 |
| Crest factor error (dB) | -0.672 | -0.924 | -0.47 |
| Spectral flatness error (dB) | 3.204 | 1.75 | 4.367 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.204 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Knee time error (ms) (all)**: -37.569 exceeds +/-30.0 - the gate's fall doesn't land where the real hardware's does - audible as the wrong overall gate length.
- **Spectral flatness error (dB) (High=0)**: 1.75 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 4.367 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Knee time error (ms) (High!=0)**: -45.546 exceeds +/-30.0 - the gate's fall doesn't land where the real hardware's does - audible as the wrong overall gate length.

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0534 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0531 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0382 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0291 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0578 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0392 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0492 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0606 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0398 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -111.633 | -29.85 | -571.085 | -15.85 | 4.06 |
| NonLin_0.8s_-3H.wav | -96.712 | -38.4 | 406.757 | -16.893 | 3.63 |
| NonLin_2.2s_0H.wav | -86.735 | -36.15 | 185.533 | -16.893 | 3.26 |
| NonLin_4.8s_0H.wav | -17.8 | 2.03 | 1.254 | -4.83 | 2.12 |
| NonLin_7.0s_-7H.wav | 5.238 | 0.1 | 23.555 | -16.712 | 1.73 |
| NonLin_7.0s_0H.wav | -4.921 | -2.5 | -0.043 | -1.927 | 1.66 |
| NonLin_9.8s_-4H.wav | -11.904 | 4.09 | 7.795 | -17.891 | 1.93 |
| NonLin_9.8s_-9H.wav | -12.721 | 3.89 | 12.714 | -16.712 | 1.69 |
| NonLin_9.8s_0H.wav | -0.93 | -0.67 | 1.549 | 0.068 | 1.73 |