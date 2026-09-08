# Concrete — vintage sampler emulation: build plan

A JUCE **instrument** plugin (AU/VST3/Standalone) for the Wild Jag catalog that plays back
user-loaded samples through the playback architectures of classic 1980s hardware samplers. Not an
insert effect. The pitch-shifting mechanism, quantization scheme, and analog filter behavior are
the emulation; each machine is a preset that configures them.

This is the ecosystem-tailored version of the original research plan: the DSP research and the
machine table below are unchanged in substance, but every deliverable is now expressed in this
repo's conventions (see [`AGENTS.md`](../../AGENTS.md) and `.claude/skills/`), and three
architectural decisions that the original left implicit — multi-zone forward compatibility, capture
pass vs. filter placement, and the trigger UI — are now decided up front.

---

## Identity

| Field | Value |
|---|---|
| Product name | **Concrete** (instruments in this catalog don't take a `" - Type"` suffix — cf. `Strike`, `Alloy`; effects do) |
| Folder | `plugins/concrete-sampler/` |
| `BUNDLE_ID` | `com.wildjag.concrete` |
| `PLUGIN_CODE` | `Cnct` (unique against `Cavn Dmge Corr Flux Aloy Grad Shld Intr Strk Aura`) |
| `PLUGIN_MANUFACTURER_CODE` | `WJag` |
| Formats | `AU VST3 Standalone`, `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE` |
| Starting version | `project(Concrete VERSION 0.1.0)` + `set(WILDJAG_RELEASE_CHANNEL "beta")` |
| Accent pair | **decide with Adam during the Phase 8 mockup.** Taken hues: teal-green (Caverns), red (Damage), olive (Corrosion), violet (Flux), steel blue (Alloy), terracotta (Gradient), magenta (Shields), yellow-green (Intruder), forest green (Strike), gold (Aura). Recommendation: a **signal blue** (`#5a72b0` muted / `#7fa5f5` bright) — the only clearly unoccupied region. |
| License | AGPLv3, like the rest of the repo |

---

## Ground rules for Claude Code

**Do not use `juce::Synthesiser`, `juce::SamplerVoice`, or `juce::SamplerSound`.** The sampler
classes do clean linear interpolation internally, which is exactly the behavior this plugin
exists to avoid. `juce::Synthesiser` is also *not* this catalog's convention — no plugin here uses
it (`alloy-bass/Source/PluginProcessor.h:282` says so explicitly, and Strike hand-rolls
[`StrikeVoiceAllocator.h`](../strike-synth/Source/StrikeVoiceAllocator.h)). Follow Strike: a
framework-free, fixed-size, allocation-free `ConcreteVoiceAllocator<MaxVoices>` template with its
own unit tests, plus a plain `ConcreteVoice` class. Phase 6's deterministic voice-stealing
requirement is far easier to test that way than through `juce::Synthesiser`'s internals.

**Split the DSP into standalone, framework-free classes.** Per `AGENTS.md`'s testing convention,
this plugin's logic is complex enough to warrant Gradient/Strike's decomposition rather than
testing `PluginProcessor.cpp` directly. Target seams:

```
Source/ConcreteSampleZone.h      # one zone: buffer ref, root, key/vel range, start/end/loop, tune/level/pan
Source/ConcreteSampleSet.h       # the zone list + lookup(note, velocity) -> zone index; the multi-zone seam
Source/ConcretePitchEngine.h     # modes A (variable-clock ZOH), B (fixed-rate drop-sample), C (delta-sigma)
Source/ConcreteQuantizer.h       # linear bit reduction + compand/expand curve
Source/ConcreteCapturePass.h     # offline resample -> drive -> quantize (-> optional filter, Phase 5)
Source/ConcreteFilterModels.h    # SSM, CEM-loss, CEM-compensated, digital+VCA, one-pole, bypass
Source/ConcreteVoice.h           # composes pitch engine + filter + amp env for one note
Source/ConcreteVoiceAllocator.h  # allocation/stealing/choke policy only, no audio (cf. Strike)
Source/ConcreteMachines.h        # the twelve machine definitions as data
```

**Verify audio empirically, don't assert it.** Every phase below has an analysis step. Render
offline to WAV and measure it numerically. Report actual measured numbers against expected values.
"It compiles and sounds fine" is not verification, and neither is reasoning about why the code
should be right — render, measure, compare.

**Use the existing offline-render convention, don't invent a new one.** This catalog already has
a `<Plugin>RenderIR` pattern (`Source/Tools/RenderIR.cpp` → a `juce_add_console_app` target) in
seven plugins. [`AlloyRenderIR`](../alloy-bass/Source/Tools/RenderIR.cpp) is the closest model:
it's MIDI-driven rather than audio-in-driven, takes `--out/--seconds/--sampleRate/--preset` plus
`--<paramID> <rawValue>` flags mapping 1:1 onto APVTS parameter IDs, and reuses
`Source/Tests/TestCreateEditorStub.cpp` so it links without the editor/LookAndFeel/BinaryData.
`ConcreteRenderIR` is that, plus sampler-specific flags (`--sample <path.wav>`, `--note`,
`--velocity`, `--sequence`). Follow the existing per-plugin copy — extracting the shared
boilerplate into a util is deliberately deferred (see the last section).

**Python analysis goes in `plugins/concrete-sampler/analysis/`**, matching
`aura-reverb/analysis/` and `intruder-gated-reverb/analysis/` (a `validate.py`, a
`validation_report.md`, `validation_results.json`, `validation_plots/`, a local `venv`). Reuse
[`plugins/common/tools/compare_wavs.py`](../common/tools/compare_wavs.py) for null tests, envelope
correlation, and log-spectral distance rather than rewriting FFT plumbing; only add genuinely new
primitives locally (image/alias energy above a given frequency, THD, quantization noise floor,
spectral centroid). Promote something to `common/tools/` only if a second plugin needs it —
`AGENTS.md`'s "build only what the current effect needs" applies here.

**Two test targets, both registered in CI.** Follow Strike's split exactly: `ConcreteTests`
(the framework-free DSP seams — fast, no JUCE plugin machinery) and `ConcreteProcessorTests`
(drives the real `ConcreteAudioProcessor`: APVTS, state, MIDI dispatch, `processBlock`). Both are
`juce::UnitTestRunner` console apps, exit 0 = pass, not CTest-integrated. Keep `createEditor()` in
`PluginEditor.cpp` so `PluginProcessor.cpp` has no GUI dependency.

**Real-time safety is an acceptance criterion, not an afterthought.** No allocation, locking, or
file I/O on the audio thread — sample loading and capture-pass re-bakes happen off it and swap in
via a reference-counted pointer (see *Architecture decisions* below). Every phase's analysis step
includes a CPU cost measurement; Mode C (delta-sigma at 64× oversampling, per voice) is the one
most likely to blow the budget, so measure it the moment it exists rather than at the end.

**Building installs.** `COPY_PLUGIN_AFTER_BUILD TRUE` means building `Concrete_All` (or the AU/VST3
targets) silently replaces whatever is installed in `~/Library/Audio/Plug-Ins/`. Build the test and
RenderIR targets freely; build the plugin targets only when Adam has asked for a build, and match
the config he's using.

**Stop at each checkpoint.** `STANDALONE CHECK` items are Adam's hand-tests. Build the standalone,
state what to load, what to play, and what he should hear, then wait for confirmation.

**Skills to use, not re-derive:** `juce-hardware-panel-ui` (Phase 8 — including its mockup-first
process and its pixel-diff verification methodology), `wildjag-plugin-installer` (Phase 9),
`wildjag-plugin-version-bump` (proactively, on every commit touching `Source/` or `CMakeLists.txt`).

---

## Architecture decisions made up front

These four are cheap now and expensive later. Decide them in Phase 0/1, not when they bite.

### 1. There are no modes — there's a zone list

v1 loads one sample. "Repitch" and "kit" are **not two modes of the engine**; they're two shapes of
the same zone list, and the DSP path can't tell them apart:

| Zones | Key ranges | What that is |
|---|---|---|
| 1 | 0–127 | one sample played chromatically — **this is v1** |
| 16 | one note each | a kit |
| 8 | spread across the keyboard | a multisampled instrument |

That third row is why a mode flag is the wrong shape. A `repitch`/`kit` toggle would make the zone
list mean something different depending on a flag, and multisampling — which costs nothing here —
would then need a third mode. One list expresses all three. Everything below follows from that:

- `ConcreteSampleSet` owns `std::vector<ConcreteSampleZone>` from day one. Note-on always goes
  through `lookup(note, velocity) -> int zoneIndex`, never at a hardcoded buffer. In v1 the vector
  has exactly one element and the lookup is trivially satisfied — but it's a real lookup.
- **State schema stores zones as a list from day one**: a `<ZONES>` child of the APVTS state tree
  holding `<ZONE index= path= root= keyLo= keyHi= velLo= velHi= start= end= loopStart= loopEnd=
  loopEnabled= reverse= tune= level= pan= chokeGroup= output=>` elements. v1 writes one element; a
  kit writes sixteen; a multisampled instrument writes eight. Presets saved in v1 keep loading
  unchanged, because nothing about the schema changes — only how many children it has.
- **Mapping is a UI affordance, not a mode.** The one genuine either/or is where a dropped sample
  *lands* when a zone is already loaded: replace the current zone (the repitch workflow) or take
  the next free pad (the kit workflow). That's a single UI preference governing assignment — no
  separate state, parameters, or signal path — and it needn't exist in v1 at all, since with one
  zone there's nothing to disambiguate.
- **Let the machine set that default.** The twelve machines split cleanly on this axis: the SP-1200,
  MPC60 and Linn 9000 are pad machines; the Emulator II, Emax, Fairlight, Synclavier, K250, Mirage,
  EPS, ASR-10 and SK-1 are keyboards. Selecting a machine can set the drop-mapping default to match
  (drop onto the MPC60 → next pad; drop onto the Emulator II → across the keyboard). It costs
  almost nothing given the zone list, and it stays a default the user can override, never a
  constraint. Not needed in v1; worth designing the affordance so it can arrive without rework.
- **Per-zone values live in that ValueTree, not in APVTS parameters.** APVTS's parameter list is
  fixed at construction, so per-zone tune/level/pan can't be added dynamically later. The choice
  is: reserve 16 zones × 3 params of dead automation slots in v1, or keep per-zone values as
  non-automatable state. **DECIDED: non-automatable state**, plus one automatable "focused zone"
  set (tune/level/pan) acting on whichever pad or key range is selected. If per-zone automation is
  wanted later, adding those parameters is a minor-version bump under the versioning rubric —
  additive, no rename, nothing breaks.
- `ConcreteVoiceAllocator` takes `zoneIndex` alongside `midiNote` in `allocateVoiceForNoteOn()`,
  and carries a **choke group** field per voice from day one (0 = no choke). v1 never sets a
  non-zero group; a kit's closed-hat-cuts-open-hat behavior then costs one branch, not an API
  change. Add the choke tests in Phase 6 even though nothing uses the feature yet.
- **Output bus layout: ship main stereo only, but architect for aux outs. DECIDED.** JUCE bus
  layouts are declared at construction, so this had to be settled before Phase 0. There is **no
  measurable CPU cost** either way — a disabled bus allocates no channels and never reaches
  `processBlock`, and an enabled-but-unused one costs a per-block `clear()` on channels nobody
  writes. The real costs of declaring aux buses up front are elsewhere:
  - `isBusesLayoutSupported` gets substantially more complex, and getting it wrong shows up as
    hosts silently refusing layouts or as `auval` failures. No other plugin in this catalog is
    multi-bus, so it's all new testing surface.
  - Logic lists a multi-output AU as a **separate instrument variant** in its plugin menu. Ship
    aux buses in v1 and users see a "Multi-Output" Concrete that routes nothing anywhere.

  Adding them later is not as breaking as it first looks: for AU the stereo variant remains, so
  existing sessions keep loading; for VST3 the bus arrangement is part of what the host persists,
  so routing may reset on upgrade. That's an acceptable one-time cost in a pre-1.0 plugin, and it
  buys a simpler, testable v1.

  **What "architect for it" means concretely** — these are v1 requirements, not later work:
  - Zones already carry an `output=` field in the state schema (see above). v1 writes 0 to it.
  - **Voices render to a destination index, never straight into the main buffer.** A small
    `ConcreteBusRouter` maps destination → output bus and, in v1, collapses every destination onto
    bus 0. Adding aux outs then changes that mapping and the constructor's bus declaration, not the
    voice code.
  - Per-zone level and pan are applied **before** the routing step, so a zone sounds identical
    whether it's summed into the main mix or sent to its own output.
  - `isBusesLayoutSupported` is written so additional buses are an additive branch rather than a
    rewrite of the condition.
  - **Test the indirection is transparent**: with every zone on destination 0, output must null
    against a build that renders voices directly into the main buffer. If the router colours the
    sound at all, it's wrong.

  What genuinely remains for later, and can't be pre-built: the constructor's bus declaration, the
  `isBusesLayoutSupported` branch, the `auval`/host matrix for the new layouts, and the
  Logic Multi-Output variant appearing. That's the irreducible part — everything upstream of it is
  in place from v1.
- Mono/stereo: `isBusesLayoutSupported` accepts mono **or** stereo main output (this catalog
  fixed exactly this in commit `6230d6d`), and playback must handle a mono *or* stereo source
  sample under either output layout. That's four combinations, all of which get a test.

### 2. Where the loaded sample lives, and how it gets to the audio thread

- The **source buffer** (exactly as loaded from disk) is never modified. The **working buffer**
  (post-capture-pass) is derived from it and regenerated whenever capture transpose, drive, bit
  depth, companding, or iteration count changes.
- The working buffer is **never persisted** — it's regenerated deterministically from source +
  parameters on state load. That keeps state small and makes re-bake the single code path.
- Re-bakes run on a background thread (long samples × up to 4 capture iterations is not a
  message-thread operation), publish via a `juce::ReferenceCountedObjectPtr<const SampleSet>`
  atomic swap, and voices hold a ref to the set they started on so an in-flight note finishes on
  the buffer it began with instead of glitching mid-note. The UI shows a bake indicator.
- **Session persistence of the source audio.** When the host saves a session (or the user saves a
  preset), the plugin writes its state — and the question is whether the *audio itself* goes in
  there or only a path to it on disk:
  - **Path only** — session files stay tiny, but the session breaks the moment the sample is
    moved, renamed, deleted, or opened on another machine. This is the classic "missing sample"
    dialog.
  - **Embed the audio** — sessions are self-contained and portable, at real size cost. Thirty
    seconds of 44.1k stereo is roughly 5MB raw, about half that as FLAC; a sixteen-zone kit of
    those is ~40MB written into *every* session save and *every* preset.
  - **Hybrid — DECIDED, and the default.** Always store the path, and additionally embed
    FLAC-compressed audio when the source is under a size cap, falling back to path-only above it.
    Self-contained for the drum one-shots this plugin is actually for, path-referenced for someone
    who loads a ten-minute field recording. Cap: **20MB per zone / 100MB per instance**,
    post-compression.

  **Plus a user override: "Embed samples in session".** A per-instance toggle that forces embedding
  regardless of the cap, for someone who knows they're handing the session to a collaborator or
  archiving it. Design notes:
  - It lives in the state tree next to the zones, **not** as an APVTS parameter — it's a
    preference, not something anyone automates. It persists with the session, so a project saved
    with it on reopens with it on.
  - **Surface the tradeoff rather than burying it.** The control shows the resulting payload size
    live (e.g. "Embed samples in session — 62MB"), so the cost is visible at the moment of the
    decision instead of arriving later as a slow-saving project. That's the whole point of making
    it a choice.
  - Three effective states, which the UI should make legible: *auto* (default — embedded because
    under the cap, or path-only because over it), *forced on*, and the resulting per-zone status.
    A zone's row shows which it is, so "why is this session 400MB" is always answerable.
  - Because path-only is now a state a user can deliberately land in, **the missing-file path needs
    real handling**: on load, a zone whose path no longer resolves shows a missing state on its pad
    with a relocate action, rather than silently loading empty. Everything else about the instance
    still loads.
- Reported latency stays 0. The capture pass is offline; nothing in the playback path adds delay.

### 3. The capture pass does not touch the filters

The capture pass is `resample → input drive/saturation → quantization`, and it deliberately does
**not** run through any filter model. On the real machines the filter chip sat on the playback
side, one per voice card, so audio only hit it coming *out*, not going *in*. This ordering is the
whole reason Phase 4 sits before Phase 5.

Phase 5 originally shipped a **"double smear"** option here as a deliberate exception — an
explicitly non-authentic toggle that baked an extra filter pass into the *end* of the capture
chain, modeling "audio that already came out of a playback path is being re-recorded." It was
removed after Phase 6: with Capture Iterations already covering compounding degradation, a second
independent "make it worse again" control didn't earn its keep. The rule is back to having no
exception — the capture pass never touches filters, full stop.

### 4. UI comes last, once — not twice

Phase 1's UI is a deliberately unstyled utility panel (load a file, see a waveform, hit notes),
built only so the standalone checks are possible. The real hardware-panel UI is Phase 8, and it
follows `juce-hardware-panel-ui`'s mockup-first process: HTML/CSS mockup → iterate with Adam →
only then C++ → pixel-diff verification. Don't build panel chrome during the DSP phases and then
throw it away.

---

## Phase 0: Scaffold and offline harness

Deliverables:

- `plugins/concrete-sampler/CMakeLists.txt` as an independent CMake project (no super-build),
  including `../common/cmake/FetchJUCE.cmake` and `../common/cmake/AddHardwarePanel.cmake`,
  linking `HardwarePanelLookAndFeel`, with `juce_add_binary_data` referencing
  `../common/Assets/WildJagLogo.svg` + `Oxanium-Bold.ttf` + `Oswald-SemiBold.ttf` (shared assets
  are never copied into a plugin's own folder). Identity fields per the table above.
  `juce_audio_formats` is needed here that most plugins don't link — this is the first plugin in
  the catalog that reads audio files.
- `ConcreteTests` + `ConcreteProcessorTests` console-app targets, wired the way Strike's are,
  each with `Source/Tests/TestRunner.cpp` and `ConcreteProcessorTests` using
  `TestCreateEditorStub.cpp`.
- `ConcreteRenderIR` console app from `Source/Tools/RenderIR.cpp`, MIDI-driven per `AlloyRenderIR`,
  plus `--sample`, `--note`, `--velocity`, `--sequence`.
- `analysis/` with `venv`, `requirements.txt` (numpy/scipy/matplotlib, as
  `common/tools/requirements.txt` already pins), and a `make_test_assets.py` generating
  `test-assets/`: 1kHz sine, 100Hz sine, white-noise burst, impulse, a bright synthesized
  transient, and a sine sweep. Generate rather than commit binaries; gitignore `test-assets/`.
- The **bus layout decision from Architecture §1** baked into the constructor.

Verification: render silence and confirm a valid WAV of the expected length/rate. Render a 1kHz
sine through a passthrough voice and confirm the analysis module reports a single partial at 1kHz
with no significant harmonics. Confirm `ConcreteTests` and `ConcreteProcessorTests` both build and
exit 0 with a trivial test.

No standalone check — nothing to hear.

---

## Phase 1: Sample zones and a clean playback baseline

The reference against which every emulation is measured, so it needs to be transparent.

Deliverables:

- `ConcreteSampleZone` / `ConcreteSampleSet` per Architecture §1, including the list-based state
  schema and the reference-counted publish/swap.
- `ConcreteVoice` with a per-voice phase accumulator and high-quality interpolation. This mode is
  a reference only; no machine preset uses it.
- WAV/AIFF loading via `juce::AudioFormatManager`, root-note assignment, basic ADSR amp envelope.
- Source-audio persistence per Architecture §2: path + capped FLAC embedding, the
  "Embed samples in session" override, and missing-file relocate handling. All three get tests —
  save/reload under the cap, over the cap, and with the source file deliberately moved.
- A utility standalone UI: load a file, draw the waveform, play from MIDI or a temporary on-screen
  keyboard. Explicitly unstyled — Phase 8 replaces it.

Analysis:
- 1kHz sine at root: single partial at 1kHz, THD < 0.1%, no energy above 2kHz beyond the noise floor.
- Same sample one octave up and one down: partials at exactly 2kHz and 500Hz, still no significant
  harmonic or image content. Any grit here is a bug in the reference path.
- Sine sweep tracks input with no discontinuities.
- Mono source / stereo source × mono out / stereo out: all four render correctly, no channel
  duplication or dropped channel.
- Save state, reload state, render again: bit-identical to the pre-save render.

**STANDALONE CHECK 1.** Load a drum loop or a sustained sample. Play across two octaves. It should
sound like a normal, clean, boring modern sampler. No grit, no aliasing, no character. That's the
point: this is the control condition.

---

## Phase 2: Pitch engine modes

Three modes, each replacing Phase 1's interpolation step. All three live in
`ConcretePitchEngine.h` behind one interface so `ConcreteVoice` doesn't branch on machine type.

**Mode A — variable-clock zero-order hold.** Each voice's phase accumulator step scales with
transposition; instead of interpolating between sample points, hold the last value until the next
is due (nearest-neighbor). No anti-imaging filter. This is what the Akai S900/S950/MPC60,
Synclavier, Fairlight, Mirage, Linn 9000, and K250 all did — changing the actual playback clock
rather than resampling.

**Mode B — fixed-rate drop-sample decimation.** The stream stays at a fixed base rate (26.04kHz
for the SP-1200); pitch changes by dropping or repeating samples within that fixed-rate stream. No
reconstruction filter, which is what makes the SP-1200 bright as well as gritty.

**Mode C — delta-sigma.** 1-bit quantization with 64× oversampling and noise shaping, for the
ASR-10. The noise is pushed up out of the audible band rather than spread flat — a different
character entirely from the linear PCM modes.

Also: the base-rate parameter (the machine's effective sampling frequency, standing in for the
hardware's selectable recording bandwidth) and coarse/fine tune.

All three must be **independent of the host sample rate** — a 26.04kHz base rate means the same
thing at 44.1k, 48k, and 96k.

Analysis:
- Mode A, 1kHz sine down one octave: visible imaging at mirror frequencies around multiples of the
  effective playback rate that Phase 1 didn't have. Report frequency and level of the strongest image.
- Mode A vs Phase 1 reference, same note: null test. They must *not* null. Report residual level.
- Mode A at increasing downward transposition: image energy increases monotonically. Chart it.
- Mode B at 26.04kHz, 1kHz sine: high-frequency image content folding down, and a spectrum
  measurably different from Mode A at the same pitch. Confirm the two are genuinely different
  engines, not the same thing with different labels.
- Mode C, full-scale sine: noise floor in-band (20Hz–15kHz) vs above 20kHz. Expect low in-band,
  rising steeply out of band. A flat noise floor means the shaping isn't working.
- Repeat the Mode A/B image measurements at 44.1kHz and 96kHz host rates; the measured artifact
  frequencies must agree.
- **CPU:** measure per-voice cost of each mode, and 8 simultaneous voices in Mode C specifically.
  Report as % of a 44.1kHz/128-sample block budget. If Mode C at 64× is unaffordable, say so with
  the number and propose the reduced-oversampling fallback before building on it.

**STANDALONE CHECK 2.** With a mode selector exposed, load a breakbeat. Play at root, down a fifth,
down an octave, in each mode. Mode A should get progressively grittier and more digitally crunchy
pitching down; Mode B bright and aliased even at root, a different flavor of grit; Mode C
relatively clean with a distinctive high-frequency hiss. All three obviously different from each
other and from Phase 1.

---

## Phase 3: Quantization and companding

Deliverables:
- Linear bit-depth reduction, parameterized, covering at least 8, 12, 13, and 16-bit.
- Companded reduction: a compress-to-storage / expand-on-playback curve for the E-mu machines
  (the Emulator II stored 8-bit companded; the Emax took a 12-bit converter and companded into
  8-bit storage). Not the same as linear truncation to the same depth — quantization error scales
  with signal level rather than staying constant.
- **No dither, ever.** None of these machines had it, and it would clean up exactly the artifacts
  being emulated.

Analysis:
- Linear 8 vs 12 vs 16-bit on the 1kHz sine: noise floor for each, expecting ~6dB per bit. Report
  actual dB figures.
- Companded 8-bit vs linear 8-bit at full scale and at −30dB: similar noise at full scale,
  noticeably lower noise on the quiet signal for the companded version. That's the whole reason
  E-mu did it — if the two measure the same at −30dB, the companding is doing nothing.
- Quantization noise should be signal-correlated (harmonic distortion) rather than white at low
  bit depths on a sine. Report the harmonic structure.

**STANDALONE CHECK 3.** Same sample at 16, 12, and 8-bit linear, then 8-bit companded. Audible
steps in grit between depths, and a quieter, smoother tail on decaying material from the companded
version than from linear at the same depth.

---

## Phase 4: The capture pass (resample)

The defining technique on these machines: pitch the source up, record it into the sampler, then
pitch it back down on playback. The sound lands at its original pitch but carries the artifacts of
having been played back well below its capture rate. On the SP-1200 the canonical version is
playing a 33rpm record at 45 (about +5 semitones) and tuning back down; on Akai and MPC machines
the equivalent was speeding the source up before sampling to stretch memory, with the grit as a
side effect people came to want. Automating it — no bouncing and reimporting — is the feature.

Deliverables:
- `ConcreteCapturePass`: offline, operating on the source buffer, producing the working buffer.
  Order is **`resample → input drive/saturation → quantization`**, mirroring the physical order:
  pitched-up audio hits the machine's converter and the converter's limits get baked into what's
  stored. It does **not** pass through any filter model (see Architecture §3).
- Playback pitch compensation, on by default, applying the inverse transpose so a note plays at the
  expected pitch. Off gives the pitched-up sound directly.
- Non-destructive, per Architecture §2: the source buffer is never written; the working buffer is
  regenerated on a background thread and swapped in.
- Controls: capture transpose in semitones (with a labeled preset value at the 33→45 ratio, the one
  people actually want), input drive, auto-compensate on/off, and a bypass for the whole pass.
- Input drive matters more than it looks: sampling hot was part of the technique, and on the MPC60
  pushing record levels audibly rolled off the high end on playback. Model saturation here, not
  clean gain.
- Optional if cheap: iterate the capture pass N times for compounding degradation. Cap at 3–4, and
  make the cost obvious in the UI (it's already a background bake, so show progress).

Analysis:
- Capture +5 with compensation on, 1kHz sine: output still 1kHz. If pitch moved, the compensation
  math is wrong.
- Bright transient source, capture engaged vs bypassed: measurably more image/alias energy above
  8kHz in the resampled version. Report the delta in dB. No measurable difference means the feature
  is cosmetic.
- Sweep capture transpose 0 → +12 semitones and chart alias energy; expect monotonic increase.
- Input drive nominal vs +12dB: THD and HF content. Expect added harmonic distortion *and*, for the
  MPC60-style stage, measurable HF rolloff at high drive.
- Non-destructiveness: apply capture, change bit depth, apply again, then set transpose to 0 and
  bypass. Output must null against Phase 1's clean playback. If it doesn't, something is writing
  over the source.
- Thread safety: hammer re-bakes (automate capture transpose) while 8 voices are sounding, under
  a thread sanitizer build. No data races, no audible dropout, no allocation on the audio thread.

**STANDALONE CHECK 4.** Load a bright drum break. Play it clean, then engage capture at +5 with
compensation on: same pitch and groove, noticeably grittier and more aliased — the sound people
bounce audio through hardware to get. Then push input drive up and confirm it dirties further and
loses top end rather than just getting louder.

---

## Phase 5: Filter models (playback side)

Four genuinely distinct behaviors, plus bypass and a one-pole. Filters are **per-voice**, never on
the master bus — these machines had a physical filter chip per voice card.

**SSM-family lowpass** (SP-12/SP-1200, Emulator II, Emax). 4-pole, 24dB/oct, OTA-based. Soft
asymmetric saturation when driven, voltage-controlled resonance, a sensitive input that overdrives
easily, self-oscillating. The SSM2044 is the documented member; the SSM2045 (Emulator II) and
SSM2047 (Emax) are same-family parts with no datasheet-level differences found in research — model
one topology with per-preset trim on resonance headroom, self-oscillation threshold, and input
drive sensitivity.

**CEM-family lowpass with resonance gain loss** (Fairlight, Linn 9000). 4-pole 24dB/oct from the
CEM3320's configurable blocks. Output level drops as resonance rises — the classic thinning-out.

**CEM-family lowpass with resonance compensation** (Ensoniq Mirage). The CEM3328 is the 3320
hardwired as a dedicated lowpass with resonance compensation added, so passband level stays roughly
constant as resonance goes up.

**Digital filter + VCA saturation stage** (Kurzweil K250). The K250's filtering was digital; its
CEM3335 chips are dual VCAs doing amplitude shaping, not filtering. Clean digital lowpass followed
by a VCA-character saturation stage. Do not give the K250 an analog filter model.

Plus **bypass** (the SP-1200's actual path deliberately omitted the reconstruction filter) and a
**one-pole** for the SK-1. Each model gets cutoff, resonance, envelope amount, and key tracking.

This phase originally also shipped a "double smear" option baking an extra filter pass into the
capture chain (see Architecture §3) — removed after Phase 6 as redundant with Capture Iterations.

Analysis:
- Frequency-response sweep of each model at low resonance: SSM and CEM models should measure close
  to 24dB/octave. Report measured dB/oct.
- Resonance sweep on both CEM models at identical settings, measuring passband level at low, mid,
  high resonance. The 3320-style model must lose level; the 3328-style must hold roughly steady.
  This is their defining difference — the numbers must show it clearly.
- SSM model driven hot: THD and harmonic structure at nominal and +12dB input. Expect soft,
  predominantly low-order harmonics, not hard clipping.
- Self-oscillation: crank resonance with no input; SSM and CEM models produce a sine at cutoff that
  tracks cutoff correctly.
- Per-voice confirmation: 4-note chord, high resonance, envelope-modulated cutoff. Each note's
  sweep must be independent. If it sounds like one filter sweeping the chord, the filter is in the
  wrong place.

**STANDALONE CHECK 5.** Sustained pad sample, sweep cutoff by hand on each model at high resonance.
The two CEM models should differ audibly in level behavior as resonance rises; the SSM model should
fatten and distort when driven rather than clipping harshly. Play a chord and confirm each note's
filter moves independently.

---

## Phase 6: Voice architecture and polyphony

Deliverables:
- `ConcreteVoiceAllocator<MaxVoices>` per Architecture §1 — per-preset voice-count limit, oldest-
  voice stealing, zone-aware allocation, choke-group field. Running out of voices was an audible,
  characteristic part of playing these machines, so it's emulation, not a limitation to design
  around.
- Per-voice amp envelope with velocity sensitivity.
- A "contoured" envelope mode for the K250, modeling the original instrument's amplitude contour
  rather than a generic ADSR.

Analysis (in `ConcreteTests`, not just by render):
- Voice count 4, six overlapping notes: exactly 4 sound, stealing deterministic and matching the
  configured policy.
- Voice count changes when switching machines.
- Velocity response: render velocity 1, 32, 64, 96, 127 and confirm output level follows the
  expected curve.
- Choke groups: two zones in the same group, second note-on cuts the first — tested even though v1
  never sets a non-zero group.

**STANDALONE CHECK 6.** SP-1200-ish settings, play an 8-note chord then a 10-note chord and hear
notes get cut. It should feel like the hardware constraint, not a glitch.

---

## Phase 7: Machines and presets

Deliverables:
- A machine definition bundling pitch engine mode, base rate, bit depth, companding, capture-pass
  defaults, filter model and settings, voice count, and envelope defaults — as data in
  `ConcreteMachines.h`.
- **Switching machines must not touch the loaded sample.** The core workflow is auditioning one's
  own snare through machine after machine.
- A single **Machine** selector as the primary control (an automatable `AudioParameterChoice`),
  with everything else exposed underneath as secondary controls so a machine can be a starting
  point and pushed past the original spec.
- The twelve machines are *also* surfaced as factory presets through
  [`wildjag::FactoryPresetList`](../common/Presets/FactoryPreset.h) + `setupPresetCombo`, so host
  program menus work like every other plugin in the catalog. **Deliberate divergence to note:**
  that header's convention is that factory presets are decoded from `.aupreset` files saved in a
  host rather than hand-tuned. These twelve are hand-authored from the researched table below,
  because the table *is* the specification. Any additional "sound" presets layered on top should
  follow the usual `.aupreset` route. Say this in a comment at `getFactoryPresets()`.
- User preset save/load via the host's normal state mechanism (this catalog doesn't ship its own
  preset file format).

Analysis:
- For each of the twelve, render the same source at root, +12, and −12 semitones. Produce a
  spectrum per render and a summary table: noise floor, spectral centroid, image/alias energy
  above 10kHz.
- Pairwise null tests across all twelve. Any pair nulling to near-silence means two are configured
  identically and one is wrong.
- Verify against expectations: SP-1200 shows the highest out-of-band image energy of the fixed-rate
  machines; SK-1 has by far the lowest spectral centroid (a 9.38kHz machine); Synclavier and K250
  are cleanest at root and change most dramatically pitched down. Report any preset that
  contradicts these rather than quietly adjusting the expectation.
- Write the results to `analysis/validation_report.md` + `validation_results.json`, matching
  `aura-reverb/analysis/`'s output convention.

**STANDALONE CHECK 7.** One drum break, stepped through all twelve at root pitch, then again
pitched down an octave. Every machine distinguishable from its neighbors, and the pitched-down pass
showing dramatic character differences between the variable-clock machines and the fixed-rate ones.

---

## Phase 8: Hardware-panel UI

Follow `juce-hardware-panel-ui` end to end — HTML mockup in `mockups/` first, iterate with Adam,
translate to C++ only after he's happy, then verify with an actual pixel diff against the rendered
mockup (never "it looks right"). Subclass `wildjag::HardwarePanelLookAndFeel` as
`ConcreteLookAndFeel` with a theme struct; use the `EditorContent` + `wildjag::ResizableZoomHandler`
split like every other plugin. Alloy's paged layout is the precedent if the control count needs
two pages.

**This plugin needs controls the catalog doesn't have yet.** Design them as reusable components
inside `Source/` first; promote anything to `plugins/common/UI/` only if a second plugin would use
it (`AGENTS.md`'s shared-vs-plugin-specific rule).

1. **Sample slot / drop target** — file chooser *and* drag-and-drop. No plugin in the catalog uses
   `juce::FileChooser`, `FileDragAndDropTarget`, or `AudioFormatManager` today; this is new
   ground. Needs a filename readout, an eject/clear action, an error state for unsupported files,
   and a **missing-file state with a relocate action** for a path-only zone whose source has moved
   (see Architecture §2 — deliberate path-only sessions are a supported outcome now, so this isn't
   an edge case).
2. **Waveform display** with sample start/end and loop markers, drawing from the *source* buffer
   with the working buffer optionally overlaid so the capture pass's effect is visible. Must read
   the buffer safely against background re-bakes (draw from the same ref-counted snapshot the
   audio thread uses).
3. **Trigger surface.** Recommendation: a **4×4 pad grid** as the primary trigger control, not a
   piano keyboard. It reads as hardware (SP-1200/MPC), fits the panel language, and is the surface
   a multi-zone set needs later — so it doesn't get rebuilt. In v1, with one zone, the 16 pads play
   it chromatically from its root, which is exactly the SP-1200/MPC "tune the pad" workflow; add
   zones and each pad simply resolves to a different one through the same lookup. Pads show
   velocity via the existing LED chrome, and a secondary compact keyboard can toggle in for pitched
   playing.

   **DECIDED: pads for v1**, with the explicit expectation that they may be swapped for a keyboard
   if the UX turns out clunky in practice. Two constraints follow from wanting that swap to stay
   cheap:
   - **Size the trigger area so a 4×4 pad grid and a two-octave keyboard fit the same rectangle.**
     Then swapping is a component swap inside a fixed footprint, not a panel re-layout — which is
     where the real cost of changing your mind would otherwise land.
   - **Neither component owns any state.** Both are pure input surfaces that emit
     `(zoneIndex, note, velocity)` into the same path MIDI takes. Nothing downstream — lookup,
     allocator, voice — can tell which one sent the note, so swapping them can't affect the DSP or
     the saved state.

   Build both into the mockup so the comparison is visual before either is written in C++.
4. **Machine selector** — a prominent selector distinct from the header preset combo, since Machine
   is a real automatable parameter, not just a preset recall.
5. **Bake indicator** — a small LED/readout showing when the working buffer is regenerating, since
   capture-pass edits are now asynchronous.
6. **"Embed samples in session" toggle** with a live payload-size readout beside it, plus a
   per-zone indicator of whether that zone is embedded or path-referenced. The size number is the
   point of the control — it puts the tradeoff in front of the user at the moment they choose,
   rather than surfacing later as a slow-saving project.
7. **Numeric readouts** for bit depth and base rate, in the panel's existing display-font style
   rather than as knob value text.

Verification is the skill's methodology verbatim: render the mockup fresh with headless Chrome,
capture the real window bounds with `osascript` rather than a hardcoded crop, and diff the two
images directly. Don't claim a match without the diff.

---

## Phase 9: Ship

A new plugin isn't done until it's registered everywhere the catalog enumerates plugins. Seven
files plus the docs and the site:

- [ ] `plugins/concrete-sampler/README.md` — build/launch/target names/`auval` line, per every
      other plugin's README.
- [ ] `installer/` via the **`wildjag-plugin-installer`** skill, **and** registration in
      `installers/build-all.sh`'s `for entry in ...` list (the skill calls out what silently breaks
      if this is skipped).
- [ ] `scripts/build-all.sh` — add `"plugins/concrete-sampler:Concrete"`.
- [ ] `scripts/test-all.sh` — add **both** `:Concrete` and `:ConcreteProcessor`.
- [ ] `.github/workflows/build-and-test.yml` — add both test-matrix entries *and* the
      `version-guardrail` matrix entry. (The guardrail passes trivially on the introducing PR,
      since there's no base version to compare against.)
- [ ] `.github/workflows/release.yml` — both the build loop and the `entry` list
      (`plugins/concrete-sampler:Concrete:Concrete`).
- [ ] `.github/workflows/sync-site-versions.yml` — add the plugin directory.
- [ ] Root `README.md` — plugins table row and the repo-structure block.
- [ ] `AGENTS.md` — the plugin list in "Repo shape".
- [ ] Marketing site (`~/code/audio-plugins-site`, `gh-pages` worktree): card, screenshot, and a
      `Concrete.pdf` manual in `assets/manuals/`, following the established manual pipeline. Manual
      copy stays literal and unpadded — no scene-setting, no unrequested sections.
- [ ] Version: leave `0.1.0` for the first shipping PR (nothing to bump from), then use
      `wildjag-plugin-version-bump` on every subsequent commit touching `Source/` or
      `CMakeLists.txt`.

---

## Machine table

Values marked **(est.)** could not be confirmed in research and are reasonable starting points to
be tuned by ear, not specifications. Everything unmarked is from documented sources.

| Machine | Pitch engine | Base rate | Bit depth | Companding | Filter model | Voices | Notes |
|---|---|---|---|---|---|---|---|
| **E-mu SP-1200** | B: fixed-rate drop-sample | 26.04kHz (fixed) | 12-bit | Linear | SSM-family, **plus bypass option** | 8 | Reconstruction filter deliberately omitted on the real unit. Ship two variants or a toggle: filter engaged (SSM2044 in circuit) and the raw unfiltered path. Highest image energy of any preset. |
| **E-mu Emulator II** | A: variable clock ZOH | 27.7kHz | 8-bit | Yes, companded | SSM-family | 8 (est.) | Warmest of the E-mu family. Lower rate and lower stored depth than the Emax. |
| **E-mu Emax** | A: variable clock ZOH | up to 42kHz (variable) | 12-bit converter, companded to 8-bit storage | Yes, companded | SSM-family | 8 (est.) | Brighter than the EII from the higher rate, but same companding scheme. Default base rate around 42kHz. |
| **Akai MPC60** | A: variable clock ZOH | 40kHz (est., variable) | 12-bit | Linear | None / gentle output stage (est.) | 16 (est.) | Shares the Akai S900 sampler lineage: playback speed changed per voice rather than algorithmic pitch shifting. Documented that hot input levels roll off the high end on playback, so a saturation-linked HF loss on the output stage is worth modeling. |
| **Fairlight CMI (II/IIx)** | A: variable clock ZOH | 32kHz (24kHz for Series I variant) | 8-bit | Linear | CEM-family with resonance gain loss | 8 | Eight channel cards, each with its own filter chip and its own clocked playback. Consider a second preset at 24kHz/8-bit for the Series I. |
| **Synclavier II** | A: variable clock ZOH | 50kHz default, programmable up to 100kHz | 16-bit | Linear | None | 8 (est.) | The high ceiling exists so downward transposition has headroom before aliasing. Cleanest preset at root pitch; the most dramatic grit onset when pitched down. Expose the base rate prominently here, it's the whole character control. |
| **Kurzweil K250** | A: variable clock ZOH | 5kHz–50kHz (variable) | 16-bit | Linear | Digital lowpass + VCA saturation stage | 12 | No analog filter model. The CEM3335 chips are dual VCAs doing amplitude contouring, not filtering. Use the contoured envelope mode here. |
| **Ensoniq Mirage** | A: variable clock ZOH | 10kHz–33kHz (variable) | 8-bit | Linear | CEM-family **with resonance compensation** | 8 | The one machine confirmed to use the compensated CEM3328. Level holds steady as resonance rises, unlike the Fairlight/Linn model. The real unit was not allowed to self-oscillate; consider capping resonance. |
| **Ensoniq EPS** | A: variable clock ZOH | ~22kHz (est., variable) | 13-bit | Linear | CEM-family with resonance gain loss (est.) | 8 (est.) | 13-bit is unusual and worth getting exactly right, it sits between the Mirage and the 16-bit machines. Filter chip not confirmed in research; treat as est. and tune by ear against reference recordings. |
| **Ensoniq ASR-10** | C: delta-sigma | ~30kHz effective, 64× oversampled | 1-bit delta-sigma | N/A | Digital filter | 8 (est.) | Architecturally the odd one out. Character comes from noise shaping, not bit-depth reduction. Do not approximate this with linear bit crushing. Watch the CPU cost (Phase 2). |
| **Linn 9000** | A: variable clock ZOH | 11kHz–37kHz (variable) | 8-bit | Linear | CEM-family with resonance gain loss | 13 poly / 18 multitimbral | Per-voice cards with individual tuning, level, and pan. The natural candidate for multi-out and per-zone tuning once multi-zone sets ship — see Architecture §1 on why the bus decision can't wait for that. |
| **Casio SK-1** | A: variable clock ZOH | 9.38kHz (fixed) | 8-bit | Linear | Simple one-pole lowpass | 4 | Single sample buffer, no per-voice card architecture. By far the darkest and crudest preset. Add portamento and vibrato, which the original had. |

### Table notes

- Where a machine has a variable base rate, the preset sets a sensible default but the base-rate
  control stays user-adjustable. That range is a large part of what made each machine flexible.
- The SP-1200's filter-bypass behavior is the single most sonically important detail here. The
  missing reconstruction filter is why it's bright as well as gritty, and it's the detail most
  bitcrusher plugins get wrong.
- The Mirage vs Fairlight/Linn filter difference (compensated vs uncompensated resonance) should be
  audible when A/B'ing those presets at high resonance. If it isn't, **Phase 5** didn't land.
- Voice counts marked (est.) affect feel more than tone. Reasonable to ship and refine later.
- **Capture-pass defaults.** Every preset ships with the capture pass off — it's a technique the
  user applies, not part of a machine's stock behavior. What each preset sets is the default
  transpose the control lands on when engaged: **+5** for the SP-1200, Emulator II, Emax, and Linn
  9000 (the record-at-45 lineage), and **+12** for the MPC60, Mirage, Fairlight, and EPS (the
  memory-stretching lineage, where a full octave was common). Synclavier and K250 default to +5 but
  matter less, since their high base rates make the capture pass subtler. The SK-1 defaults to +5
  and hits its limits fast at 9.38kHz, which is the fun of it. The ASR-10's delta-sigma path
  responds differently to capture than the PCM machines — verify it separately rather than
  assuming the PCM behavior transfers.

---

## Ordering and dependencies

Phases 1–6 are strictly sequential; each builds on the last. Phase 7 requires all of them, Phase 8
requires Phase 7 (the control set has to be settled before the panel is laid out), and Phase 9 is
the ship checklist. Phase 0's harness is required by every phase and must not be skipped or
deferred.

Phase 4 sits before Phase 5 because the capture pass depends on the pitch engine and the
quantization stage but deliberately **not** on the filters — the filter chips were on the playback
side (see Architecture §3 for the "double smear" option that briefly crossed this line and was
later removed).

Multi-zone sets — kits, multisampled instruments — are out of scope for v1, but Architecture §1 is
what makes them a UI change rather than a rewrite: the zone list, the list-based state schema, the
zone-aware allocator with choke groups, the per-zone value storage, and the pad grid all exist in
v1 with a single zone in them. There is no mode to add later, only zones and a drop-mapping
default.

---

## Decisions

Settled 2026-09-05:

1. **Output buses — ship main stereo only, architect for aux outs.** No CPU difference either way;
   the cost of declaring them in v1 is `isBusesLayoutSupported` complexity, new `auval`/host
   testing surface, and a stray "Multi-Output" variant in Logic's instrument menu that routes
   nothing. So v1 declares one stereo bus, but routes every voice through a destination index and a
   `ConcreteBusRouter` that collapses to bus 0 — adding real aux outs later is then a mapping and
   a constructor change, not a voice-code change. See Architecture §1 for the full provision list
   and for the part that genuinely can't be pre-built.
2. **Trigger surface — 4×4 pad grid**, with a keyboard swap kept cheap by the fixed-footprint and
   stateless-surface constraints in Phase 8. Revisit if the UX feels clunky in use.
3. **Accent pair — provisionally signal blue** (`#5a72b0` / `#7fa5f5`). Changing it later is a
   three-line edit to the theme struct in `ConcreteLookAndFeel.cpp`, since `PluginEditor.cpp` reads
   the accent back through `getAccentColour()` rather than repeating hex literals. Only the
   non-code artifacts (mockup, site screenshot, manual PDF) need regenerating.
4. **Per-zone automation — non-automatable per-zone state**, plus one automatable "focused zone"
   set. See Architecture §1.
5. **Sample persistence — hybrid by default, with a user override.** Path always stored; FLAC
   audio also embedded under a 20MB-per-zone / 100MB-per-instance cap. Plus an "Embed samples in
   session" toggle that forces embedding regardless of size, showing the resulting payload size
   live so the tradeoff is visible at the point of decision. Because deliberate path-only sessions
   are now possible, missing files get real relocate handling rather than silently loading empty.
   See Architecture §2.
6. **Shared render-harness extraction — deferred.** `ConcreteRenderIR` carries its own copy of the
   ~60 lines of boilerplate for now; the analysis of what is and isn't shareable is kept below for
   whenever this gets picked up.

Nothing is blocking Phase 0.

---

## Deferred: extracting the shared render harness

**Not doing this now** — recorded so the analysis isn't re-derived later. Concrete ships its own
`Source/Tools/RenderIR.cpp` like the other seven, making it the eighth copy.

Seven plugins have a `Source/Tools/RenderIR.cpp`, and a measurable slice of each is the same code.
Verified mechanically, not by eye: `parseArgs` and `getFloatArg` are **byte-identical across all
seven**; the WAV-writing tail is identical in three and one line apart in two more; the
preset-by-name lookup is identical modulo the processor type in the three plugins that have
presets. That's roughly 55–70 lines of the 150–306 in each file.

What is *not* shared, and shouldn't be: the test-signal design (per-plugin by intent — Caverns'
four-segment L/R-asymmetric signal exists for its own reasons, Alloy drives MIDI instead), the
render loop (Caverns has a two-phase sample-rate-change mode, Alloy slices MIDI per block, Shields
is impulse-only), the `allParamIDs` table, and plugin-specific hooks like Alloy's
`setAgeSeedForTesting`.

So the extraction is the boilerplate, not the harness:

```
plugins/common/tools/OfflineRender.h    # header-only, no CMake helper needed
  wildjag::render::Args                 # parseArgs / getFloat / getString / has
  template <typename P> setParam(P&, const char*, float)
  template <typename P> applyPresetByName(P&, const std::string&) -> bool
  template <typename P> applyParamOverrides(P&, const Args&, span<const char* const>)
  writeWav(const juce::File&, const juce::AudioBuffer<float>&, double sampleRate) -> bool
```

Header-only on purpose: each plugin is an independently configured CMake project, and this is
compiled once per plugin anyway, so it needs no `AddOfflineRender.cmake` alongside
`AddHardwarePanel.cmake` — just an `#include "../../common/tools/OfflineRender.h"`.

**When it's picked up**, two things to expect. The refactor can be proven neutral by re-rendering
each plugin's output before and after and confirming the WAVs are byte-identical — eight working
tools with known-good behavior is a good position to refactor from. And because `Source/Tools/`
sits under `Source/`, the `version-guardrail` CI job fires for every plugin touched, so it needs a
**patch bump per plugin** (a tool-only change is patch under the versioning rubric). Mechanical,
but noisy, and worth expecting rather than discovering in CI.
