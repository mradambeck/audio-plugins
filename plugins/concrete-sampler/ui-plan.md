# Concrete Phase 8 — sampler-panel UI direction

## Context

Phase 7 (machines and presets) is finished, verified, and uncommitted — that lands first.

Phase 8 is the hardware-panel UI, and the plan doc currently says "Follow `juce-hardware-panel-ui`
end to end" while, in the same section, listing **seven controls the catalog has no precedent for**
(drop target with a missing-file relocate state, waveform with loop markers, 4×4 pad grid, prominent
machine selector, bake indicator, embed toggle with live payload readout, standalone numeric
readouts). That internal tension is the strongest existing argument that Adam's instinct is right:
the shared language is Roland RE-501 **rack-effect** DNA — a textured chassis, badge-labelled section
boxes, 88px knobs — and Concrete is a **tabletop sampler**. The skill itself is built on the premise
that "the only thing that changes per plugin is the accent colour pair," which is exactly the premise
that breaks here.

Adam also already has a substantial LCD prototype at `/Users/adambeck/code/lcd-mockup` — React 19 +
Vite, ~900 lines. It is far more than a waveform panel: it already has a **boot screen**, **four paged
views** (`sample | hardware | filter | resample`), **soft-key navigation with inverse-video
selection**, and a settled blue-backlit palette. Those four views map almost 1:1 onto Concrete's
parameter groups. This direction builds on that rather than restarting.

**Scope of the next work session: an approved mockup and a rewritten Phase 8 spec — not finished C++.**

---

## Step 0 — commit Phase 7 (blocked by plan mode; do this first)

Everything is verified already: `ConcreteTests` 625,180 assertions, `ConcreteProcessorTests` 214,121,
and `verify_phase0.py`–`verify_phase7.py` all green. One commit covering the machine table, the
Machine parameter + factory presets, the pitch-engine root-pitch fix, and the two stale-analysis-script
fixes. Files are listed in `git status` under `plugins/concrete-sampler/`.

---

## The design thesis: _the screen is the instrument_

On these machines the display is not decoration — it **is** the parameter surface, and the panel
around it is mostly buttons that navigate it. Grounding: the MPC60 shipped **56 buttons and one
knob**; the MPC60 and ASR-10 both used a **240×64** display; the Mirage's entire front panel is a
two-digit LED and a keypad.

Concrete can't be a literal replica of any one machine — it emulates twelve. So it should be a
sampler **that never existed**, built from the shared design DNA of the twelve rather than skinned as
any single one. (This is also the gap in the market: Arturia's CMI V goes full skeuomorphic replica,
TAL-Sampler goes fully abstract.)

### Three divergences, each traceable to a real difference

