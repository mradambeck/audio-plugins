# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -4.754 | -12.63 | 1.546 |
| Fall rate error (dB/s) | -89.944 | -80.983 | -97.114 |
| Plateau droop error (dB/s) | -0.926 | -13.234 | 8.92 |
| Build-up error (ms) | -8.524 | -2.404 | -13.419 |
| Onset NED mean error (0-20ms) | -0.049 | -0.012 | -0.078 |
| Onset NED first-window error | -0.164 | -0.136 | -0.187 |
| Time-to-NED=0.9 error (ms) | -183.583 | -169.116 | -195.156 |
| Mixing time error (ms) | -130.161 | -151.746 | -112.893 |
| Mid/side ratio error (dB) | -0.246 | -0.119 | -0.349 |
| Log-spectral distance (dB, unsigned) | 3.826 | 3.535 | 4.058 |
| Crest factor error (dB) | -0.353 | -0.391 | -0.323 |
| Spectral flatness error (dB) | 4.201 | 2.479 | 5.578 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 4.201 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 2.479 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 5.578 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Log-spectral distance (dB, unsigned) (High!=0)**: 4.058 exceeds +/-4.0 - overall tonal balance is audibly off, not just a narrow band - see the per-band table for where.
- **Per-band plateau droop error (dB/s, mean |error|) (all)**: 66.276 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (all)**: 97.424 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High=0)**: 56.135 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High=0)**: 95.111 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High!=0)**: 74.389 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High!=0)**: 99.274 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- Per-band sign mismatches (all): 13 band/capture/metric combination(s) where the render decays in the OPPOSITE direction from the real hardware - a more severe failure than a magnitude-only miss. See validation_report.md's own 'Per-band sign mismatches' section for the full list.

## Per-band sign mismatches

Every octave band/capture/metric where the render decays in the OPPOSITE direction from the real hardware (see `_band_sign_mismatches`' own docstring) - a more severe failure than the magnitude-only per-band check above, since a sign flip can hide entirely inside a broadband average that still looks reasonable.

- Time=0.1 High=-3 1414-2828Hz plateau droop: render=-178.69dB/s vs. real=186.39dB/s - opposite direction
- Time=0.1 High=-3 2828-5657Hz plateau droop: render=-227.25dB/s vs. real=196.76dB/s - opposite direction
- Time=0.1 High=-3 5657-11314Hz plateau droop: render=-147.00dB/s vs. real=107.26dB/s - opposite direction
- Time=0.8 High=-3 5657-11314Hz plateau droop: render=-163.65dB/s vs. real=107.25dB/s - opposite direction
- Time=0.8 High=-3 11314-22049Hz fall rate: render=378.79dB/s vs. real=-91.93dB/s - opposite direction
- Time=2.2 High=0 44-89Hz plateau droop: render=70.05dB/s vs. real=-206.98dB/s - opposite direction
- Time=2.2 High=0 707-1414Hz plateau droop: render=-141.70dB/s vs. real=25.29dB/s - opposite direction
- Time=2.2 High=0 11314-22049Hz fall rate: render=352.13dB/s vs. real=-111.72dB/s - opposite direction
- Time=4.8 High=0 2828-5657Hz plateau droop: render=3.79dB/s vs. real=-33.44dB/s - opposite direction
- Time=4.8 High=0 5657-11314Hz plateau droop: render=4.02dB/s vs. real=-28.64dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz plateau droop: render=-180.94dB/s vs. real=48.59dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz fall rate: render=309.86dB/s vs. real=-129.29dB/s - opposite direction
- Time=7.0 High=-7 44-89Hz plateau droop: render=1.32dB/s vs. real=-17.91dB/s - opposite direction

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0695 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0689 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.038 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0276 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.1012 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0352 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0413 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0537 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0265 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | 23.061 | -313.98 | 11.838 | -12.857 | 6.35 |
| NonLin_0.8s_-3H.wav | 26.009 | -10.74 | -1.598 | -2.925 | 6.23 |
| NonLin_2.2s_0H.wav | -27.868 | -51.43 | -9.302 | -2.925 | 4.2 |
| NonLin_4.8s_0H.wav | -90.635 | -64.65 | 36.626 | -1.837 | 4.27 |
| NonLin_7.0s_-7H.wav | 41.156 | -128.29 | 19.348 | -16.712 | 2.89 |
| NonLin_7.0s_0H.wav | 27.007 | -100.09 | -38.283 | -2.925 | 2.84 |
| NonLin_9.8s_-4H.wav | -51.814 | -20.19 | 8.794 | -17.891 | 2.57 |
| NonLin_9.8s_-9H.wav | -30.68 | -12.37 | 6.22 | -16.712 | 2.25 |
| NonLin_9.8s_0H.wav | 40.975 | -107.76 | -41.977 | -1.928 | 2.83 |