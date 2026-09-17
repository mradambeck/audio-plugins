# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -14.265 | 10.409 | -34.004 |
| Fall rate error (dB/s) | -99.141 | -75.785 | -117.826 |
| Plateau droop error (dB/s) | -38.832 | -58.323 | -23.24 |
| Build-up error (ms) | -20.362 | -15.782 | -24.027 |
| Onset NED mean error (0-20ms) | 0.076 | 0.057 | 0.091 |
| Onset NED first-window error | 0.122 | 0.155 | 0.096 |
| Time-to-NED=0.9 error (ms) | -199.546 | -193.061 | -204.735 |
| Mixing time error (ms) | -151.801 | -170.295 | -137.007 |
| Mid/side ratio error (dB) | -0.006 | -0.005 | -0.006 |
| Log-spectral distance (dB, unsigned) | 3.446 | 2.592 | 4.128 |
| Crest factor error (dB) | 0.015 | -0.755 | 0.631 |
| Spectral flatness error (dB) | 4.292 | 2.074 | 6.066 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 4.292 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 2.074 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 6.066 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Log-spectral distance (dB, unsigned) (High!=0)**: 4.128 exceeds +/-4.0 - overall tonal balance is audibly off, not just a narrow band - see the per-band table for where.
- **Knee time error (ms) (High!=0)**: -34.004 exceeds +/-30.0 - the gate's fall doesn't land where the real hardware's does - audible as the wrong overall gate length.

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0663 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0644 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0419 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0437 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0357 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0438 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0376 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.035 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0438 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -9.025 | -205.5 | -37.071 | -15.011 | 4.67 |
| NonLin_0.8s_-3H.wav | -11.066 | -205.66 | -34.231 | -17.052 | 4.54 |
| NonLin_2.2s_0H.wav | -23.038 | -85.57 | -35.222 | -16.054 | 2.5 |
| NonLin_4.8s_0H.wav | -16.961 | -81.28 | -75.744 | -16.962 | 3.28 |
| NonLin_7.0s_-7H.wav | -58.957 | -55.32 | -5.886 | -29.024 | 3.93 |
| NonLin_7.0s_0H.wav | 38.821 | -64.9 | -61.841 | -16.054 | 2.28 |
| NonLin_9.8s_-4H.wav | -45.986 | -65.81 | -21.913 | -30.022 | 3.2 |
| NonLin_9.8s_-9H.wav | -44.988 | -56.84 | -17.099 | -29.024 | 4.3 |
| NonLin_9.8s_0H.wav | 42.812 | -71.39 | -60.485 | -14.059 | 2.31 |