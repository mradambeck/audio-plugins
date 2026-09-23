# Shields validation report

2 reference capture(s) compared against one render of the plugin's current defaults (see Source/PluginProcessor.cpp's createParameterLayout()).

**Standing rule: this table is not the final word on anything.** Check what's actually driving any number before concluding a change helped or hurt - see ml-toolkit/README.md's own "after any synthesis architecture change" section.

## Headline metrics

| Reference | Envelope correlation | LSD (dB) | Decay slope error (dB/s) | Peak time error (s) | Resonant peak height error (dB) |
|---|---|---|---|---|---|
| preset-45.wav | 0.9433 | 4.17 | 0.1443 | -0.155 | -0.1031 |
| preset-49.wav | 0.9421 | 4.29 | 0.3202 | -0.155 | 0.8029 |

## Decay and attack timing detail

| Reference | Decay slope rendered | Decay slope reference | Peak time rendered | Peak time reference | Level@0.5s error | Level@1.0s error |
|---|---|---|---|---|---|---|
| preset-45.wav | -10.14 | -10.29 | 0.17 | 0.325 | 2.61 | 0.32 |
| preset-49.wav | -10.14 | -10.46 | 0.17 | 0.325 | 2.62 | 1.08 |

## Flagged concerns

The following values exceed a threshold picked from the magnitude of a real, currently-open gap (see CONCERN_THRESHOLDS in this script) - worth acting on, not just noting:

- **Peak timing error (s)**: -0.155 exceeds +/-0.1 - the buildup peaks at a measurably different time than the real hardware's own - a positive value means the render's peak arrives LATE relative to the reference.
- **Log-spectral distance (dB, unsigned)**: 4.23 exceeds +/-4.0 - overall tonal balance is audibly off, not just a narrow band - see the per-band table for where.
- **LTAS band error (125-250Hz)**: mean |error| 2.03dB exceeds +/-2.0dB - see the per-band table for the signed direction.
- **LTAS band error (500-1000Hz)**: mean |error| 3.27dB exceeds +/-2.0dB - see the per-band table for the signed direction.
- **LTAS band error (4000-8000Hz)**: mean |error| 3.25dB exceeds +/-2.0dB - see the per-band table for the signed direction.

## Per-band tonal balance (RMS-matched, 1/3-octave-smoothed LTAS, render minus reference)

| Band | preset-45.wav | preset-49.wav |
|---|---|---|
| 20-125Hz | -0.18 | -0.51 |
| 125-250Hz | -1.79 | -2.27 |
| 250-500Hz | -1.23 | -1.39 |
| 500-1000Hz | -3.56 | -2.97 |
| 1000-2000Hz | -1.55 | -1.30 |
| 2000-4000Hz | -1.76 | -1.49 |
| 4000-8000Hz | -2.58 | -3.92 |
| 8000-16000Hz | -0.59 | -2.60 |

## Resonant peak height above own spectral floor

| Reference | Rendered (dB above floor) | Reference (dB above floor) | Error |
|---|---|---|---|
| preset-45.wav | 10.41 | 10.51 | -0.1031 |
| preset-49.wav | 10.41 | 9.61 | 0.8029 |