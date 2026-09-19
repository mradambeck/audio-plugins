# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | -5.057 | -12.132 | 0.603 |
| Fall rate error (dB/s) | -89.799 | -81.263 | -96.628 |
| Plateau droop error (dB/s) | -3.928 | -12.186 | 2.679 |
| Build-up error (ms) | -7.274 | -2.404 | -11.17 |
| Onset NED mean error (0-20ms) | -0.022 | -0.003 | -0.037 |
| Onset NED first-window error | -0.129 | -0.129 | -0.13 |
| Time-to-NED=0.9 error (ms) | -188.904 | -166.122 | -207.129 |
| Mixing time error (ms) | -144.271 | -145.51 | -143.279 |
| Mid/side ratio error (dB) | -1.627 | -1.6 | -1.649 |
| Log-spectral distance (dB, unsigned) | 3.963 | 3.678 | 4.192 |
| Crest factor error (dB) | -0.343 | -0.654 | -0.094 |
| Spectral flatness error (dB) | 3.65 | 2.323 | 4.712 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 3.65 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 2.323 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 4.712 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Log-spectral distance (dB, unsigned) (High!=0)**: 4.192 exceeds +/-4.0 - overall tonal balance is audibly off, not just a narrow band - see the per-band table for where.
- **Per-band plateau droop error (dB/s, mean |error|) (all)**: 83.303 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (all)**: 97.335 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High=0)**: 54.151 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High=0)**: 89.306 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High!=0)**: 106.625 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High!=0)**: 103.757 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- Per-band sign mismatches (all): 14 band/capture/metric combination(s) where the render decays in the OPPOSITE direction from the real hardware - a more severe failure than a magnitude-only miss. See validation_report.md's own 'Per-band sign mismatches' section for the full list.

## Per-band sign mismatches

Every octave band/capture/metric where the render decays in the OPPOSITE direction from the real hardware (see `_band_sign_mismatches`' own docstring) - a more severe failure than the magnitude-only per-band check above, since a sign flip can hide entirely inside a broadband average that still looks reasonable.

- Time=0.1 High=-3 1414-2828Hz plateau droop: render=-181.05dB/s vs. real=186.39dB/s - opposite direction
- Time=0.1 High=-3 2828-5657Hz plateau droop: render=-225.18dB/s vs. real=196.76dB/s - opposite direction
- Time=0.1 High=-3 5657-11314Hz plateau droop: render=-210.16dB/s vs. real=107.26dB/s - opposite direction
- Time=0.8 High=-3 1414-2828Hz plateau droop: render=-113.08dB/s vs. real=186.37dB/s - opposite direction
- Time=0.8 High=-3 2828-5657Hz plateau droop: render=-314.06dB/s vs. real=196.77dB/s - opposite direction
- Time=0.8 High=-3 5657-11314Hz plateau droop: render=-167.12dB/s vs. real=107.25dB/s - opposite direction
- Time=0.8 High=-3 11314-22049Hz fall rate: render=434.40dB/s vs. real=-91.93dB/s - opposite direction
- Time=2.2 High=0 44-89Hz plateau droop: render=50.53dB/s vs. real=-206.98dB/s - opposite direction
- Time=2.2 High=0 707-1414Hz plateau droop: render=-139.11dB/s vs. real=25.29dB/s - opposite direction
- Time=2.2 High=0 11314-22049Hz fall rate: render=318.92dB/s vs. real=-111.72dB/s - opposite direction
- Time=4.8 High=0 5657-11314Hz plateau droop: render=4.84dB/s vs. real=-28.64dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz plateau droop: render=-187.43dB/s vs. real=48.59dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz fall rate: render=290.75dB/s vs. real=-129.29dB/s - opposite direction
- Time=7.0 High=-7 44-89Hz plateau droop: render=0.33dB/s vs. real=-17.91dB/s - opposite direction

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.1786 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.174 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.2588 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.1816 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.1861 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.1592 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.1607 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.1741 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.1495 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | 23.288 | -309.1 | -17.314 | -1.655 | 6.76 |
| NonLin_0.8s_-3H.wav | 25.238 | -11.19 | -1.794 | -3.696 | 6.71 |
| NonLin_2.2s_0H.wav | -26.871 | -52.13 | -7.294 | -2.925 | 4.53 |
| NonLin_4.8s_0H.wav | -90.635 | -64.55 | 38.841 | -1.837 | 4.52 |
| NonLin_7.0s_-7H.wav | 41.973 | -128.62 | 15.709 | -15.895 | 2.51 |
| NonLin_7.0s_0H.wav | 28.005 | -99.93 | -38.778 | -2.925 | 2.74 |
| NonLin_9.8s_-4H.wav | -56.802 | -20.8 | 10.121 | -17.891 | 2.65 |
| NonLin_9.8s_-9H.wav | -30.68 | -13.43 | 6.675 | -16.712 | 2.33 |
| NonLin_9.8s_0H.wav | 40.975 | -108.44 | -41.514 | -1.928 | 2.92 |