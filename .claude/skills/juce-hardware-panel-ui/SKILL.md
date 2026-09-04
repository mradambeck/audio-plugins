---
name: juce-hardware-panel-ui
description: Apply this user's shared "hardware panel" visual design language (Roland RE-501-inspired) to a JUCE audio plugin's UI — a textured chassis, sectioned/badge-labeled control groups, flat matte knobs, and cream LED pushbuttons. Use whenever styling or restyling a JUCE plugin's PluginEditor/LookAndFeel for this user, or when starting a new plugin that should visually match their existing catalog (e.g. Caverns). The only thing that changes per plugin is the accent color pair.
---

# JUCE hardware-panel UI

A reusable visual language for this user's JUCE plugins, modeled on 1980s
rack-gear hardware (originally developed against a Roland RE-501 Chorus Echo
reference) rather than a typical flat "modern DAW plugin" look. It was worked
out over many rounds of visual iteration for the **Caverns** delay plugin,
then extracted into a shared base class, `wildjag::HardwarePanelLookAndFeel`
(`plugins/common/LookAndFeel/HardwarePanelLookAndFeel.h/.cpp`), once the same chrome
had been proven out (verbatim, not re-derived) across six plugins. That file
— plus `plugins/common/LookAndFeel/HardwarePanelTheme.h` — is the **canonical
reference implementation**. `plugins/caverns-delay/Source/PluginEditor.h/.cpp` is
still the reference for the *editor-side* layout/hand-painted chrome pattern
(sections, badges, chassis texture), since that part stays per-plugin.

**Code is the source of truth, not this description and not the reference
screenshot below.** For a new plugin, you are not copying a LookAndFeel file
and editing marked blocks anymore — you are subclassing the shared base:

1. `Source/<Plugin>LookAndFeel.h`: subclass `wildjag::HardwarePanelLookAndFeel`
   (`#include "HardwarePanelLookAndFeel.h"`) with just a constructor. Only add
   an override if this plugin needs one of the extension points below.
2. `Source/<Plugin>LookAndFeel.cpp`: build a `wildjag::HardwarePanelTheme` in
   an anonymous namespace — the accent colour trio, badge ink, and the
   display/small-print typefaces (loaded from this plugin's own
   `BinaryData.h`) — and pass it to the base constructor. This theme struct is
   the **one place** these values live; `PluginEditor.cpp`'s hand-painted
   chrome (section badges, wordmark) reads them back via
   `getAccentColour()`/`getBadgeInkColour()` on the LookAndFeel instance, not
   a second copy of the hex literals.
