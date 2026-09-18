# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | 2.719 | 6.326 | -0.168 |
| Fall rate error (dB/s) | -44.481 | -29.9 | -56.146 |
| Plateau droop error (dB/s) | 53.755 | 13.215 | 86.187 |
| Build-up error (ms) | -7.148 | -2.902 | -10.544 |
| Onset NED mean error (0-20ms) | -0.047 | -0.013 | -0.074 |
| Onset NED first-window error | -0.164 | -0.134 | -0.189 |
| Time-to-NED=0.9 error (ms) | -197.551 | -187.075 | -205.932 |
| Mixing time error (ms) | -147.279 | -143.016 | -150.689 |
| Mid/side ratio error (dB) | -0.226 | -0.087 | -0.337 |
| Log-spectral distance (dB, unsigned) | 3.997 | 4.34 | 3.722 |
| Crest factor error (dB) | -0.229 | -0.549 | 0.027 |
| Spectral flatness error (dB) | 4.164 | 2.484 | 5.508 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 4.164 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 2.484 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Log-spectral distance (dB, unsigned) (High=0)**: 4.34 exceeds +/-4.0 - overall tonal balance is audibly off, not just a narrow band - see the per-band table for where.
- **Spectral flatness error (dB) (High!=0)**: 5.508 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Per-band plateau droop error (dB/s, mean |error|) (all)**: 90.256 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (all)**: 107.211 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High=0)**: 45.431 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High=0)**: 114.287 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High!=0)**: 126.117 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High!=0)**: 101.55 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- Per-band sign mismatches (all): 23 band/capture/metric combination(s) where the render decays in the OPPOSITE direction from the real hardware - a more severe failure than a magnitude-only miss. See validation_report.md's own 'Per-band sign mismatches' section for the full list.

## Per-band sign mismatches

Every octave band/capture/metric where the render decays in the OPPOSITE direction from the real hardware (see `_band_sign_mismatches`' own docstring) - a more severe failure than the magnitude-only per-band check above, since a sign flip can hide entirely inside a broadband average that still looks reasonable.

- Time=0.1 High=-3 88-177Hz plateau droop: render=102.63dB/s vs. real=-323.89dB/s - opposite direction
- Time=0.1 High=-3 177-354Hz plateau droop: render=65.08dB/s vs. real=-527.17dB/s - opposite direction
- Time=0.1 High=-3 2828-5657Hz plateau droop: render=-26.28dB/s vs. real=196.76dB/s - opposite direction
- Time=0.1 High=-3 5657-11314Hz plateau droop: render=-16.24dB/s vs. real=107.26dB/s - opposite direction
- Time=0.8 High=-3 88-177Hz plateau droop: render=67.86dB/s vs. real=-323.91dB/s - opposite direction
- Time=0.8 High=-3 177-354Hz plateau droop: render=40.04dB/s vs. real=-527.18dB/s - opposite direction
- Time=0.8 High=-3 1414-2828Hz plateau droop: render=-17.24dB/s vs. real=186.37dB/s - opposite direction
- Time=0.8 High=-3 2828-5657Hz plateau droop: render=-37.73dB/s vs. real=196.77dB/s - opposite direction
- Time=0.8 High=-3 5657-11314Hz plateau droop: render=-62.85dB/s vs. real=107.25dB/s - opposite direction
- Time=2.2 High=0 177-354Hz plateau droop: render=3.95dB/s vs. real=-166.86dB/s - opposite direction
- Time=2.2 High=0 707-1414Hz plateau droop: render=-29.42dB/s vs. real=25.29dB/s - opposite direction
- Time=2.2 High=0 11314-22049Hz fall rate: render=284.01dB/s vs. real=-111.72dB/s - opposite direction
- Time=4.8 High=0 1414-2828Hz plateau droop: render=7.09dB/s vs. real=-23.77dB/s - opposite direction
- Time=4.8 High=0 2828-5657Hz plateau droop: render=27.83dB/s vs. real=-33.44dB/s - opposite direction
- Time=4.8 High=0 5657-11314Hz plateau droop: render=24.23dB/s vs. real=-28.64dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz plateau droop: render=-11.12dB/s vs. real=48.59dB/s - opposite direction
- Time=7.0 High=-7 2828-5657Hz plateau droop: render=8.30dB/s vs. real=-36.03dB/s - opposite direction
- Time=7.0 High=-7 5657-11314Hz plateau droop: render=9.04dB/s vs. real=-29.02dB/s - opposite direction
- Time=7.0 High=0 2828-5657Hz plateau droop: render=1.83dB/s vs. real=-36.35dB/s - opposite direction
- Time=9.8 High=-4 2828-5657Hz plateau droop: render=4.88dB/s vs. real=-33.83dB/s - opposite direction
- Time=9.8 High=-4 5657-11314Hz plateau droop: render=4.47dB/s vs. real=-32.05dB/s - opposite direction
- Time=9.8 High=-9 2828-5657Hz plateau droop: render=5.87dB/s vs. real=-33.78dB/s - opposite direction
- Time=9.8 High=-9 5657-11314Hz plateau droop: render=4.25dB/s vs. real=-31.62dB/s - opposite direction

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0887 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0682 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0302 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0334 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0364 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.026 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0314 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0404 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0264 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | 2.109 | -210.68 | 165.989 | 2.109 | 4.43 |
| NonLin_0.8s_-3H.wav | 5.465 | -53.06 | 144.558 | -2.517 | 4.0 |
| NonLin_2.2s_0H.wav | 5.057 | -45.69 | 14.741 | -3.923 | 2.85 |
| NonLin_4.8s_0H.wav | -4.83 | -26.1 | 49.552 | -0.839 | 7.07 |
| NonLin_7.0s_-7H.wav | 5.238 | -6.61 | 48.469 | -16.712 | 3.31 |
| NonLin_7.0s_0H.wav | 14.036 | -25.86 | -6.333 | -3.923 | 3.68 |
| NonLin_9.8s_-4H.wav | 4.059 | -8.83 | 39.398 | -17.891 | 3.57 |
| NonLin_9.8s_-9H.wav | -17.709 | -1.55 | 32.521 | -17.709 | 3.3 |
| NonLin_9.8s_0H.wav | 11.043 | -21.95 | -5.102 | -2.925 | 3.76 |