| Rack effect (today's catalog)                                | Tabletop sampler (Concrete)                                                                                                                                   |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Textured chassis + wrapped bezel + inset panel               | **No chassis.** One flat dark field, a genuinely recessed _screen well_ (the one place a bevel belongs), a hairline front-edge lip                            |
| Sections = 3.5px `#e6ece6` rounded box + filled accent badge | **Silkscreen legends** — small-caps label + hairline rule, no boxes, no badges                                                                                |
| 88px/112px knobs dominate                                    | **Buttons dominate.** 10 of Concrete's 19 params are natively discrete (5 choice, 3 int, 2 bool); only 9 are continuous, and most of those are set-and-forget |

Panel colour stays dark grey/near-black as Adam asked. Net effect is **less** chrome than the current
language, not more.

### What we keep (none of it encodes the rack aesthetic)

- `wildjag::ResizableZoomHandler` + the `EditorContent`/shell split — Concrete lacks this today and
  should gain it (`plugins/common/UI/ResizableZoom.h`)
- `wildjag::setupPresetCombo` / `FactoryPresetList` for the header preset menu
- Oswald SemiBold for silkscreen small print (it's DIN-label duty — perfect for panel legends);
  Oxanium stays available for the wordmark
- The existing `ConcreteLookAndFeel` subclass and its provisional signal-blue accent
  (`#5a72b0`/`#7fa5f5`) — which, usefully, is already close kin to the prototype's LCD blue
- `drawLinearSlider` (fader chrome), `drawToggleButton` (button + LED), combo chrome
- **Every "JUCE API notes" trap in the skill** — these are JUCE facts, not style: per-member
  `setLookAndFeel()` for Sliders, `drawLabel`'s unconditional `drawRect`, blur clipping vs component
  bounds, font-height ratios, `drawComboBoxTextWhenNothingSelected`'s coordinate space
- The mockup-first + headless-Chrome + `osascript` + PIL pixel-diff loop — process is sound for any
  visual target

### What we drop

The chassis texture and wrapped bezel, the inset panel-within-chassis, `drawHardwareSection()`'s
border+badge motif, knob-led layout, and the skill's **"no new Component subclasses, on purpose"**
rule — an LCD is inherently a custom-painted component, and this plugin needs several.

---

## Decisions taken (confirmed with Adam)

1. **Editing model — screen + performance strip.** The LCD's pages own the 10 discrete params and the
   set-and-forget continuous ones. A short strip of physical controls owns what you grab mid-take.
2. **Control shape — a mix of vertical faders and small sampler-styled knobs**, chosen per control.
3. **Soft keys — both.** The screen prints the four labels in its footer band _and_ four real buttons
   sit beneath, both hit-testing the same action. Mitigate the redundancy risk by making the on-screen
   row read clearly as _labels that light_, and the buttons as _hardware_ — the button under the label,
   not two competing controls.
4. **Screen colour — one consistent blue** throughout (`#1231de → #0555eb` backlight, `#c4cef9` ink).
   The twelve machines differentiate by sound and by what the screen prints, not by chrome.

---

## Parameter allocation

**Panel — performance strip (proposed; confirm visually in the mockup):**

| Control                 | Shape           | Why                                                                                                                                                 |
| ----------------------- | --------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| Base Rate               | **fader**       | The single most character-defining continuous param, huge skewed range, and a genuinely musical sweep. Pairs with Phase 8 item 7's numeric readout. |
| Filter Cutoff           | **fader**       | The one control you ride constantly                                                                                                                 |
| Filter Resonance        | small knob      | Classic pairing beside cutoff                                                                                                                       |
| Coarse Tune / Fine Tune | two small knobs | Tuning trim pots, as on an SK-1/Emax                                                                                                                |

Two tall faders for the two frequency-domain sweeps, three trim knobs for Q and tuning.

**Panel — buttons:** Machine selector (prominent, its own block — it's a real automatable parameter,
distinct from the header preset combo), the four soft keys, Load / Eject, One-Shot, and the bake +
embed indicator LEDs.

**Screen pages** (extending the prototype's existing four):

- `SAMPLE` — waveform, filename, size, duration, root note, one-shot, start/end + loop markers
- `MACHINE` — machine, pitch engine, base rate, bit depth, quantizer mode, voice count, amp envelope
- `FILTER` — filter model, cutoff, resonance, env amount, key track
- `CAPTURE` — the prototype's empty `resample` stub, filled in: transpose, drive, iterations, bypass,
  pitch compensate — plus bake progress, which belongs here since this page causes it

**Trigger surface:** 4×4 pad grid, already DECIDED in the plan doc, with the two constraints that keep
a keyboard swap cheap — a fixed footprint that a two-octave keyboard also fits, and both surfaces
stateless (they only emit `(note, velocity)` into `processor.keyboardState`).

---

## Processor-side plumbing (real work, not polish)

Four genuine gaps the UI cannot paper over. These land **before or with** the editor:

1. **No public bake-in-progress signal.** `bakeRequested` is private _and_ is cleared before
   `rebakeNow()` runs, so it isn't even a correct in-progress flag. Needs a real public signal — and
   Phase 4's own spec asks for _progress_, not a binary LED, for the 4-iteration case.
   (`Source/PluginProcessor.h:228-252`, `PluginProcessor.cpp:214-250`)
2. **No change-broadcast to the editor.** Nothing repaints when a background bake publishes a new
   sample set. Add a `juce::ChangeBroadcaster` on the processor (preferred over an editor Timer).
3. **No cheap payload-size accessor** for the embed readout. The only sizing path is a real FLAC
   encode (`ConcreteSampleIO::encodeZoneAsFlac`) — far too expensive per-paint. Needs a cached,
   background-computed size.
4. **`loadSample()` runs on the message thread** (`PluginEditor.cpp:433-447`) — its own comment already
   flags this as Phase 8's problem. This is also what gives the load animation something real to cover.

---

## The load animation has a real job

Not decoration — it covers three genuinely asynchronous operations: plugin open (the boot screen),
sample load (once #4 above is off the message thread), and capture-pass re-bakes (#1). Authentic
reference: the **Mirage flickers random values, then settles** on a two-digit code. Adam's blinking
`_` cursor and the braille-ghost boot screen are already the right instinct; the sequence just needs
to be driven by real state rather than a 2600ms timer.

---

## Mockup workflow

**Extend the React prototype rather than restarting.** It already renders real dropped audio through
wavesurfer, and iterating in React is as fast as CSS. Build the full panel around the existing screen,
iterate with Adam there, then **export one static HTML snapshot** into
`plugins/concrete-sampler/mockups/` (base64-embedded fonts, `disabled` on every control, per the
catalog convention) as the pixel-diff ground truth. Read
`plugins/common/LookAndFeel/MOCKUP_GROUND_TRUTH.md` before writing it.

Reusable values already settled in the prototype: backlight `linear-gradient(90deg, #1231de, #0555eb)`,
ink `#c4cef9`, LCD-off surround `#061e45`, glass recess `inset 0 0 10px 2px rgba(0,0,0,0.5)`, 3px ink
borders, inverse-video selection, and the trick where played waveform bars use `#0555eb` so they
dissolve into the backlight.

---

## Open items to resolve during the mockup

- **VCR OSD Mono licensing.** The prototype uses it for everything. Confirm redistribution/embedding
  rights before it ships in a binary; the catalog's convention is OFL-licensed faces with the OFL.txt
  committed alongside. Fallbacks: an OFL pixel/terminal face, or hand-drawing a true bitmap font.
- **`/Users/adambeck/code/lcd-mockup` has zero commits — every file is untracked.** Worth committing
  before building on it.
- **LCD pixel grid under zoom.** `ResizableZoomHandler` applies an arbitrary `AffineTransform`; a
  crisp pixel-grid screen will go soft at non-integer scales. Decide whether the screen snaps to
  integer internal scaling.
- **Screen resolution.** Whether to adopt the authentic 240×64 (MPC60/ASR-10) and scale up by an
  integer factor, or keep the prototype's 500×250.
- Whether to add scanlines / dot-matrix grid / glow. The prototype has **none** — and Adam already
  tried `text-shadow` glow and commented it out, which is worth respecting.
- Accent pair confirmation, now that the screen blue is settled.
- Window size (currently 700×726; the pad grid and screen will drive this).

---

## Verification

Per the skill's methodology, unchanged: render the mockup fresh with headless Chrome (never from
memory), capture the real window with `osascript` bounds rather than a hardcoded crop, and diff the
two images side by side — after every round of fixes, not once at the end. The skill records Caverns
being declared "matching" twice while visibly wrong; don't repeat that.

---

## Explicitly out of scope for the next session

Writing the C++ editor. Multi-zone editing, choke-group UI, per-zone tune/level/pan (three APVTS
params that don't exist yet), and drop-mapping-follows-machine are all later work — the plan doc
already defers them.
