# Inhalt validation report

9 captures compared. Split all / High=0 / High!=0 to expose systematic bias rather than average it away.

**Standing rule: this aggregate table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/effects/ambience's own history of this metric getting worse while the DSP got more correct, twice.

## Aggregate (mean absolute-value-blind signed error, unless noted)

| Metric | All | High=0 | High!=0 |
|---|---|---|---|
| Knee time error (ms) | 4.89 | 6.326 | 3.742 |
| Fall rate error (dB/s) | -51.572 | -34.15 | -65.51 |
| Plateau droop error (dB/s) | 50.932 | 11.756 | 82.272 |
| Build-up error (ms) | -12.403 | -6.145 | -17.41 |
| Onset NED mean error (0-20ms) | -0.049 | -0.012 | -0.078 |
| Onset NED first-window error | -0.164 | -0.136 | -0.187 |
| Time-to-NED=0.9 error (ms) | -192.895 | -185.578 | -198.748 |
| Mixing time error (ms) | -147.344 | -155.488 | -140.83 |
| Mid/side ratio error (dB) | -0.207 | -0.118 | -0.279 |
| Log-spectral distance (dB, unsigned) | 3.72 | 3.987 | 3.506 |
| Crest factor error (dB) | 0.081 | -0.447 | 0.504 |
| Spectral flatness error (dB) | 4.147 | 2.473 | 5.487 |

## Flagged concerns

The following aggregate values exceed a threshold picked from the magnitude of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - worth listening to, not just noting:

- **Spectral flatness error (dB) (all)**: 4.147 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High=0)**: 2.473 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Spectral flatness error (dB) (High!=0)**: 5.487 exceeds +/-1.5 - the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than the real hardware - the qualitative 'openness vs. grit' complaint.
- **Per-band plateau droop error (dB/s, mean |error|) (all)**: 84.997 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (all)**: 103.613 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High=0)**: 43.047 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High=0)**: 107.498 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- **Per-band plateau droop error (dB/s, mean |error|) (High!=0)**: 118.557 exceeds 25.0 - at least one octave band's mid-plateau decay is measurably off from the real hardware's own - check the per-band table for which band(s), and whether any show a SIGN mismatch below (a far more severe failure than a magnitude-only miss).
- **Per-band fall rate error (dB/s, mean |error|) (High!=0)**: 100.504 exceeds 25.0 - at least one octave band's post-knee fall rate is measurably off from the real hardware's own - check the per-band table for which band(s).
- Per-band sign mismatches (all): 12 band/capture/metric combination(s) where the render decays in the OPPOSITE direction from the real hardware - a more severe failure than a magnitude-only miss. See validation_report.md's own 'Per-band sign mismatches' section for the full list.

## Per-band sign mismatches

Every octave band/capture/metric where the render decays in the OPPOSITE direction from the real hardware (see `_band_sign_mismatches`' own docstring) - a more severe failure than the magnitude-only per-band check above, since a sign flip can hide entirely inside a broadband average that still looks reasonable.

- Time=0.1 High=-3 1414-2828Hz plateau droop: render=-0.34dB/s vs. real=186.39dB/s - opposite direction
- Time=0.1 High=-3 2828-5657Hz plateau droop: render=-49.43dB/s vs. real=196.76dB/s - opposite direction
- Time=0.1 High=-3 5657-11314Hz plateau droop: render=-46.89dB/s vs. real=107.26dB/s - opposite direction
- Time=0.8 High=-3 1414-2828Hz plateau droop: render=-26.58dB/s vs. real=186.37dB/s - opposite direction
- Time=0.8 High=-3 2828-5657Hz plateau droop: render=-60.40dB/s vs. real=196.77dB/s - opposite direction
- Time=0.8 High=-3 5657-11314Hz plateau droop: render=-50.93dB/s vs. real=107.25dB/s - opposite direction
- Time=2.2 High=0 707-1414Hz plateau droop: render=-32.92dB/s vs. real=25.29dB/s - opposite direction
- Time=2.2 High=0 11314-22049Hz fall rate: render=332.94dB/s vs. real=-111.72dB/s - opposite direction
- Time=4.8 High=0 1414-2828Hz plateau droop: render=1.53dB/s vs. real=-23.77dB/s - opposite direction
- Time=4.8 High=0 2828-5657Hz plateau droop: render=21.04dB/s vs. real=-33.44dB/s - opposite direction
- Time=4.8 High=0 5657-11314Hz plateau droop: render=21.65dB/s vs. real=-28.64dB/s - opposite direction
- Time=4.8 High=0 11314-22049Hz plateau droop: render=-11.89dB/s vs. real=48.59dB/s - opposite direction

## Stereo (IACC, rendered vs. reference, per capture)

| File | IACC rendered | IACC reference | Coherence floor (r/ref) |
|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.044 | 0.0057 | 0.3466 / 0.3466 |
| NonLin_0.8s_-3H.wav | 0.0441 | 0.0056 | 0.3797 / 0.3797 |
| NonLin_2.2s_0H.wav | 0.0313 | 0.0212 | 0.3492 / 0.3492 |
| NonLin_4.8s_0H.wav | 0.0305 | 0.0394 | 0.3255 / 0.3255 |
| NonLin_7.0s_-7H.wav | 0.0833 | 0.0115 | 0.2543 / 0.2543 |
| NonLin_7.0s_0H.wav | 0.0339 | 0.0367 | 0.2375 / 0.2375 |
| NonLin_9.8s_-4H.wav | 0.0373 | 0.0158 | 0.2182 / 0.2182 |
| NonLin_9.8s_-9H.wav | 0.0493 | 0.0089 | 0.2375 / 0.2375 |
| NonLin_9.8s_0H.wav | 0.0266 | 0.0356 | 0.2298 / 0.2298 |

## Per-capture gate errors

| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |
|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | -0.884 | -240.53 | 164.039 | -14.852 | 4.51 |
| NonLin_0.8s_-3H.wav | 7.052 | -67.73 | 150.6 | -16.893 | 4.43 |
| NonLin_2.2s_0H.wav | 5.057 | -56.25 | 29.573 | -15.896 | 3.67 |
| NonLin_4.8s_0H.wav | -4.83 | -27.94 | 46.702 | -0.839 | 5.88 |
| NonLin_7.0s_-7H.wav | 5.238 | -9.05 | 42.512 | -16.712 | 3.01 |
| NonLin_7.0s_0H.wav | 14.036 | -28.27 | -14.195 | -4.92 | 3.13 |
| NonLin_9.8s_-4H.wav | 4.059 | -8.99 | 29.153 | -19.886 | 2.9 |
| NonLin_9.8s_-9H.wav | 3.243 | -1.25 | 25.058 | -18.707 | 2.68 |
| NonLin_9.8s_0H.wav | 11.043 | -24.14 | -15.055 | -2.925 | 3.27 |