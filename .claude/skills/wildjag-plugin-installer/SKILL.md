---
name: wildjag-plugin-installer
description: Scaffold a macOS .pkg installer for a Wild Jag JUCE plugin, following the exact conventions used across the existing catalog (Caverns, Damage, Corrosion) — per-format (AU/VST3/Standalone) checkboxes plus a system-vs-user install-location choice. Use whenever a new plugin is added under ~/code/audio-plugins/plugins, or whenever asked to add/fix installer support for an existing one — including registering it in the shared group installer at ~/code/audio-plugins/installers/, and in the download-counter Worker's PLUGIN_SLUGS allowlist, so it appears in both.
---

# Wild Jag plugin installer scaffolding

Every plugin under `~/code/audio-plugins/plugins/<plugin-repo>/` gets its own
`installer/` folder that builds a double-clickable macOS `.pkg`, and is also
registered in the shared group installer at `~/code/audio-plugins/installers/`.
**`plugins/caverns-delay/installer/` is the canonical reference implementation** —
copy its four files and substitute the per-plugin values below rather than
re-deriving the scripts from this description.
`~/code/audio-plugins/installers/README.md` documents the whole system end to
end; skim it once for context.

A new plugin isn't done until **both** of these exist:
1. Its own `installer/` folder (this repo's standalone installer).
2. An entry in the top-level `installers/` group installer.

## What every installer offers

Both the per-plugin and the group installer let the user pick, via
checkboxes:
- **Which formats to install** — Audio Unit, VST3, and/or Standalone App,
  independently.
- **Where to install** — Apple's native "Install for all users of this Mac"
  (`/Library/Audio/Plug-Ins/...`, `/Applications`, needs an admin password)
  vs. "Install for me only" (`~/Library/Audio/Plug-Ins/...`, `~/Applications`,
  no password). This is **not** a free-form folder browser — Audio Units are
  hard-restricted by macOS to exactly those two locations regardless of
  installer, so don't try to add arbitrary-folder picking; it would just
  produce an AU no host can find. It's implemented by building each format as
  its own *relocatable* component package (`pkgbuild --install-location`
  given as a path **without a leading slash**, e.g.
  `Library/Audio/Plug-Ins/Components`) plus
  `<domains enable_currentUserHome="true" enable_localSystem="true"/>` in the
  distribution XML, which is what lets Installer.app resolve that relative
  path against `/` or `~` at install time.

This means **each plugin produces three separate component packages** (one
per format), not one combined package — that's what makes independent
format checkboxes possible.

## Step 1 — per-plugin `installer/` folder

Copy from `plugins/caverns-delay/installer/` into the new plugin repo:

```
<new-plugin-repo>/installer/
  build.sh
  distribution.xml.in
  Resources/
    welcome.html
    license.txt
    conclusion.html
```

Then substitute these per-plugin values (grep the copied files for `Caverns`
and replace every occurrence — script/XML structure itself is identical
across all three existing plugins, don't restructure it):

| Placeholder | Where | Example (Corrosion / loss-drive repo) |
|---|---|---|
| `PLUGIN_NAME` | `build.sh` | The CMake target name from `juce_add_plugin(...)` in the plugin's `CMakeLists.txt` — **not** the repo folder name. `corrosion-drive`'s target is `Corrosion`. |
| `BUNDLE_ID` | `build.sh`, `distribution.xml.in` (`pkg-ref id`, suffixed `.au`/`.vst3`/`.standalone`) | `com.wildjag.<lowercase-plugin-name>` — matches `BUNDLE_ID` already set in the plugin's own `CMakeLists.txt` `juce_add_plugin(... BUNDLE_ID ...)`. Each format gets its own pkg identifier: `com.wildjag.corrosion.au.pkg`, `.vst3.pkg`, `.standalone.pkg`. |
| Component pkg filenames | `distribution.xml.in`'s three `<pkg-ref>` bodies | `Corrosion-AU-Component.pkg`, `Corrosion-VST3-Component.pkg`, `Corrosion-Standalone-Component.pkg` |
| `<title>`, three `<choice id>`/`title` | `distribution.xml.in` | Plugin display name + "Audio Unit (AU)" / "VST3" / "Standalone App" |
| Plugin description / paths | `Resources/welcome.html`, `conclusion.html` | Swap the plugin name and one-line description throughout |
| License text | `Resources/license.txt` | Just the plugin name in the header line — boilerplate body is identical |

**Never hand-write a version number in `distribution.xml.in`.** Its three
`<pkg-ref>` `version` attributes must be the literal token `__VERSION__`, not
`0.1.0` or any other literal — `build.sh` reads the real `VERSION` out of the
plugin's own `CMakeLists.txt` (`project(<Name> VERSION x.y.z)`) via
`scripts/plugin-version.sh` and generates the real `installer/output/distribution.xml`
from this template at build time (see `AGENTS.md` > Versioning). This is why
the file is named `distribution.xml.in` and not `distribution.xml` — the
`.in` marks it as a template, never consumed directly by `productbuild`.

After copying, `chmod +x installer/build.sh`.

`build.sh` now sources `../../installers/sign-and-notarize.sh` and signs each
staged bundle (`sign_bundle`) plus notarizes/staples the final `-Installer.pkg`
(`notarize_and_staple`) — nothing to add for a new plugin, it inherits this
automatically as long as `DEVELOPER_ID_APPLICATION`/`DEVELOPER_ID_INSTALLER`
are set to real Developer ID identities and either `NOTARY_PROFILE` or
`NOTARY_KEY_PATH`/`NOTARY_KEY_ID`/`NOTARY_ISSUER_ID` are set in the shell
(see `installers/README.md`). Set `SKIP_NOTARIZE=1` for a fast sign-only local
build while iterating.

Verify it builds before moving on:
```sh
cd <new-plugin-repo>
./installer/build.sh
```
This should produce `installer/output/<Name>-{AU,VST3,Standalone}-Component.pkg`
and `installer/output/<Name>-Installer.pkg`. Sanity-check each component
package's payload and install-location:
```sh
for fmt in AU VST3 Standalone; do
  f="installer/output/<Name>-${fmt}-Component.pkg"
  rm -rf /tmp/pkgcheck && pkgutil --expand "$f" /tmp/pkgcheck
  echo "$fmt: $(grep -o 'install-location="[^"]*"' /tmp/pkgcheck/PackageInfo)"
  rm -rf /tmp/pkgcheck
done
```
Expect `Library/Audio/Plug-Ins/Components` (AU), `Library/Audio/Plug-Ins/VST3`
(VST3), `Applications` (Standalone) — **relative, no leading slash**. A
leading `/` here means the package will always install system-wide, silently
defeating the "install for me only" choice — check this every time, it's an
easy typo to introduce when hand-editing `pkgbuild --install-location`. Also
confirm the payload actually contains the bundle:
```sh
pkgutil --payload-files "installer/output/<Name>-AU-Component.pkg" | grep '\.component$'
```
pkgbuild silently packages whatever exists under `installer/stage/<fmt>/` —
if the Release build didn't produce that artefact, the pkg still builds
successfully but ends up empty or wrong; a clean exit code alone doesn't
confirm anything.

## Step 2 — add `.gitignore` entries in the new plugin repo

The three existing plugin repos already ignore installer build output; make
sure the new one does too (append to its `.gitignore`, don't create a
separate ignore file):

```gitignore
# Installer build output
installer/stage/
installer/output/
*.pkg
```

## Step 3 — register the plugin in the group installer

Edit four files under `~/code/audio-plugins/installers/` (**not** inside the
plugin's own repo):

1. **`build-all.sh`** — add `"plugins/<plugin-repo>:<Name>"` to the `for entry in
   ...` list. The loop already builds `--component-only`, copies all three
   format packages, and extracts that plugin's version (via
   `scripts/plugin-version.sh`) into the `sed_args` array consumed by the next
   two files — no per-plugin script duplication needed.

2. **`distribution.xml.in`** — this is a two-level tree: one group `<line
   choice="choice_<name>">` (containing three nested `<line>`s for
   `choice_<name>_au`/`_vst3`/`_standalone`) in `<choices-outline>`, plus four
   `<choice>` elements (the group one has **no** `<pkg-ref>` child — it's a
   pure container whose checkbox toggles its three children — and the three
   leaf choices each reference one `<pkg-ref>`), plus three `<pkg-ref>`
   entries at the bottom referencing the three `-Component.pkg` filenames.
   Copy an existing plugin's whole block (group choice + 3 leaf choices + 3
   pkg-refs, ~15 lines) and rename every `caverns`/`Caverns` occurrence — **and**
   change each `<pkg-ref>`'s `version="0.1.0"`-shaped literal to
   `version="__<NAME_UPPER>_VERSION__"` (e.g. `__CORROSION_VERSION__`),
   matching the token `build-all.sh`'s `sed_args` array will generate for this
   plugin. Like the per-plugin `distribution.xml.in`, this file is a
   template — never hand-write a real version number into it.

3. **`Resources/welcome.html.in`** — add one `<li>` to the plugin list using
   the same `__<NAME_UPPER>_VERSION__` token, e.g.
   `<li><b>Corrosion</b> v__CORROSION_VERSION__ - lo-fi drive</li>`.

4. **`README.md`** — add a row to the plugin table.

Then rebuild and verify the combined installer still produces the right
choice count and every component is relocatable:
```sh
cd ~/code/audio-plugins/installers
./build-all.sh
rm -rf /tmp/wj-check && pkgutil --expand output/WildJagPlugins-Installer.pkg /tmp/wj-check
grep -c '<choice id=' /tmp/wj-check/Distribution   # 4 per plugin (1 group + 3 formats)
for f in /tmp/wj-check/*.pkg; do echo "$(basename "$f"): $(grep -o 'install-location="[^"]*"' "$f/PackageInfo")"; done
rm -rf /tmp/wj-check
```

## Step 4 — register the plugin in the download counter

Add the new plugin's lowercase slug (same `lowercase(<Name>)` convention
used everywhere else — the CMake project Name lowercased, e.g. `Corrosion`
→ `corrosion`) to the `PLUGIN_SLUGS` array in
`~/code/audio-plugins/download-counter/src/index.ts`. That allowlist is
deliberately hardcoded (the Worker has no access to the repo's
`CMakeLists.txt` at request time) and gates which `/dl/<slug>/<format>`
routes the Worker will actually redirect — an unlisted slug 404s instead of
guessing a filename.

