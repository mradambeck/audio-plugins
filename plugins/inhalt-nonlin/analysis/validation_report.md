# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | 4.578 | 6.326 | 3.179 |
| Fall rate error (dB/s) | -55.021 | -35.86 | -70.35 |
| Plateau droop error (dB/s) | 77.282 | 12.131 | 129.404 |
| Build-up error (ms) | 3.359 | -5.896 | 10.762 |
| Onset NED mean error (0-20ms) | -0.046 | -0.012 | -0.073 |
| Onset NED first-window error | -0.167 | -0.136 | -0.192 |
| Time-to-NED=0.9 error (ms) | -202.872 | -188.571 | -214.313 |
| Mixing time error (ms) | -151.134 | -145.51 | -155.633 |
| Mid/side ratio error (dB) | -0.795 | -0.139 | -1.32 |
| Log-spectral distance (dB, unsigned) | 4.364 | 3.74 | 4.864 |
| Crest factor error (dB) | -0.632 | -0.625 | -0.638 |
| Spectral flatness error (dB) | 3.027 | 2.458 | 3.481 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.027 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Log-spectral distance (dB, unsigned) (all)**: 4.364 exceeds +/-4.0 - overall tonal balance is audibly off, not just a narrow band - see the per-band table for where.
- **Spectral flatness error (dB) (High=0)**: 2.458 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 3.481 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Log-spectral distance (dB, unsigned) (High!=0)**: 4.864 exceeds +/-4.0 - overall tonal balance is audibly off, not just a narrow band - see the per-band table for where.
- **Per-band plateau droop error (dB/s, mean |error|) (all)**: 85.412 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (all)**: 121.223 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High=0)**: 47.081 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High=0)**: 112.878 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High!=0)**: 116.076 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High!=0)**: 127.898 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- Per-band sign mismatches (all): 16 band/capture/metric combination(s) where the render decays in the OPPOSITE direction from the real hardware - a more severe failure than a magnitude-only miss. See validation_report.md's own 'Per-band sign mismatches' section for the full list.

## Per-band sign mismatches

Every octave band/capture/metric where the render decays in the OPPOSITE direction from the real hardware (see `_band_sign_mismatches`' own docstring) - a more severe failure than the magnitude-only per-band check above, since a sign flip can hide entirely inside a broadband average that still looks reasonable.

- Time=0.1 High=-3 177-354Hz plateau droop: render=124.44dB/s vs. real=-527.17dB/s - opposite direction
- Time=0.1 High=-3 354-707Hz plateau droop: render=77.81dB/s vs. real=-803.23dB/s - opposite direction
- Time=0.1 High=-3 2828-5657Hz plateau droop: render=-78.72dB/s vs. real=196.76dB/s - opposite direction
- Time=0.1 High=-3 5657-11314Hz plateau droop: render=-58.83dB/s vs. real=107.26dB/s - opposite direction
- Time=0.8 High=-3 177-354Hz plateau droop: render=80.83dB/s vs. real=-527.18dB/s - opposite direction
- Time=0.8 High=-3 354-707Hz plateau droop: render=3.29dB/s vs. real=-803.23dB/s - opposite direction
- Time=0.8 High=-3 1414-2828Hz plateau droop: render=-39.70dB/s vs. real=186.37dB/s - opposite direction
- Time=0.8 High=-3 2828-5657Hz plateau droop: render=-64.03dB/s vs. real=196.77dB/s - opposite direction
- Time=0.8 High=-3 5657-11314Hz plateau droop: render=-51.52dB/s vs. real=107.25dB/s - opposite direction
- Time=2.2 High=0 177-354Hz plateau droop: render=24.82dB/s vs. real=-166.86dB/s - opposite direction
- Time=2.2 High=0 707-1414Hz plateau droop: render=-39.78dB/s vs. real=25.29dB/s - opposite direction
- Time=2.2 High=0 11314-22049Hz fall rate: render=316.61dB/s vs. real=-111.72dB/s - opposite direction
- Time=4.8 High=0 1414-2828Hz plateau droop: render=1.55dB/s vs. real=-23.77dB/s - opposite direction
- Time=4.8 High=0 2828-5657Hz plateau droop: render=21.04dB/s vs. real=-33.44dB/s - opposite direction
- Time=4.8 High=0 5657-11314Hz plateau droop: render=21.66dB/s vs. real=-28.64dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz plateau droop: render=-11.89dB/s vs. real=48.59dB/s - opposite direction

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.335 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.2487 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0444 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0306 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0444 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0261 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0344 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0445 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0257 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | 2.29 | -189.93 | 290.408 | 64.15 | 8.99 |
| NonLin_0.8s_-3H.wav | 1.066 | -143.61 | 264.778 | 46.961 | 7.04 |
| NonLin_2.2s_0H.wav | 5.057 | -60.32 | 32.611 | -14.898 | 3.11 |
| NonLin_4.8s_0H.wav | -4.83 | -27.86 | 46.719 | -0.839 | 5.78 |
| NonLin_7.0s_-7H.wav | 5.238 | -8.21 | 37.905 | -18.707 | 2.65 |
| NonLin_7.0s_0H.wav | 14.036 | -29.49 | -15.682 | -4.92 | 2.88 |
| NonLin_9.8s_-4H.wav | 4.059 | -9.38 | 29.055 | -19.886 | 2.93 |
| NonLin_9.8s_-9H.wav | 3.243 | -0.62 | 24.872 | -18.707 | 2.71 |
| NonLin_9.8s_0H.wav | 11.043 | -25.77 | -15.125 | -2.925 | 3.19 |