---
name: wildjag-plugin-version-bump
description: Bump a Wild Jag plugin's version (the project(...VERSION...) line in its CMakeLists.txt), whenever finalizing a commit or PR that changes a plugin's Source/ or CMakeLists.txt. Use PROACTIVELY and automatically before such a commit — don't wait to be asked; this repo has no git hooks, so this skill is the pre-commit-equivalent step. The build-and-test.yml version-guardrail CI job is a mechanical backstop for when this skill is skipped, not a substitute for it. The marketing site's version badge is no longer part of this skill — sync-site-versions.yml updates it automatically from CMakeLists.txt on every push to main.
---

# Wild Jag plugin version bumping

Every plugin's version lives in exactly one place — `project(<Name> VERSION
X.Y.Z)` in `<plugin>/CMakeLists.txt`. Everything else (each installer's
`distribution.xml`, the group installer's welcome screen, the marketing
site's version badge) is generated or updated *from* that value — never
hand-edit a version number anywhere else.

This applies **per plugin, independently** — a PR touching two plugins gets
two independent classifications and, if warranted, two independent bumps.
Not every change needs a bump: read Step 2 before touching anything.

## Step 1 — identify touched plugins

```sh
git diff --name-only <base>...HEAD
```

Group changed paths by top-level plugin directory. Only `<plugin>/Source/**`
and `<plugin>/CMakeLists.txt` changes are version-relevant — installer-script
edits, README changes, or CI workflow edits for that plugin don't need a
version bump on their own (see the patch-level examples below though; some
installer/build-only changes still warrant a patch bump, judgment call).

## Step 2 — classify each plugin's change

All plugins are currently pre-1.0 (`0.y.z`). Per semver's own stated
convention for `0.y.z` — "anything MAY change" — this skill uses a
two-tier rubric instead of three:

| Bump | When |
|---|---|
| **patch** (x.y.Z) | DSP bug fix with no behavior/parameter change · performance/allocation fix with identical output · UI-only visual change (no new/removed controls) · installer/build-system-only change · test-only change |
| **minor** (x.Y.0) | New parameter added · changed default value · DSP algorithm change with audible but non-breaking behavior · expanded parameter range · renamed/removed parameter (breaks saved automation — would be *major* post-1.0) · `BUNDLE_ID`/`PLUGIN_CODE`/`PLUGIN_MANUFACTURER_CODE` change (breaks host identity — a DAW sees it as a different plugin) · incompatible preset/state format |

Both "additive feature" and "breaking-but-pre-1.0" collapse onto the same
minor-digit bump — that's expected, not a bug in the rubric. When several
changes of different weight touched one plugin in the same diff, classify by
the *highest* one present.

**Never bump to `1.0.0` unprompted.** A real 1.0.0 is a deliberate, manual
decision the user makes when they judge a plugin stable/feature-complete —
not something this skill infers from a diff.

**Never touch `WILDJAG_RELEASE_CHANNEL`** (the `set(WILDJAG_RELEASE_CHANNEL
"stable")` line next to `project(...)`) unless the user has explicitly asked
for a beta designation on this change. Channel is a human decision about
readiness for testers, not something derivable from what the diff touches.

If a rename changes `BUNDLE_ID`/`PLUGIN_CODE` such that the plugin has no
prior released identity under the new name (e.g. renaming the whole product,
as opposed to giving an existing shipped plugin a new bundle ID) — there's
nothing to bump *from*; leave the fresh `0.1.0` as-is rather than inventing a
bump for a product that hasn't shipped under that identity yet.

## Step 3 — bump `CMakeLists.txt`

Edit the `project(<Name> VERSION X.Y.Z)` line in the plugin's `CMakeLists.txt`
per the Step 2 classification. That's the only file this skill touches — the
installer's `distribution.xml` (generated from a `.xml.in` template at build
time) and the root group installer's `distribution.xml`/`welcome.html`
(same) update themselves automatically the next time anyone runs
`installer/build.sh` or `installers/build-all.sh`. Hand-editing either
generated file would just be overwritten, or worse, drift from the real
source of truth — don't.

The marketing site's version badge (`~/code/audio-plugins-site`, a worktree
checked out to `gh-pages`) is **not** touched here either — once this commit
reaches `main`, `.github/workflows/sync-site-versions.yml` reads the new
`CMakeLists.txt` and pushes the matching `.version`/`.badge-wip`/
`.badge-beta` update to `gh-pages` on its own (a `beta` channel always wins
over the WIP badge — never both). Don't hand-edit `index.html`
as part of a version bump; that would just be overwritten (or drift ahead of
the source of truth) on the next sync run.

## Step 4 — the CI guardrail is a backstop, not something to skip toward

`build-and-test.yml`'s `version-guardrail` job fails a PR if a plugin's
`Source/`/`CMakeLists.txt` changed but its `project(...VERSION...)` string
didn't move between base and head. If this skill ran correctly, that check
should always be a no-op green pass — a red `version-guardrail` means this
skill was skipped (or its Step 3 edit didn't land), not a separate problem to
solve independently. Fix it by going back to Step 3, not by editing CI.

## Out of scope (don't do these as part of a version bump)

- **Cutting a GitHub Release or tag.** The existing rolling `latest` release
  process (`.github/workflows/release.yml`) is untouched by this skill and
  publishes automatically on every push to `main` regardless of version
  numbers.
- **Hand-editing any `distribution.xml` or `welcome.html`.** Both are
  generated from `.xml.in`/`.html.in` templates at build time (see
  `wildjag-plugin-installer`'s skill for the templating mechanism) — editing
  the generated file is always wrong post-templatization.
- **Hand-editing the site's `index.html` version badge.** Generated from
  `CMakeLists.txt` by `sync-site-versions.yml` on every push to `main` — same
  reasoning as the installer files above.
- **Versioning the combined `WildJagPlugins-Installer.pkg` itself.** It has
  no version of its own by design — its welcome screen lists each bundled
  plugin's real version instead of inventing a meaningless aggregate number.
