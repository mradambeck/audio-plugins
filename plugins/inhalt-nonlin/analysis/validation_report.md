# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | 5.556 | 7.823 | 3.742 |
| Fall rate error (dB/s) | -51.187 | -33.81 | -65.088 |
| Plateau droop error (dB/s) | 50.065 | 10.934 | 81.369 |
| Build-up error (ms) | -12.625 | -6.145 | -17.809 |
| Onset NED mean error (0-20ms) | -0.049 | -0.012 | -0.078 |
| Onset NED first-window error | -0.164 | -0.136 | -0.187 |
| Time-to-NED=0.9 error (ms) | -191.565 | -185.578 | -196.354 |
| Mixing time error (ms) | -141.801 | -143.016 | -140.83 |
| Mid/side ratio error (dB) | -0.125 | -0.093 | -0.15 |
| Log-spectral distance (dB, unsigned) | 3.652 | 3.87 | 3.478 |
| Crest factor error (dB) | 0.062 | -0.476 | 0.492 |
| Spectral flatness error (dB) | 4.156 | 2.477 | 5.499 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 4.156 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 2.477 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 5.499 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Per-band plateau droop error (dB/s, mean |error|) (all)**: 85.137 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (all)**: 105.385 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High=0)**: 42.933 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High=0)**: 109.28 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High!=0)**: 118.9 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High!=0)**: 102.269 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- Per-band sign mismatches (all): 12 band/capture/metric combination(s) where the render decays in the OPPOSITE direction from the real hardware - a more severe failure than a magnitude-only miss. See validation_report.md's own 'Per-band sign mismatches' section for the full list.

## Per-band sign mismatches

Every octave band/capture/metric where the render decays in the OPPOSITE direction from the real hardware (see `_band_sign_mismatches`' own docstring) - a more severe failure than the magnitude-only per-band check above, since a sign flip can hide entirely inside a broadband average that still looks reasonable.

- Time=0.1 High=-3 1414-2828Hz plateau droop: render=-0.36dB/s vs. real=186.39dB/s - opposite direction
- Time=0.1 High=-3 2828-5657Hz plateau droop: render=-49.43dB/s vs. real=196.76dB/s - opposite direction
- Time=0.1 High=-3 5657-11314Hz plateau droop: render=-46.90dB/s vs. real=107.26dB/s - opposite direction
- Time=0.8 High=-3 1414-2828Hz plateau droop: render=-26.58dB/s vs. real=186.37dB/s - opposite direction
- Time=0.8 High=-3 2828-5657Hz plateau droop: render=-60.40dB/s vs. real=196.77dB/s - opposite direction
- Time=0.8 High=-3 5657-11314Hz plateau droop: render=-50.94dB/s vs. real=107.25dB/s - opposite direction
- Time=2.2 High=0 707-1414Hz plateau droop: render=-32.93dB/s vs. real=25.29dB/s - opposite direction
- Time=2.2 High=0 11314-22049Hz fall rate: render=324.04dB/s vs. real=-111.72dB/s - opposite direction
- Time=4.8 High=0 1414-2828Hz plateau droop: render=1.55dB/s vs. real=-23.77dB/s - opposite direction
- Time=4.8 High=0 2828-5657Hz plateau droop: render=21.05dB/s vs. real=-33.44dB/s - opposite direction
- Time=4.8 High=0 5657-11314Hz plateau droop: render=21.66dB/s vs. real=-28.64dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz plateau droop: render=-11.89dB/s vs. real=48.59dB/s - opposite direction

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.0429 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0432 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0315 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0306 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0444 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0261 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0344 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0445 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0256 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -0.884 | -240.76 | 164.253 | -14.852 | 4.59 |
| NonLin_0.8s_-3H.wav | 7.052 | -67.79 | 150.757 | -16.893 | 4.51 |
| NonLin_2.2s_0H.wav | 5.057 | -56.24 | 29.59 | -15.896 | 3.64 |
| NonLin_4.8s_0H.wav | -4.83 | -27.93 | 46.718 | -0.839 | 5.77 |
| NonLin_7.0s_-7H.wav | 5.238 | -7.08 | 37.905 | -18.707 | 2.65 |
| NonLin_7.0s_0H.wav | 20.023 | -27.09 | -17.448 | -4.92 | 2.88 |
| NonLin_9.8s_-4H.wav | 4.059 | -8.89 | 29.058 | -19.886 | 2.93 |
| NonLin_9.8s_-9H.wav | 3.243 | -0.92 | 24.874 | -18.707 | 2.71 |
| NonLin_9.8s_0H.wav | 11.043 | -23.98 | -15.126 | -2.925 | 3.19 |