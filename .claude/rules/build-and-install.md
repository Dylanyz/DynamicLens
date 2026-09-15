# Build and install — when X, do Y

The full recipe, the AutoSDK quirk and the failure table live here; the ask-first gate is
`editor-restarts.md`.

## The cycle

```powershell
Tools\build_dynamiclens.ps1              # build only, packages to %TEMP%\dlb, safe with the editor up
Tools\build_dynamiclens.ps1 -InstallOnly # install, only after Dylan has closed the editor
```

Build first, always. Then ask. Then install. Then he relaunches. Then re-run dependent Python.

## What the script does, and why each part exists

- **Packages to a temp dir** rather than building in place, so a running editor never blocks a build.
- **Derives the repo from its own location** (`<repo>\Tools`) and picks the engine from the
  `EngineVersion` in `DynamicLens.uplugin`, falling back to the newest `UE_*` install. No
  machine-specific paths, because this repo is public.
- **Sets `UE_SDKS_ROOT` to a writable stub** when it is unset, which quiets Unreal Build Tool's
  AutoSDK probe for platforms we do not target. What it probed is logged in
  `%LOCALAPPDATA%\UnrealEngine\5.8\Saved\Logs\AutoSDKInfo.txt`. **The stub does not substitute for
  the .NET Framework SDK** — see the blocker below.
- **Builds into `<package>.new` and only swaps it into place on success**, so a failed build cannot
  destroy a good package that is waiting to be installed. An earlier version wiped the package dir
  first and did exactly that on 2026-09-15, losing the 09-07 build.

- **Installs only `Binaries\Win64` and `Intermediate\Build`**, by robocopy. Copying the whole
  package would overwrite `Content` and `Source` with the packaged copies and destroy uncommitted
  work. An early attempt that copied naively produced a nested `Binaries\Binaries`.
- **Checks `UnrealEditor.modules`.** A matching `BuildId` on both sides means the new DLL is
  compatible with the installed engine build and the next launch will not prompt to rebuild.

## Current blocker: no .NET Framework SDK on this machine

`BuildPlugin` fails before compiling anything with:

```
Unable to instantiate module 'SwarmInterface': Could not find NetFxSDK install dir;
Install a version of .NET Framework SDK at 4.6.0 or higher.
Result: Failed (RulesError)
```

Verified 2026-09-15: there is no `NETFXSDK` key under
`HKLM\SOFTWARE\[WOW6432Node\]Microsoft\Microsoft SDKs`, nothing under
`C:\Program Files (x86)\Windows Kits\NETFXSDK`, and the only toolchain installed is
**Visual Studio 18.4.0 Build Tools** without the .NET Framework SDK component.

**The fix** is to add that one component, in the Visual Studio Installer: Build Tools →
Modify → Individual components → **.NET Framework 4.8 SDK** (and the 4.8 targeting pack).
It needs admin and is Dylan's call. Until then **no C++ in this plugin can be built on this
machine**, and neither can CitySample's own modules if they ever need recompiling; both would hit
the same wall.

Because SwarmInterface is an editor-target dependency rather than anything to do with this plugin,
there is no flag that skips it. Do not try to fake a NetFxSDK inside the AutoSDK stub; it gets
further and then fails in the link.

## After installing, re-run the dependent Python

```python
import dynamiclens_tools as dl
dl.build_image_circle_material(force=True)   # the material HLSL changed
dl.import_presets()                          # preset fields were added or renamed
```

**A new field on a preset struct defaults to zero in existing assets.** If you added one whose
non-zero value carries the behaviour, `import_presets()` is *required* or every shipped preset
silently loses it. This is exactly what happened when chromatic aberration became an amount over
per-channel offsets: without the re-import the amount sat at 0 and every rim lost its colour fringe.

`dl.import_presets()` **overwrites** preset assets. If Dylan has hand-tweaked one in the editor, ask
before running it, and get the tweak into `presets.json` first.

## Failure modes

| Symptom | Cause | Fix |
|---|---|---|
| `Could not find NetFxSDK install dir`, RulesError | no .NET Framework SDK installed | add the .NET Framework 4.8 SDK component in the VS Installer; see the blocker section above |
| Link error, cannot write the DLL | editor running | close it, asking first |
| UBT fails registering build platforms | `UE_SDKS_ROOT` unset or bogus | the stub; the script handles it |
| Build succeeds, editor unchanged | package never installed | `-InstallOnly` |
| New property exists but does nothing | preset assets predate it | `dl.import_presets()` |
| Post-process chain goes blank after a material rebuild | orphaned parentless MID | `ClearEffect()`, or reselect the camera |
| `git pull --rebase` fails on a `.uasset`, "Invalid argument" | the editor holds the asset open | `git checkout HEAD -- .`, then merge rather than rebase |
