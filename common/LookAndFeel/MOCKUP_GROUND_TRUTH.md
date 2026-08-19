# Building a new plugin's HTML mockup: copy these, don't re-derive them

When building an HTML/CSS mockup for a new plugin (see the `juce-hardware-panel-ui` skill's
"Design process" section), the values below are **already decided** by this file's own drawing
code and by the established `PluginEditor.cpp` pattern (Caverns is the reference). Eyeballing a
screenshot to guess these produced a real, repeated back-and-forth while building Bloom's mockup —
every value here was wrong on the first pass, caught only by reading this code directly. Read this
file first; only fall back to screenshot comparison for things that genuinely aren't code constants
(the chassis texture's exact visual weight, for instance).

## Knob geometry (`drawRotarySlider`, this file)

All radii below are proportions of `radius = size/2 - 4`, where `size` is the knob's square cell
side length (the mockup's `<svg width height>`). None of this is guessable from a screenshot at
normal zoom — the flute/tick bands are only a few px wide.

| Element | Radius | Notes |
|---|---|---|
| Knob cap (solid fill) | `radius * 0.72` | The ENTIRE visible dark circle. There is no separate wider "body" ring — a mockup pass once invented one; it doesn't exist here. |
| Tick ring, inner edge | `radius * 0.84` | Floats OUTSIDE the cap with a real gap (`0.84 - 0.72 = 0.12r` of empty space) — ticks are not flush against the knob. |
| Tick ring, outer edge | `radius * 0.98` | Close to the cell's edge. |
| Fluted rim | `capRadius - 3` to `capRadius` | An overlay drawn INSIDE the cap's own edge, not a separate ring outside it. |
| Inset ring | `capRadius - 5.5` | A second, thinner circle inside the cap — easy to forget; it's a distinct stroke, not the same as the cap's own outline. |
| Pointer | `capRadius * 0.32` to `capRadius - 5.0` | |

Stroke weights/opacities (get these wrong and the ticks/flutes read as "dots" instead of lines —
this exact mistake shipped twice on Bloom's mockup before being caught):

| Element | Width | Colour/opacity |
|---|---|---|
| Major tick (start/middle/end, 3 of 11) | 2.2px | `tickColour` @ 0.85 |
| Minor tick (other 8 of 11) | 1.5px | `tickColour` @ 0.55 |
| Fluted rim line (26 of them) | **1.0px** | black @ **0.4** — thinner/lighter than intuition suggests |
| Cap outer stroke | 1.5px | black @ 0.55 |
| Cap highlight stroke (offset 0.75px inward) | 1.0px | white @ 0.06 |
| Inset ring | 1.0px | black @ 0.4 |
| Pointer | 2.6px | `pointerColour` @ 0.88, butt cap |

## Fonts: which face, per element

Two embedded faces exist (`HardwarePanelTheme::displayTypeface`/`smallPrintTypeface`) and it is
**not** simply "big text = display, small text = small-print":

- **Display face (Oxanium Bold)**: wordmark, section badges, knob/fader NAME labels, button text,
  combo text — and, counter-intuitively, **the slider's value readout** ("350.0 ms", "37.7%").
  `createSliderTextBox()` builds that Label with `getDisplayFont(getSliderTextBoxFontHeight())`
  (10.5px default) and a near-zero `0.03` kerning factor. A mockup pass shipped this using the
  small-print face with normal Oswald tracking, which read as visibly the wrong font once compared
  side by side.
- **Small-print face (Oswald SemiBold)**: the header tagline and the footer text ONLY. Both use
  noticeably WIDE letter-spacing — tagline `11px`/`0.26` kerning, footer `9.5px`/`0.14` kerning —
  much wider than anything using the display face.

## Text colour: three separate dim tiers, not one

There is no single "muted grey" reused everywhere — conflating these is what made Bloom's first
mockup pass read as "brighter/heavier" than the reference when compared side by side:

- General readable text (combo/preset text, knob name labels): the bright `ink` tone
  (`textColour`/`textColourDim`, ~`#cfe3e0`–`#e7f3f1` depending on element).
  the header tagline color (see below) is meant to be much dimmer than this).
- Header tagline: its own dedicated, notably darker colour — Caverns uses `#6f8280` (set directly
  via `tagLabel.setColour(...)` in `PluginEditor.cpp`, not any shared "dim" constant).
- Footer text: **two different** colours, not one — left ("Caverns · v0.1.0") uses `#586566`,
  right ("Wild Jag") uses `#3a4547` (even dimmer). Check the actual `g.setColour(...)` calls right
  before each `g.drawText(...)` in the footer-painting code, per plugin — don't assume symmetry.

For a new plugin, pick colours in the SAME dimness ballpark as these (they're not really
accent-tinted, just generic low-contrast ink), rather than reusing whatever "ink-dim" token the
mockup already has for knob-value text — that token needs to stay legible for a functional
readout and is measurably brighter than these three.

## Where to verify

- Knob geometry, stroke weights: `HardwarePanelLookAndFeel::drawRotarySlider()` in this file.
- Font/colour choices per label: the specific `setFont(...)`/`setColour(...)` calls in the
  reference plugin's own `PluginEditor.cpp` (Caverns is canonical) — grep for the exact label
  (`tagLabel`, `footerArea`, the slider text box setup) rather than assuming a pattern holds.
- Full mockup-building process, HTML/CSS-specific pitfalls (flexbox `min-height:auto`, matching a
  footer's padding to its sibling row, viewport-size-dependent bugs): see the `juce-hardware-panel-ui`
  skill's "HTML mockup CSS gotchas" section — this file covers the C++-side ground truth those
  mockups need to match, not the mockup mechanics themselves.
