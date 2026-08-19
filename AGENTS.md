# AGENTS.md

Instructions for any AI coding agent (Claude Code, or otherwise) working in this repo. See
[`README.md`](README.md) for the human-facing project overview this file assumes as context.

## Repo shape

This is a monorepo of independent JUCE plugins (`caverns-delay`, `damage-fuzz`,
`corrosion-drive`, `flux-phaser`, `alloy-bass`, `gradient-pitch`, `shields-reverb`) plus `common/` (shared
LookAndFeel/assets/CMake helpers) and `installers/` (the combined installer). Each plugin folder
is a fully independent CMake project — `cd <plugin> && cmake -B build` works on its own. Do not
introduce a unified CMake super-build; per-plugin independence is relied on by the installer
scripts and by the skills below.

## Build/test commands

- One plugin: `cd <plugin> && cmake -B build -G Xcode && cmake --build build --config Release
  --target <Name>_All`, then `--target <Name>Tests` + run the resulting binary. See that plugin's
  own README for exact target names.
- All plugins: `scripts/build-all.sh` / `scripts/test-all.sh` from the repo root.
- JUCE is fetched automatically (`common/cmake/FetchJUCE.cmake`, pinned to a fixed tag) — never
  add a manual JUCE clone step back into any instructions or scripts.

## Project skills

Three project-scoped Claude Code skills live in `.claude/skills/` and encode this catalog's
conventions — use them rather than re-deriving the patterns from scratch:

- **`juce-hardware-panel-ui`** — the shared visual language (chassis texture, sectioned/badged
  control groups, flat matte knobs, LED pushbuttons). Use this when styling or restyling any
  plugin's `PluginEditor`/LookAndFeel, or when building a new plugin's UI. It documents a
  mockup-first process (build an HTML/CSS mockup, iterate with the user, only then translate to
  C++) and a strict screenshot-diff verification methodology — follow both; do not skip straight
  to C++, and do not declare a UI "matching" without an actual pixel comparison against a
  baseline screenshot. New plugins subclass the shared `wildjag::HardwarePanelLookAndFeel`
  (`common/LookAndFeel/`) rather than copying a LookAndFeel file wholesale — see the skill for the
  extension points (accent theme, rotary-slider overlay, fader thumb width, textbox font/colour).
- **`wildjag-plugin-installer`** — scaffolds a new plugin's `installer/` folder and registers it
  in the group installer at `installers/`. A new plugin isn't done until both the per-plugin
  installer and the group-installer registration exist; the skill calls out exactly what silently
  breaks if the registration step is skipped.
- **`wildjag-plugin-version-bump`** — bumps a plugin's version and updates its site badge. Use
  proactively (don't wait to be asked) whenever finalizing a commit/PR that touches a plugin's
  `Source/` or `CMakeLists.txt`. See `## Versioning` below for the rubric it follows.

## Versioning

Each plugin's version lives in exactly one place — `project(<Name> VERSION X.Y.Z)` in
`<plugin>/CMakeLists.txt` — everything else (installer XML, the site's version badges) is
generated from it at build time; don't hardcode a version number anywhere else. A sibling
`set(WILDJAG_RELEASE_CHANNEL "stable")` line (`stable` | `beta`) is a separate, optional maturity
flag for explicitly marking a build as an early beta test — it's a manual, human decision, not
something to infer from a diff.

All plugins are pre-1.0 (`0.y.z`). Bug fixes / UI-only / installer-only changes bump the patch
digit; everything else — new parameters, changed defaults, and anything that would normally be a
breaking major change (renamed/removed parameters, `BUNDLE_ID`/`PLUGIN_CODE` changes, incompatible
preset format) — bumps the minor digit instead, per semver's own `0.y.z` convention. An actual
`1.0.0` only happens as a deliberate manual decision, never an automatic bump.

Use the `wildjag-plugin-version-bump` skill proactively whenever finalizing a commit/PR that
touches a plugin's `Source/` or `CMakeLists.txt` — it also updates that plugin's version badge on
the marketing site (`~/code/audio-plugins-site`, a separate worktree/branch checked out to
`gh-pages`). `build-and-test.yml`'s `version-guardrail` job is a CI backstop for when the skill is
skipped, not a substitute for using it.

The combined `WildJagPlugins-Installer.pkg` has no version of its own by design — its welcome
screen lists each bundled plugin's real version (generated from the same CMakeLists.txt values)
rather than inventing a meaningless aggregate number.

## Conventions to preserve

- **Shared vs. plugin-specific assets**: a plugin's `juce_add_binary_data(...)` should reference
  `../common/Assets/...` for anything byte-identical across the catalog (the logo, Oxanium/Oswald
  fonts) and only keep genuinely plugin-specific assets (a different display font, a plugin-only
  logo variant) in that plugin's own `Source/Assets/`. Don't copy a shared asset back into a
  plugin's local folder.
- **LookAndFeel changes**: shared rendering logic lives in
  `common/LookAndFeel/HardwarePanelLookAndFeel.{h,cpp}`. If a change is needed for one plugin only
  (a new accent colour, a font swap), it belongs in that plugin's own `<Plugin>LookAndFeel.cpp`
  theme/overrides, not in the shared base class. If multiple plugins would need the same new
  capability, add a new well-named extension point to the base class (see the existing
  `paintRotarySliderOverlay`/`getLinearSliderThumbWidth`/`getSliderTextBoxFontHeight` pattern) —
  don't special-case a specific plugin's name inside shared code.
- **Verify LookAndFeel changes visually, not just by compiling.** Screenshot the affected
  plugin's Standalone build before and after a change and diff them (pixel-by-pixel, e.g. via
  Python PIL `ImageChops.difference(...).getbbox()`) unless the change is intentionally visual.
  This caught three real, invisible-by-code-review regressions during this repo's LookAndFeel
  refactor (a fader thumb-width assumption, missing button-chrome methods, a textbox font/colour
  divergence) — treat "looks the same in the code" as insufficient on its own.
- **Tests**: each plugin's `<Name>Tests` target is a `juce::UnitTestRunner`-based console app, not
  CTest-integrated. Exit code 0 = all passed. Keep new DSP logic testable the way Gradient's is
  (DSP split into standalone classes like `GradientDelayBuffer`/`GradientPitchShiftEngine`) when a
  plugin's processor logic grows complex enough to warrant it; otherwise testing
  `PluginProcessor.cpp` directly (the other 5 plugins' pattern) is fine.
- **`.claude/settings.local.json` files** (root and any per-plugin) are personal permission
  allowlists, not shared config — don't treat their contents as project convention.

## License

AGPLv3 (see [`LICENSE`](LICENSE)). Don't add code under an incompatible license, and don't assume
a permissive (MIT-style) license when scaffolding new files.
