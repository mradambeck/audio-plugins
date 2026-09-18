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
- **Per-band plateau droop error (dB/s, mean |error|) (all)**: 106.101 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (all)**: 163.133 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High=0)**: 73.949 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High=0)**: 166.218 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High!=0)**: 131.822 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High!=0)**: 160.665 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- Per-band sign mismatches (all): 26 band/capture/metric combination(s) where the render decays in the OPPOSITE direction from the real hardware - a more severe failure than a magnitude-only miss. See validation_report.md's own 'Per-band sign mismatches' section for the full list.

## Per-band sign mismatches

Every octave band/capture/metric where the render decays in the OPPOSITE direction from the real hardware (see `_band_sign_mismatches`' own docstring) - a more severe failure than the magnitude-only per-band check above, since a sign flip can hide entirely inside a broadband average that still looks reasonable.

- Time=0.1 High=-3 44-89Hz plateau droop: render=6.98dB/s vs. real=-163.22dB/s - opposite direction
- Time=0.1 High=-3 1414-2828Hz plateau droop: render=-90.68dB/s vs. real=186.39dB/s - opposite direction
- Time=0.1 High=-3 2828-5657Hz plateau droop: render=-129.98dB/s vs. real=196.76dB/s - opposite direction
- Time=0.1 High=-3 5657-11314Hz plateau droop: render=-180.35dB/s vs. real=107.26dB/s - opposite direction
- Time=0.1 High=-3 11314-22049Hz fall rate: render=288.04dB/s vs. real=-405.28dB/s - opposite direction
- Time=0.8 High=-3 44-89Hz plateau droop: render=33.78dB/s vs. real=-163.67dB/s - opposite direction
- Time=0.8 High=-3 354-707Hz plateau droop: render=255.78dB/s vs. real=-803.23dB/s - opposite direction
- Time=0.8 High=-3 1414-2828Hz plateau droop: render=-65.20dB/s vs. real=186.37dB/s - opposite direction
- Time=0.8 High=-3 2828-5657Hz plateau droop: render=-108.59dB/s vs. real=196.77dB/s - opposite direction
- Time=0.8 High=-3 5657-11314Hz plateau droop: render=-164.34dB/s vs. real=107.25dB/s - opposite direction
- Time=0.8 High=-3 11314-22049Hz fall rate: render=435.86dB/s vs. real=-91.93dB/s - opposite direction
- Time=2.2 High=0 1414-2828Hz plateau droop: render=129.01dB/s vs. real=-33.73dB/s - opposite direction
- Time=2.2 High=0 11314-22049Hz fall rate: render=342.34dB/s vs. real=-111.72dB/s - opposite direction
- Time=4.8 High=0 177-354Hz plateau droop: render=3.18dB/s vs. real=-35.95dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz plateau droop: render=-148.75dB/s vs. real=48.59dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz fall rate: render=166.96dB/s vs. real=-129.29dB/s - opposite direction
- Time=7.0 High=0 88-177Hz plateau droop: render=18.31dB/s vs. real=-61.34dB/s - opposite direction
- Time=7.0 High=0 177-354Hz plateau droop: render=0.26dB/s vs. real=-40.86dB/s - opposite direction
- Time=7.0 High=0 354-707Hz plateau droop: render=9.21dB/s vs. real=-41.34dB/s - opposite direction
- Time=7.0 High=0 707-1414Hz plateau droop: render=10.63dB/s vs. real=-24.97dB/s - opposite direction
- Time=7.0 High=0 1414-2828Hz plateau droop: render=18.73dB/s vs. real=-34.38dB/s - opposite direction
- Time=7.0 High=0 2828-5657Hz plateau droop: render=5.88dB/s vs. real=-36.35dB/s - opposite direction
- Time=9.8 High=0 354-707Hz plateau droop: render=4.27dB/s vs. real=-39.71dB/s - opposite direction
- Time=9.8 High=0 707-1414Hz plateau droop: render=3.49dB/s vs. real=-37.50dB/s - opposite direction
- Time=9.8 High=0 1414-2828Hz plateau droop: render=12.17dB/s vs. real=-33.92dB/s - opposite direction
- Time=9.8 High=0 2828-5657Hz plateau droop: render=0.87dB/s vs. real=-34.16dB/s - opposite direction

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