# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | 11.35 | 20.794 | 3.796 |
| Fall rate error (dB/s) | -119.417 | -116.297 | -121.912 |
| Plateau droop error (dB/s) | -5.155 | -48.502 | 29.523 |
| Build-up error (ms) | -7.939 | -3.9 | -11.17 |
| Onset NED mean error (0-20ms) | -0.022 | -0.002 | -0.037 |
| Onset NED first-window error | -0.13 | -0.13 | -0.13 |
| Time-to-NED=0.9 error (ms) | -194.225 | -179.592 | -205.932 |
| Mixing time error (ms) | -152.031 | -161.723 | -144.277 |
| Mid/side ratio error (dB) | -1.655 | -1.669 | -1.644 |
| Log-spectral distance (dB, unsigned) | 3.809 | 3.202 | 4.294 |
| Crest factor error (dB) | -0.676 | -0.368 | -0.922 |
| Spectral flatness error (dB) | 3.45 | 2.174 | 4.47 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.45 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 2.174 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 4.47 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Log-spectral distance (dB, unsigned) (High!=0)**: 4.294 exceeds +/-4.0 - overall tonal balance is audibly off, not just a narrow band - see the per-band table for where.
- **Per-band plateau droop error (dB/s, mean |error|) (all)**: 81.216 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (all)**: 83.118 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High=0)**: 64.456 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High=0)**: 75.591 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High!=0)**: 94.625 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High!=0)**: 89.139 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- Per-band sign mismatches (all): 12 band/capture/metric combination(s) where the render decays in the OPPOSITE direction from the real hardware - a more severe failure than a magnitude-only miss. See validation_report.md's own 'Per-band sign mismatches' section for the full list.

## Per-band sign mismatches

Every octave band/capture/metric where the render decays in the OPPOSITE direction from the real hardware (see `_band_sign_mismatches`' own docstring) - a more severe failure than the magnitude-only per-band check above, since a sign flip can hide entirely inside a broadband average that still looks reasonable.

- Time=0.1 High=-3 1414-2828Hz plateau droop: render=-172.27dB/s vs. real=186.39dB/s - opposite direction
- Time=0.1 High=-3 2828-5657Hz plateau droop: render=-167.55dB/s vs. real=196.76dB/s - opposite direction
- Time=0.1 High=-3 5657-11314Hz plateau droop: render=-63.58dB/s vs. real=107.26dB/s - opposite direction
- Time=0.8 High=-3 1414-2828Hz plateau droop: render=-170.06dB/s vs. real=186.37dB/s - opposite direction
- Time=0.8 High=-3 2828-5657Hz plateau droop: render=-301.22dB/s vs. real=196.77dB/s - opposite direction
- Time=0.8 High=-3 5657-11314Hz plateau droop: render=-100.25dB/s vs. real=107.25dB/s - opposite direction
- Time=2.2 High=0 44-89Hz plateau droop: render=50.77dB/s vs. real=-206.98dB/s - opposite direction
- Time=2.2 High=0 177-354Hz plateau droop: render=40.06dB/s vs. real=-166.86dB/s - opposite direction
- Time=2.2 High=0 11314-22049Hz fall rate: render=297.87dB/s vs. real=-111.72dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz plateau droop: render=-176.90dB/s vs. real=48.59dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz fall rate: render=297.73dB/s vs. real=-129.29dB/s - opposite direction
- Time=7.0 High=-7 44-89Hz plateau droop: render=0.31dB/s vs. real=-17.91dB/s - opposite direction

## Stereo sign mismatches

Every capture where the render's stereo correlation at the reference's own dominant lag is the OPPOSITE SIGN from the real hardware's (see `_stereo_sign_mismatches`' own docstring) - narrow vs. wide, not just off in magnitude.

None.

## Stereo (IACC and lag-correlation, rendered vs. reference, per capture)

`Lag corr.` is evaluated at the reference's own dominant lag (`Lag (ms)`) on both signals - see `core.features.dominant_lag_correlation`'s own docstring for why this catches a real correlation well outside IACC's own +-1ms window.

| File | IACC rendered | IACC reference | Coherence floor (r/ref) | Lag (ms) | Lag corr. rendered | Lag corr. reference |
|---|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.1669 | 0.0057 | 0.3466 / 0.3466 | 2.49 | 0.9763 | 0.9781 |
| NonLin_0.8s_-3H.wav | 0.166 | 0.0056 | 0.3797 / 0.3797 | 2.49 | 0.9765 | 0.9784 |
| NonLin_2.2s_0H.wav | 0.2611 | 0.0212 | 0.3492 / 0.3492 | 2.49 | -0.4648 | -0.4614 |
| NonLin_4.8s_0H.wav | 0.1945 | 0.0394 | 0.3255 / 0.3255 | 2.49 | -0.2441 | -0.238 |
| NonLin_7.0s_-7H.wav | 0.1912 | 0.0115 | 0.2543 / 0.2543 | 2.49 | -0.1399 | -0.1914 |
| NonLin_7.0s_0H.wav | 0.1654 | 0.0367 | 0.2375 / 0.2375 | 2.49 | -0.1869 | -0.1855 |
| NonLin_9.8s_-4H.wav | 0.1664 | 0.0158 | 0.2182 / 0.2182 | 2.49 | -0.1747 | -0.1837 |
| NonLin_9.8s_-9H.wav | 0.1778 | 0.0089 | 0.2375 / 0.2375 | 2.49 | -0.1607 | -0.1807 |
| NonLin_9.8s_0H.wav | 0.1552 | 0.0356 | 0.2298 / 0.2298 | 2.49 | -0.1887 | -0.1778 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | 24.286 | -354.41 | 73.766 | -1.655 | 7.55 |
| NonLin_0.8s_-3H.wav | 27.233 | -31.05 | 56.262 | -3.696 | 7.25 |
| NonLin_2.2s_0H.wav | -4.92 | -52.85 | -65.751 | -2.925 | 4.46 |
| NonLin_4.8s_0H.wav | -15.805 | -93.7 | -33.659 | -4.83 | 3.4 |
| NonLin_7.0s_-7H.wav | 20.022 | -197.48 | 12.195 | -15.895 | 2.37 |
| NonLin_7.0s_0H.wav | 44.966 | -159.63 | -47.289 | -4.92 | 2.45 |
| NonLin_9.8s_-4H.wav | -25.873 | -20.21 | 0.166 | -17.891 | 2.36 |
| NonLin_9.8s_-9H.wav | -26.689 | -6.41 | 5.228 | -16.712 | 1.94 |
| NonLin_9.8s_0H.wav | 58.934 | -159.01 | -47.309 | -2.925 | 2.5 |