# Wild Jag Plugin Installers

macOS `.pkg` installers for the Wild Jag plugins:

| Plugin    | Repo               | Description                 |
|-----------|--------------------|-----------------------------|
| Caverns   | `caverns-delay/`   | Reverb/delay plugin         |
| Damage    | `damage-fuzz/`     | Fuzz/distortion plugin      |
| Corrosion | `corrosion-drive/` | Lo-fi drive plugin          |
| Flux      | `flux-phaser/`     | Analog phase shifter plugin |
| Alloy     | `alloy-bass/`      | Industrial bass synth       |
| Gradient  | `gradient-pitch/`  | Pitch shifting delay plugin |
| Shields   | `shields-reverb/`  | Diffuse reverb              |

Each plugin builds AU, VST3, and Standalone formats. Every installer (per-plugin and group) lets you choose:

- **Which formats to install** — Audio Unit, VST3, and/or Standalone App, via independent checkboxes.
- **Where to install** — a native "Install for all users of this Mac" (`/Library/Audio/Plug-Ins/...`, `/Applications` — requires an administrator password) vs "Install for me only" (`~/Library/Audio/Plug-Ins/...`, `~/Applications` — no password needed) choice. Both are locations every AU/VST3 host scans.

This is implemented with Apple's own mechanism for it: each format is built as its own *relocatable* component package (`pkgbuild --install-location` given as a **relative** path, e.g. `Library/Audio/Plug-Ins/Components`, not an absolute one), and the distribution's `<domains enable_currentUserHome="true" enable_localSystem="true"/>` lets Installer.app resolve that relative path against either `/` or `~` depending on which the user picks. There's no arbitrary "browse to any folder" picker — Audio Units are hard-restricted by macOS to those two locations regardless of installer, so a free-form folder chooser would just produce a plugin no host can find.

There are two ways to install: a **per-plugin installer** for just one plugin, or the **group installer** here, which lets you pick any combination of plugins and formats via checkboxes.

## Per-plugin installers

Each plugin repo has its own `installer/` folder:

```
<plugin-repo>/installer/
  build.sh            # build script
  distribution.xml     # productbuild distribution (format checkboxes + welcome/license/conclusion screens)
  Resources/           # welcome.html, license.txt, conclusion.html
  output/               # (generated) built .pkg files
  stage/                 # (generated) staging roots used by pkgbuild (au/, vst3/, standalone/)
```

To build one:

```sh
cd caverns-delay   # or damage-fuzz / corrosion-drive / flux-phaser / alloy-bass / gradient-pitch
./installer/build.sh
```

This builds the plugin in Release, stages its AU/VST3/Standalone artefacts separately, and produces:

- `installer/output/<Plugin>-AU-Component.pkg`
- `installer/output/<Plugin>-VST3-Component.pkg`
- `installer/output/<Plugin>-Standalone-Component.pkg`

  Three flat, relocatable component packages, one per format (also used by the group installer below).

- `installer/output/<Plugin>-Installer.pkg` — the double-clickable installer: welcome/license screens, a checkbox per format, and the install-location choice.

## Group installer (this folder)

`build-all.sh` rebuilds all six plugins by delegating to each plugin's own `installer/build.sh --component-only`, then wraps all eighteen component packages (6 plugins × 3 formats) into a single distribution. The picker shows a checkbox per plugin that expands into per-format sub-checkboxes (all checked by default, independently toggleable — unchecking a plugin's parent checkbox unchecks its formats, and vice versa):

```sh
cd installers
./build-all.sh
```

Produces `installers/output/WildJagPlugins-Installer.pkg`. The distribution uses `customize="always"`, so the picker screen always shows — you don't have to hunt for a "Customize" button.

Files:

```
installers/
  build-all.sh        # builds all six plugins and assembles the combined .pkg
  distribution.xml     # productbuild distribution defining the nested plugin/format choices
  Resources/            # welcome.html, license.txt, conclusion.html for the combined installer
  output/                 # (generated) WildJagPlugins-Installer.pkg
  stage/                   # (generated) copies of each plugin's 3 component .pkg files
```

## Notes

- Installing to the system-wide location (`/Library/...`, `/Applications`) prompts for admin credentials; installing to the per-user location (`~/Library/...`, `~/Applications`) doesn't.
- **Signed and notarized**: every `.component`/`.vst3`/`.app` is signed with a Developer ID Application certificate, every `.pkg` is signed with a Developer ID Installer certificate, and the final installer `.pkg`s are submitted to Apple for notarization and stapled (see `installers/sign-and-notarize.sh`). Building locally requires `DEVELOPER_ID_APPLICATION`/`DEVELOPER_ID_INSTALLER` identities in your login Keychain and either a `NOTARY_PROFILE` (registered via `xcrun notarytool store-credentials`) or `NOTARY_KEY_PATH`/`NOTARY_KEY_ID`/`NOTARY_ISSUER_ID` exported in your shell; set `SKIP_NOTARIZE=1` to sign-only for fast local iteration.
- Generated `stage/` and `output/` directories are git-ignored and can be deleted/rebuilt at any time.
