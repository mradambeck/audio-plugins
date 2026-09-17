# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -15.817 | 10.409 | -36.798 |
| Fall rate error (dB/s) | -97.666 | -75.785 | -115.17 |
| Plateau droop error (dB/s) | -35.546 | -58.323 | -17.324 |
| Build-up error (ms) | -19.808 | -15.782 | -23.029 |
| Onset NED mean error (0-20ms) | 0.073 | 0.057 | 0.085 |
| Onset NED first-window error | 0.115 | 0.155 | 0.083 |
| Time-to-NED=0.9 error (ms) | -208.193 | -193.061 | -220.299 |
| Mixing time error (ms) | -162.887 | -170.295 | -156.961 |
| Mid/side ratio error (dB) | -0.012 | -0.005 | -0.018 |
| Log-spectral distance (dB, unsigned) | 2.708 | 2.592 | 2.8 |
| Crest factor error (dB) | -0.233 | -0.755 | 0.184 |
| Spectral flatness error (dB) | 3.475 | 2.074 | 4.596 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.475 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 2.074 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 4.596 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Knee time error (ms) (High!=0)**: -36.798 exceeds +/-30.0 - the gate's fall doesn't land where the real hardware's does - audible as the wrong overall gate length.

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0698 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0679 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0419 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0437 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0401 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0438 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0368 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0421 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0438 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -9.025 | -202.35 | -30.65 | -15.011 | 4.22 |
| NonLin_0.8s_-3H.wav | -11.066 | -202.23 | -27.391 | -17.052 | 4.17 |
| NonLin_2.2s_0H.wav | -23.038 | -85.57 | -35.222 | -16.054 | 2.5 |
| NonLin_4.8s_0H.wav | -16.961 | -81.28 | -75.744 | -16.962 | 3.28 |
| NonLin_7.0s_-7H.wav | -69.932 | -53.73 | -0.906 | -27.029 | 1.83 |
| NonLin_7.0s_0H.wav | 38.821 | -64.9 | -61.841 | -16.054 | 2.28 |
| NonLin_9.8s_-4H.wav | -45.986 | -65.69 | -17.843 | -29.024 | 2.03 |
| NonLin_9.8s_-9H.wav | -47.982 | -51.85 | -9.829 | -27.029 | 1.75 |
| NonLin_9.8s_0H.wav | 42.812 | -71.39 | -60.485 | -14.059 | 2.31 |