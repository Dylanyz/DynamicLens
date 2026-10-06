# Maintenance — keeping these docs true

Last updated: 2026-10-06

DynamicLens is under active development, so `CLAUDE.md`, `.claude/rules/` and `.claude/refs/` go
stale faster than the code. After any session that changed the plugin or learned something about it,
run the shared wrap-up checklist in the plugin hub (`refs/maintenance.md`: README and refs agree,
Dylan's rules, script `Verified:` lines, prune, project facts stay out), then these DynamicLens items.
Both are mandatory.

## DynamicLens checklist

1. **New control added?** Add it to `using-the-component.md` with what it is *for*, not just
   its range. Dylan describes looks, not parameters, so the docs should too.
2. **New engine internal discovered** — an Epic behaviour that had to be worked around — goes in the
   "Engine internals this fights" section of `architecture.md`, together with the symptom that
   revealed it (an engine-true fact also goes to `/ue-docs`). These are the expensive facts, and the
   ones most likely to be "simplified" away by a later agent who does not know why the workaround is there.
3. **New third-party data source?** `NOTICE` in the same commit, as a credit and as an excluded
   path, then mirror it in `presets-and-profiles.md` and `SOURCES.md`. See `../rules/licensing-and-credits.md`.
4. **Which preset a given film used** belongs in that film project's own `.claude/`.

## Watch list

- **Engine version.** Everything here is UE 5.8. Moving to 5.9 needs a new junction, a rebuild,
  and a re-verify of the Epic internals in `architecture.md`, the most version-fragile part.
- **`CameraCalibrationCore`.** If Epic fixes the displacement-value rescale in
  `BlendDisplacementMaps.usf`, or raises the default displacement map resolution, both the module's
  startup fixup and the camera-frame-units workaround can be retired.
- **Open items Dylan has raised but that are not done:** asymmetric overscan for fisheyes, tiedtke
  zoom lenses, an anamorphic parametric fit, and polish for a possible Fab release.

## Log

- 2026-10-06 — **Moved into the plugin hub** (`Desktop\Coding\ueplugins\DynamicLens`, junction re-pointed).
  The shared build/install, editor-restart and update-runbook rules and `refs/hot-swap.md` were removed
  (now plugin hub `refs/` and `/ue-agent-control` `launch-close.md`); the DynamicLens-only parts (what is
  live without a build, the post-install re-imports, the failure rows) moved into `CLAUDE.md` "Iterating".
  `Tools/build_dynamiclens.ps1` regenerated from the shared template (gains the `BuildId` check). The
  .NET installer command in the old rule had a corrupted path (`\v` stored as a vertical-tab byte);
  the plugin hub copy is the verified one.

- 2026-09-25 — **v0.8.0 released.** Headline: the Preset Browser, and the fisheye rework (one preset per
  lens, Image Circle Scale + Field, dynamic fisheye overscan, the distortion-map fix). Browser documented in
  README (with `Docs/images/preset-browser.png`), `using-the-component.md` and `presets-and-profiles.md`.
- 2026-09-15 — **v0.7.0 released.** First public release, prebuilt Win64 binaries attached. README
  install section rewritten; the old one pointed at an absolute path on one machine and promised
  binaries that .gitignore excludes, so nobody else could install it. Descriptor bumped to 0.7.0
  with docs, support and author URLs filled in.
- 2026-09-15 — Tested and rejected removing `"Installed": true`: it does not block a project-plugin
  build, so it was never what blocked collaborators. Engine-wide layout kept; reasoning and the
  measured comparison are in refs/live-coding.md.


- 2026-09-06 — Plugin design settled: handler in Manual mode, no Lens Files on disk.
- 2026-09-07 — Superellipse data mask, per-texel ST-map validity, Image Circle Scale and Chromatic
  Amount built. That build was never installed and sat in `%TEMP%\dlb`.
- 2026-09-11 — Repo made public; Apache-2.0 with `LICENSE`, `NOTICE` and per-file SPDX headers,
  tiedtke's and Andy Davis's data carved out.
- 2026-09-15 — **Builds were blocked, now fixed.** Was: no .NET Framework SDK, so UBT cannot
  instantiate SwarmInterface and `BuildPlugin` fails with a RulesError before compiling. Fixed by adding the .NET
  Framework 4.8 SDK component to VS 18 Build Tools (exact command now in the plugin hub's refs/build-install.md). Also on this date a
  build attempt destroyed the waiting 09-07 package, because the script wiped the package dir
  before building; it now stages and swaps only on success. The 09-07 work (Image Circle Scale,
  Chromatic Amount) was rebuilt the same day from source and is waiting to be installed.
  Clean build measured at 76 s total, 12 s of which is the compile.
- 2026-09-15 — Docs moved into the repo as `CLAUDE.md` + `.claude/`. They were briefly a user-scope
  `/dynamiclens` skill, retired the same day at Dylan's request so the knowledge lives with the code
  and ships with the open-source repo. Build script moved to `Tools/`, made self-locating and
  engine-detecting. Hard rule recorded: never restart the editor without asking. The hot-swap route
  was investigated and rejected as the default because Live Coding is in use on this machine.
