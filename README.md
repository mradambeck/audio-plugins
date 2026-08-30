# Wild Jag Audio Plugins

[https://mradambeck.github.io/audio-plugins/](https://mradambeck.github.io/audio-plugins/)

A monorepo of macOS audio plugins (AU / VST3 / Standalone) built with [JUCE](https://juce.com)
and CMake, styled with a shared "hardware panel" visual language (see
[`.claude/skills/juce-hardware-panel-ui/`](.claude/skills/juce-hardware-panel-ui/SKILL.md)).

## Plugins

| Plugin | Folder | Description |
|---|---|---|
| Caverns | [`caverns-delay/`](caverns-delay/) | Bucket-brigade-style stereo delay |
| Damage | [`damage-fuzz/`](damage-fuzz/) | FM-mangled fuzz/distortion |
| Corrosion | [`corrosion-drive/`](corrosion-drive/) | Tanh-based distortion/overdrive |
| Flux | [`flux-phaser/`](flux-phaser/) | Analog-style phase shifter |
| Alloy | [`alloy-bass/`](alloy-bass/) | Stacked analog + FM mono synth |
| Gradient | [`gradient-pitch/`](gradient-pitch/) | Pitch-shifting delay |
| Shields | [`shields-reverb/`](shields-reverb/) | Diffuse algorithmic reverb with a slow-building swell |
| Intruder | [`intruder-gated-reverb/`](intruder-gated-reverb/) | Non-linear gated reverb modeled on the AMS RMX16's Non-Lin 2 |
| Strike | [`strike-synth/`](strike-synth/) | Extended Karplus-Strong physical-modeling string synth |

Each plugin has its own README with build/launch instructions specific to that plugin and a
description of how it works; this README covers everything shared across the whole monorepo.

## Repository structure

```
audio-plugins/
├── common/                 # Shared code/assets used by every plugin
│   ├── LookAndFeel/         # HardwarePanelLookAndFeel base class + shared LevelMeterSlider
│   ├── Assets/              # Shared logo + fonts (byte-identical across plugins)
│   └── cmake/               # FetchJUCE.cmake, AddHardwarePanel.cmake
├── caverns-delay/           # One folder per plugin, each independently buildable
├── damage-fuzz/
├── corrosion-drive/
├── flux-phaser/
├── alloy-bass/
├── gradient-pitch/
├── shields-reverb/
├── intruder-gated-reverb/
├── strike-synth/
├── installers/               # Combined "install everything" .pkg builder
├── scripts/                  # build-all.sh / test-all.sh (loop over all plugins)
├── .claude/skills/            # Project-scoped Claude Code skills for this catalog's conventions
└── .deps/                     # (generated) JUCE checkout, fetched by CMake -- not committed
```

Every plugin folder follows the same internal layout:

```
<plugin>/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp    # DSP + parameter state
│   ├── PluginEditor.h/.cpp       # UI layout
│   ├── <Plugin>LookAndFeel.h/.cpp  # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── Assets/                   # Only this plugin's own non-shared assets, if any
│   └── Tests/                    # juce::UnitTestRunner-based test suite
└── installer/                    # This plugin's own .pkg installer
```

## Requirements

- CMake 3.22+
- Xcode (macOS, for AU/VST3/Standalone codesigning and bundling)
- Nothing else — JUCE itself is fetched automatically by CMake (see below), no manual clone or
  sibling checkout needed.

## JUCE dependency

JUCE is fetched via CMake `FetchContent`, pinned to a fixed tag in
[`common/cmake/FetchJUCE.cmake`](common/cmake/FetchJUCE.cmake) (currently `9.0.1`). The first
`cmake -B build` you run, for any plugin, clones JUCE once into `.deps/juce-9.0.1/` at the repo
root; every other plugin's subsequent configure reuses that same checkout instead of re-cloning.
Nothing needs to be installed or cloned by hand.

## Building

### One plugin

Each plugin is independently configurable/buildable from its own directory:

```sh
cd corrosion-drive
cmake -B build -G Xcode
cmake --build build --config Release --target Corrosion_All
```

`-G Xcode` is recommended on macOS since JUCE's plugin bundling/codesigning post-build steps are
most reliable there. See that plugin's own README for its exact target names, `auval` command,
and Standalone-launch instructions.

### All plugins at once

```sh
scripts/build-all.sh
```

Configures and builds all plugins' AU/VST3/Standalone formats in Release, one at a time (each
still an independent CMake project — this is a loop, not a unified super-build).

## Testing

Each plugin has its own `<Name>Tests` target — a headless console app driven by
`juce::UnitTestRunner`, not CTest — that exercises its `AudioProcessor` (or, for Gradient, its
DSP classes directly) without any plugin-format/GUI machinery. Build and run one plugin's suite:

```sh
cd corrosion-drive
cmake --build build --config Release --target CorrosionTests
./build/CorrosionTests_artefacts/Release/CorrosionTests
```

Or run every plugin's suite at once:

```sh
scripts/test-all.sh
```

Exits non-zero if any plugin's suite fails — safe to wire into CI (see
[`.github/workflows/build-and-test.yml`](.github/workflows/build-and-test.yml)).

## Installers

Each plugin has its own `installer/` folder producing a standalone `.pkg`; `installers/` at the
repo root builds one combined installer covering every plugin. See
[`installers/README.md`](installers/README.md) for the full build/verify process.

## Contributing / AI-assisted development

See [`AGENTS.md`](AGENTS.md) for repo conventions, and
[`.claude/skills/`](.claude/skills/) for the project-scoped Claude Code skills that encode this
catalog's UI and installer conventions (used automatically by Claude Code; useful as reference for
any contributor regardless of tooling).

## License

[AGPLv3](LICENSE) — this matches JUCE's free-tier license terms. If you're building on this code
under a commercial JUCE license instead, you're free to relicense your own fork accordingly.