After editing, redeploy from that directory:
```sh
cd ~/code/audio-plugins/download-counter
npx tsc --noEmit
npx wrangler deploy
```
Then sanity-check the new route before moving on:
```sh
curl -I "https://wildjag-downloads.mr-adambeck.workers.dev/dl/<slug>/pkg"
```
Expect a `302` to `.../releases/latest/download/<Name>-Installer.pkg` — not
a `404`.

Note: as of this writing the live site's download buttons still link
directly to GitHub, not through this Worker (a deliberate choice made when
the Worker was first built — see git history for `download-counter/`). If
that changes, a new plugin's site card needs its `href`s pointed at
`/dl/<slug>/...` too, same as every other plugin's.

## Things that will silently go wrong if skipped

- **Forgetting Step 3.** The per-plugin installer will work fine on its own,
  but the plugin won't show up as a checkbox in `WildJagPlugins-Installer.pkg`
  — nothing errors, the group installer just quietly ships without it.
- **Forgetting Step 4.** Same silent-failure shape as Step 3, but for
  downloads instead of installation: nothing errors, `/dl/<slug>/<format>`
  just 404s for that plugin until its slug is added to `PLUGIN_SLUGS`.
- **An absolute (leading-`/`) `--install-location`.** Locks that format to
  system-wide installation regardless of what the user picks in the "install
  for me only" vs "all users" choice — no error at build or install time,
  the format just always lands in `/Library/...` or `/Applications`. Always
  re-check with the `pkgutil --expand` + grep snippet above after editing.
- **Using the repo folder name instead of the CMake target name** for
  `PLUGIN_NAME`. They differ for `corrosion-drive` (target `Corrosion`) and
  could differ for a future plugin too — always read the actual `project(...)` /
  `juce_add_plugin(...)` name out of the plugin's `CMakeLists.txt`, don't
  assume it matches the directory.
- **A stale Debug-only build, or a missing staged bundle.** `build.sh` builds
  `--config Release` itself, so this shouldn't happen from a clean run, but
  pkgbuild's "Inferring bundle components" step will quietly package whatever
  happens to exist under `installer/stage/<fmt>/` — verify payload contents
  (Step 1's `pkgutil --payload-files` check) rather than trusting a
  successful exit code alone.