3. Extension points on the base class — **only override one of these if the
   new plugin actually needs it**; most plugins need none:
   - `paintRotarySliderOverlay(...)` — a knob decoration beyond the standard
     cap/ticks/pointer. Damage is the only current user (a live gate-level
     tick on its Gate knob).
   - `getLinearSliderThumbWidth(juce::Rectangle<float> bounds)` — the
     vertical fader thumb's width. Default is `bounds.getWidth() * 0.8f`
     (Caverns/Corrosion/Gradient's paired Dry/Wet faders). Damage overrides
     to `0.4f` for its slimmer single fader; Flux overrides to
     `0.8f - 20.0f` for its wider single Blend fader. **Do not assume the
     default is universal just because most plugins use it** — this exact
     assumption caused a real, easy-to-miss visual regression during the
     original migration to this base class (caught only by a pixel diff
     against a pre-migration screenshot, not by reading the code). If a new
     plugin's fader doesn't visually match its mockup, this is the first
     thing to check.
   - `getSliderTextBoxFontHeight()` — the knob-value readout's font height.
     Default `10.5f`; Gradient overrides to `10.0f`.
   - `HardwarePanelTheme::sliderTextBoxTextColour` — the knob-value readout's
     text colour. Default `0xff7f938f` (teal-grey); Gradient overrides to
     `0xffc9a68c` (warm tan, matching its terracotta accent).
   - `drawButtonBackground`/`drawButtonText` are already on the base class
     (a momentary-trigger button variant of the toggle-button chrome, minus
     the LED) — used as-is by Flux (Shift) and Alloy (Panic), no override
     needed to use them, just wire a plain `juce::TextButton` up to the
     LookAndFeel normally.

If a genuinely new *shared* capability is needed beyond these, that's a
`plugins/common/LookAndFeel/` change (reviewed like shared infra, extending the base
class with a new well-named extension point), not a per-plugin copy-paste of
the whole file. Never touch `plugins/common/LookAndFeel/HardwarePanelLookAndFeel.*`
for a need that's actually plugin-specific.

`reference/caverns-reference.png` in this skill's own folder is a full-window
screenshot of the real, running Caverns app (not the HTML mockup — that
artifact can go stale/disappear). Use it **only** as a final "does the new
plugin's chassis/header/footer/section chrome actually still match"
comparison after building, the same way the mockup screenshots were used
during Caverns' own development (see Verification methodology below) — never
as something to read pixel values or layout back out of. Re-deriving styling
from a screenshot is the exact failure mode that caused most of the
back-and-forth during Caverns' development in the first place.

## Verification methodology — read this before claiming a match

The Caverns implementation was declared "matching the mockup" **twice** while
actually having major, easily-visible defects (knob name labels positioned
above the knob instead of below; label/value text rendering at completely
wrong sizes; a vertically-centered section that should've been top-aligned;
outlined value boxes the mockup never had). Both false "it matches" claims
happened because verification was done by eyeballing a screenshot from memory
of the mockup, not by actually comparing against it. Do not repeat that.

**1. Render the real mockup file, don't recall it.** If a mockup HTML file
exists, render it with headless Chrome and screenshot it fresh every time you
verify — never rely on memory of what it looked like several edits ago:
```sh
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  --headless --disable-gpu --screenshot=/tmp/mock.png --window-size=1300,1600 \
  "file:///path/to/mockup.html"
```

**2. Capture the actual app window precisely, not a guessed crop.** Screen
layout shifts between sessions (other windows move, panels resize), so a
hardcoded pixel offset from an earlier screenshot silently drifts out of
alignment and produces bogus measurements. Get the real window bounds first:
```sh
osascript -e 'tell application "System Events" to tell process "Caverns" to get position of window 1 & size of window 1'
screencapture -x -R<x>,<y>,<w>,<h> /tmp/app.png   # capture exactly that region
```
If a permission dialog (e.g. microphone) is covering the window, it needs to
be dismissed with **Allow**, not Don't Allow — clicking Don't Allow on a
plugin that legitimately needs mic access breaks the user's actual workflow
(this happened once already this session). System permission dialogs resist
Accessibility-API clicking; install `cliclick` (`brew install cliclick`) and
click real screen coordinates instead.

**3. Diff the two images directly, side by side, at matching crop/scale** —
stack them in one composite (PIL is fine) and read them together. A "looks
about right" glance at the app alone is not a comparison.

**4. Read the mockup's actual source before assuming a layout convention.**
The Caverns mockup puts each rotary knob's name label *below* the knob
(DOM order inside `.knob-cell` is SVG, then name, then value — check the JS
that builds it, not just how it renders at a glance), which is backwards from
how almost every other plugin places a knob's name. Don't assume "label above
control" just because that's the conventional JUCE/plugin pattern — grep the
mockup's actual markup/CSS for `padding`, `margin-top`, `gap`, `font-size`,
and DOM order and translate those values directly, not from memory of what
the rendered page looked like.

**5. Never write a LookAndFeel getter that ignores its own parameter.**
```cpp
// WRONG — silently overrides every setFont() call anywhere else in the app,
// because it returns the same font for every Label regardless of what that
// specific Label was told to use.
juce::Font getLabelFont(juce::Label&) override { return getDisplayFont(11.0f); }
```
`Label::getFont()` always returns the last value passed to `setFont()` —
*including* JUCE's own internal default (`FontOptions{15.0f}`) if `setFont()`
was never called. The correct pattern checks for that sentinel and only
supplies a default in that case, otherwise it must respect what the label
was explicitly given:
```cpp
juce::Font getLabelFont(juce::Label& label) override
{
    static const juce::Font unset { juce::FontOptions{15.0f} };
    const auto labelFont = label.getFont();
    if (labelFont == unset)
        return getDisplayFont(11.0f).withExtraKerningFactor(0.09f);
    return labelFont;
}
```
The same trap applies to any `LookAndFeel` override that receives the
component as a parameter (`getComboBoxFont`, `getTextButtonFont`, etc.) —
if you don't have a reason to ignore the parameter, you're probably about to
clobber some other call site's explicit styling.

**6. `LookAndFeel_V4`'s default `drawLabel` always calls `g.drawRect(...)`
at the end**, drawing an outline using `Label::outlineColourId` regardless of
what branch it took. Setting that colour ID to `transparentBlack` *should*
suppress it and often does, but if an outline keeps showing up anyway after
you've set every plausible colour ID to transparent, stop chasing the colour
resolution chain and just override `drawLabel` directly — it's ~10 lines and
removes all ambiguity about what's drawing the box.

**7. A JUCE component's width constrains everything drawn inside it,
independent of what the mockup's CSS visually shows.** If a mockup's fader
track is 12px wide but its text/value sits in a wider flex cell around it
(browsers don't clip overflowing text by default), do **not** size the JUCE
`Slider` component to match the visual 12px track — JUCE's built-in value
textbox and any attached `Label` clip/ellipsize to the component's actual
bounds. Size the component to fit its widest required content (the value
text), and let the LookAndFeel's `drawLinearSlider` paint a visually-thinner
track within those wider bounds.

## The one thing that changes per plugin: the accent pair

Every other token below is fixed across all plugins using this language.
Pick a new **accent pair** for each plugin (a muted variant and a brighter
saturated variant of the same hue):

| Plugin | Muted accent | Bright accent |
|---|---|---|
| Caverns (delay) | `#579e92` (teal-green) | `#34d6bb` |
| *new plugin* | *pick a muted, desaturated version of the plugin's hue* | *a punchier, more saturated version of the same hue* |

- **Muted accent** is used for: section badges (background), ComboBox arrows.
- **Bright accent** is used for: the brand wordmark, and the Dry/Wet-style
  fader fill (or whatever the plugin's primary continuous-mix control is).
  These two get a brighter pop than everything else — a deliberate choice
  made during review, not an inconsistency.
- Knobs do **not** use the accent at all — see below.
- LEDs on pushbuttons (Bypass/Sync/Link-style transport/mode toggles) are
  **always red** (`#e4463c`), independent of the plugin's accent — it reads
  as a universal "engaged" indicator, the way real hardware panel lights do.

## Fixed tokens (do not vary per plugin)

**Structure/chassis**
- Outer chassis: textured near-black gradient (`#1c1f20` → `#0a0c0d`), a fine
  **woven cross-hatch grain** — two overlapping sets of full-canvas diagonal
  hairlines (roughly ±35°, ~3px period, alternating light/dark at low alpha),
  *not* random speckles, which read as dust rather than a leatherette weave —
  plus a soft radial highlight near the top-left (as if lit from above) and a
  couple of diagonal scuff-highlight gradients. Drawn once into a cached
  `juce::Image` in `resized()`, never per-`paint()` call. No image assets.
- An outer drop shadow on the whole chassis (`juce::DropShadow`) plus a thin
  bright/dark hairline pair right at its edge (white ~7% alpha, black ~40%
  alpha) — this is what actually sells the "wrapped bezel" look; the texture
  alone reads as flat without it.
- Inset panel face, ~15px in from the chassis edge, rounded corners (~8px),
  its own 3-stop gradient (`#262d2f` → `#1d2325` → `#171c1d`) **plus** a
  separate soft white radial highlight over the upper-left (on top of the
  gradient, ~10% alpha) and a light vignette darkening toward the edges —
  the gradient stops alone read as flat/dim; the highlight is what makes it
  look genuinely lit rather than just diagonally tinted.
- Header/footer bars: `#14181a` → `#0d1011` gradient, 1px `#33393b` divider
  under the header only.

**Sections** (the core "hardware panel" motif — grouped controls in a
bordered box with a badge, echoing Roland's CHORUS/ECHO/REVERB groupings)
- One **unbroken** 3.5px border per section (`#e6ece6` @ 62% alpha,
  ~7px corner radius) — the badge sits *inside* it, never straddling/breaking
  the border line.
- Badge: centered, hugs its text (no forced min-width), ~7px/18px/5px padding,
  muted-accent fill, dark ink text (`#0c211d`), no LED dot, no drop shadow.
- ~40px top padding reserved inside each section for badge clearance.

**Knobs** — deliberately *not* a modern plugin's lit-arc knob. Real analog
hardware doesn't glow; ticks are static panel print, not a value-lit ring.
- Flat matte fill (`#16191a`), **no gradient**, darker than the panel for contrast.
- 11 static tick marks (fixed grey `#818f8c`, not colored by value), a fluted
  grip rim (~26 short radial lines), and a single muted-cream (`#c7c0ac`)
  pointer line with a blunt (butt-cap) end — not glossy, not accent-colored.
- Default cap size 88px; give any "primary/hero" control (Caverns: L/R Time)
  a larger 112px cap to establish hierarchy, same treatment otherwise.

**Pushbuttons + LEDs**
- Flat fill (no gradient) — off `#cec8b7`, on `#dcd6c4` — 3px corner radius,
  a real drop shadow (`juce::DropShadow`, offset+blur — not just an inset tint).
  A component's own `paint()` clips to its own local bounds, so a button
  component sized exactly to its visible rect clips off most of a blurred
  shadow drawn around it — give every button's JUCE bounds a few extra px of
  margin on each side (a `buttonShadowMargin` constant), and inset the drawn
  button by that same margin inside `drawToggleButton`, or the shadow only
  shows on buttons that happen to have empty space around them by luck.
- A small (9px) LED dot before the label text; red when on (see above), dim
  `#8a8578` when off. When on, the glow around it must be an actual soft blur
  (`juce::DropShadow` on the LED's own ellipse path) — a crisp ringed outline
  (`Graphics::drawEllipse`) reads as a ring around the LED, not a glow, and
  looks nothing like the mockup's CSS `box-shadow` blur.
- Fixed width for paired toggles (don't stretch to fill their row), centered
  as a group.

**Fader** (Dry/Wet-style vertical mix control)
- Dark track (`#14191a` → `#1c2224`), bright-accent fill gradient, and a
  **dark**, thick (11px), sharp-cornered (1px radius) thumb bar — not a pale
  rounded pill.

**Typography** — two roles, both embedded via `BinaryData` (never referenced
by system font name — see Fonts below):
- Display/label face: everything except small print — brand wordmark, section
  badges, knob names + value readouts, button text, combo text, field labels.
  Weight 700–800.
- Small-print face: header tag line, footer text only. Weight 600–700.
- Line-height/vertical-centering note: whichever exact font you embed, its
  metrics may sit visually high in flex/centered boxes (this happened when
  Caverns settled on Oxanium) — verify centering in badges/buttons against a
  screenshot rather than assuming default centering is correct.

## Fonts: sourcing and embedding

Never reference display fonts by system name — Windows/Linux users won't have
Futura/Century Gothic/etc., and the whole point is a consistent look. Instead:

1. Pick free (OFL-licensed) faces with the right character. Caverns landed on
   **Oxanium** (display — geometric sci-fi/retro-futuristic) + **Oswald**
   (small print — DIN 1451-inspired). For a new plugin, a different accent
   hue might call for a different display face; re-run the same "show me a
   few options rendered for real" process (see Design process below) rather
   than assuming Oxanium is universal — the small-print pairing (Oswald) is
   more broadly reusable since it's just doing DIN-label duty.
2. Google Fonts' repo now ships most families as **variable fonts only**
   (`ofl/<family>/<Family>[wght].ttf`, no static instances). Get the static
   weight JUCE needs with `fonttools`:
   ```sh
   pip3 install fonttools
   curl -sL -o Family-Variable.ttf \
     "https://raw.githubusercontent.com/google/fonts/main/ofl/<family>/<Family>%5Bwght%5D.ttf"
   python3 -m fontTools.varLib.instancer -o Family-Bold.ttf Family-Variable.ttf wght=700
   ```
   Also grab `ofl/<family>/OFL.txt` and keep it alongside the font in the repo
   for license compliance.
3. If this is a genuinely new face for this plugin only (not the shared
   Oxanium/Oswald pair every other plugin already uses), add both `.ttf`
   files (+ their `OFL.txt`s) to this plugin's own `Source/Assets/`, and add
   them to its `juce_add_binary_data(...)` call in `CMakeLists.txt`. If
   you're reusing Oxanium or Oswald, don't copy them in — reference
   `../common/Assets/Oxanium-Bold.ttf` / `../common/Assets/Oswald-SemiBold.ttf`
   directly from the `juce_add_binary_data(...)` `SOURCES` list instead (see
   any existing plugin's `CMakeLists.txt` for the pattern); those files are
   shared, byte-identical across the catalog, and live in one place.
4. Load in the LookAndFeel constructor:
   ```cpp
   displayTypeface = juce::Typeface::createSystemTypefaceFor(
       BinaryData::FamilyBold_ttf, (size_t) BinaryData::FamilyBold_ttfSize);
   ```
   Note JUCE's binary-data symbol naming: hyphens are *dropped* (not
   underscored), dots become underscores — `Oxanium-Bold.ttf` → `OxaniumBold_ttf`.
   Check the generated `BinaryData.h` if a name doesn't compile.
5. Expose `getDisplayFont(height)` / `getSmallPrintFont(height)` helpers on
   the LookAndFeel (falling back to a bold system font if the typeface failed
   to load) so the editor's hand-painted text (wordmark, badges, footer) uses
   the same embedded faces as the widgets.

## JUCE API notes (version-dependent, verify against the fetched JUCE)

These tripped up the first implementation — check the actual JUCE version
before assuming an API exists. JUCE is fetched via CMake `FetchContent`,
pinned in `plugins/common/cmake/FetchJUCE.cmake` (currently tag `9.0.1`); after a
`cmake -B build` configure, the checked-out source is under
`../.deps/juce-<version>/` relative to the repo root, not a sibling `JUCE/`
directory:
- String width measurement may **not** be on `Font` anymore — newer JUCE
  moved it to the static `juce::GlyphArrangement::getStringWidth(font, text)`.
- `Typeface::createSystemTypefaceFor(const void*, size_t)` and the
  `FontOptions(const Typeface::Ptr&)` constructor are the way to build a
  `Font` from embedded binary data: `juce::Font(juce::FontOptions(typeface).withHeight(h))`.
- `juce::Point<float>::getPointOnCircumference(radius, angleRadians)` (angle
  clockwise from 12 o'clock) is the cleanest way to place ticks/pointers —
  it uses the exact same angle convention as `Slider`'s `rotaryStartAngle`/
  `rotaryEndAngle`, so no manual degree conversion is needed.
- `juce::Rectangle::removeFromTop/Bottom/Left/Right` are non-`const` — don't
  call them on a `const auto` you computed for reuse elsewhere; take a mutable
  copy first.
- `Label::backgroundColourId`/`outlineColourId` and `TextEditor::backgroundColourId`/
  `outlineColourId` default to a visible grey box/outline in `LookAndFeel_V4`
  — set all four to `transparentBlack` explicitly. This alone did **not**
  reliably suppress the outline in practice; `LookAndFeel_V4`'s default
  `drawLabel` calls `g.drawRect(...)` at the end unconditionally regardless
  of which branch it took, so if the box keeps showing up after the colour
  IDs are set, stop chasing the colour resolution chain and override
  `drawLabel` directly (~10 lines) so nothing ever draws a box.
- **`juce::Font`'s "height" is not the same thing as a CSS pixel font-size,
  and the gap is font-specific — verify per font, don't assume.** JUCE
  defines height as (ascent + descent) in its own normalized sense; CSS
  `font-size` sets the em-square. For most fonts these are close enough to
  ignore, but Oswald's `hhea` ascent+descent sums to ~1.48× its `unitsPerEm`
  (check with `fontTools`: `TTFont(path)['hhea'].ascent`/`.descent` vs
  `['head'].unitsPerEm`), so a JUCE height matching the mockup's CSS px
  renders visibly smaller than intended. Where a font's ascent+descent sum
  is anomalous like this, multiply the height passed to `FontOptions` by
  `(ascent+descent)/unitsPerEm` inside the `getXFont()` helper — correct it
  in the one place that builds the `Font`, not at each call site. Confirm
  with a real pixel measurement (crop + ruler overlay, see Verification
  methodology) before and after — don't trust the arithmetic alone, and
  don't trust a quick eyeballed measurement either; both have been wrong.
- **A `Slider`'s (or `ComboBox`'s) internal value-textbox `Label` is built
  using whatever `LookAndFeel` is active at the moment it's first needed —
  which, for a `Slider`/`ComboBox`/`Button` that's a *member variable* of the
  editor, is during that member's default construction, i.e. before the
  editor's own `setLookAndFeel()` call even runs in the constructor body.**
  `ComboBox` recovers automatically (`parentHierarchyChanged()` calls
  `lookAndFeelChanged()`, rebuilding its internal label once actually
  parented) but `Slider` has no such override and never rebuilds its
  textbox on its own — it silently keeps whatever font/colour the *global
  default* `LookAndFeel` gave it at construction, no matter what the
  `HardwarePanelLookAndFeel` constructor or `createSliderTextBox` override says.
  Symptom: value readouts in the wrong font/size/colour despite the
  LookAndFeel code looking correct. Fix: call `.setLookAndFeel(&lookAndFeel)`
  explicitly on every `Slider`/`ComboBox`/`Button` member when setting it up
  (not just once on the editor) — cheap, and removes this class of bug
  entirely regardless of which JUCE component turns out to need it.
- **Baseline-aligning two labels set in very different font sizes** (e.g. a
  27px wordmark next to an 11px tag line, mockup `align-items: baseline`)
  can't be done with `Justification::centred` on equal-height boxes — the
  two fonts' baselines land at different points within an equally-centred
  box. Use `Justification::topLeft` and position each label's bounds
  explicitly from a shared baseline Y: `label.setBounds(x, baselineY -
  font.getAscent(), w, std::ceil(font.getHeight()))`.
- **`ComboBox`'s "nothing selected" placeholder text is drawn by a completely
  separate method, `drawComboBoxTextWhenNothingSelected`, not through the
  normal label/`ComboBox::textColourId` path** — and JUCE's default
  implementation always renders it at 50% alpha, regardless of what colour
  is set. Setting `ComboBox::textColourId` (even via a direct per-instance
  `setColour`, not just the LookAndFeel default) has no effect on this text
  at all. If a combo's placeholder (e.g. "Preset" shown before any program
  is picked) needs to match the same full-strength colour as selected-item
  text, override `drawComboBoxTextWhenNothingSelected` directly. **When you
  do, compute its text area from `label.getBounds()`, not
  `label.getLocalBounds()`.** This method is called from within
  `ComboBox::paint()`, so its `Graphics&` is in the *combo's* coordinate
  space — but `Label::getLocalBounds()` always returns `(0,0,w,h)`
  regardless of where `positionComboBoxText()` actually placed the label.
  Stock `LookAndFeel_V2` has this exact same mismatch in its default
  implementation; it isn't noticeable there only because the stock left
  inset is a trivial 1px. The symptom is very specific and easy to
  misdiagnose: adjusting the left-inset constant in `positionComboBoxText`
  appears to do *nothing at all* to the placeholder text's position (while
  correctly moving any real selected-item text, which goes through the
  normal `Label::paint()` → `drawLabel` path and doesn't have this bug) —
  if a positioning constant seems to have zero effect on one specific piece
  of text but works everywhere else, suspect a coordinate-space mismatch
  like this rather than continuing to change the number.
- **A component's `setColour(SomeComponent::xColourId, ...)` only propagates
  to an internally-owned sub-`Label` if that owning component has a
  `colourChanged()` override that explicitly copies it across** (`ComboBox`
  does this for `textColourId` → its internal label). Setting the colour
  only on the shared `LookAndFeel` does not trigger this — call `setColour`
  directly on the specific component instance when a colour isn't showing up
  despite the LookAndFeel default being set correctly.
- **Don't trust CSS `rgba()` alpha values to transfer 1:1 into JUCE line/fill
  alpha and expect the same visual weight.** A repeating-linear-gradient
  texture at low alpha (2-5%) reads clearly in a browser (subpixel-AA over
  many devicePixelRatio-scaled samples) but was completely invisible at
  normal viewing scale when redrawn in JUCE as literal hairlines at the same
  nominal alpha — only visible when zoomed in 5x+ during verification
  screenshots. Tune grain/texture alpha by eye against an **unzoomed**
  screenshot crop, not by porting the CSS numbers directly, and not by
  checking only a heavily zoomed-in crop (which will look fine even when the
  effect is imperceptible at actual size).
- **A component's `paint()` clips to its own local bounds**, so any blur
  (`DropShadow`, a glow drawn via a blurred path) needs the component's
  JUCE bounds to be measurably larger than the visual element it surrounds,
  in every direction the blur needs to extend — including toward whichever
  edge the "full value" position of a slider/fader puts its fill flush
  against. Undersized margins clip the blur silently (no error, no warning,
  it just doesn't render), so check specifically at extreme parameter values
  (0% and 100%), not just the default/mid position.

## No new Component subclasses, on purpose

Caverns has zero custom `Component` subclasses — every control is a stock
JUCE widget skinned via one `LookAndFeel`, and the section borders/badges/
chassis texture are hand-painted directly in `PluginEditor::paint()` via a
small private helper (`drawHardwareSection(g, bounds, label)`), the same way
the header bar was already hand-painted before this redesign. Keep that
pattern for new plugins unless a plugin has enough repeated sections to
clearly justify a real reusable Component — most won't.

## Design process for a new plugin

Don't jump straight to code. The Caverns look was arrived at through ~15
rounds of visual iteration against a **published HTML/CSS/SVG mockup**
(an Artifact), not by guessing in JUCE and rebuilding each time — C++/JUCE
iteration is much slower than editing CSS. For a new plugin:
1. Build an HTML mockup of the plugin's actual control layout using this
   skill's fixed tokens + the new plugin's accent pair.
2. Iterate on it with the user until approved — including a disabled-state
   mock if the plugin has interlocked controls (Sync/Link-style), and a
   real-font comparison board if introducing a new display face.
3. Only then translate into a `<Plugin>LookAndFeel` subclass (see above) and
   `PluginEditor` layout code, using the mockup as the pixel-accurate spec,
   and verify with
   a real screenshot of the running Standalone app (not just "it compiles").
   Follow the **Verification methodology** section above exactly — render
   the mockup fresh, capture the app window precisely, diff them side by
   side. Do this after every round of fixes, not just once at the end.

## Checklist for a new plugin

**Set on the `HardwarePanelTheme` in `<Plugin>LookAndFeel.cpp`:**
- [ ] Accent pair (`accentMuted`, `accentBrightHi`, `accentBrightLo`,
      `badgeInkColour`) — the only place these are defined; everything else
      reads them back via `getAccentColour()`/`getBadgeInkColour()` on the
      LookAndFeel instance
- [ ] Display typeface, if the accent/mood calls for something other than
      Oxanium (re-run the font-comparison step; don't assume) — set
      `displayTypeface` to the plugin's own `BinaryData` symbols and the
      correct `heightCorrectionRatio` (see the JUCE API notes above on font
      height vs. CSS px; measure it, don't guess `1.0f`)
- [ ] `sliderTextBoxTextColour`, only if this plugin's knob-value readout
      shouldn't use the default teal-grey (rare — only Gradient does this)

**Only override if actually needed (see the extension points listed above
— check each one against this plugin's mockup before assuming "not needed"):**
- [ ] `paintRotarySliderOverlay` — a knob decoration beyond cap/ticks/pointer
- [ ] `getLinearSliderThumbWidth` — if this plugin's fader isn't a standard
      paired Dry/Wet layout
- [ ] `getSliderTextBoxFontHeight` — if `10.5f` doesn't match the mockup

**Still per-plugin, in `PluginEditor.cpp` (unrelated to the LookAndFeel
theme, don't forget these just because the accent pair above is set):**
- [ ] `titleLabel`'s wordmark colour in the constructor — same hue family as
      `accentBrightHi`/`accentBrightLo` by design, but a separate literal;
      update it alongside the theme's accent pair, not instead of it
- [ ] Brand wordmark text + tag line
- [ ] Section names/grouping, matched to the new plugin's actual parameters
- [ ] Which knob(s), if any, get the larger "hero" cap size
- [ ] Footer company-name text (the version string already reads live from
      `JucePlugin_VersionString` — no code change needed there)

**Carry over unchanged (already true by construction — nothing to do, just
don't reintroduce a local copy):**
- [ ] Chassis/panel colors, texture technique, section border/badge structure
- [ ] Knob rendering (flat, static ticks, no lit arc, fluted rim)
- [ ] Pushbutton + red-LED convention
- [ ] Small-print face (Oswald) and its embedding mechanism — reference
      `../common/Assets/Oswald-SemiBold.ttf` in `juce_add_binary_data`,
      don't copy the font file into this plugin's own `Source/Assets/`
- [ ] `Label`/`TextEditor` transparent-background fix
- [ ] No new Component subclasses unless clearly justified

**Final verification (do not skip):** build the Standalone app, screenshot
it, and diff against the approved mockup per the Verification methodology
above. A plugin that "should" match because the theme values look right is
not the same as one that's been checked pixel-by-pixel — three of the six
existing plugins had a real, invisible-by-inspection rendering divergence
(thumb width, extra button chrome, textbox font/colour) that only a pixel
diff caught.
