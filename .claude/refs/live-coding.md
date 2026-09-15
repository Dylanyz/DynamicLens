# Live Coding and the engine-vs-project plugin question

The honest answer to "can we update the plugin without restarting the editor?", with what was
actually tested on 2026-09-15 rather than assumed.

## What Live Coding does and does not do

Unreal's Live Coding (Ctrl+Alt+F11) recompiles changed C++ and patches it into the running editor.

**It handles:** changes inside existing function bodies. Maths, thresholds, branching, fixes to an
existing driver. A large share of the tuning work on this plugin, and it patches in seconds.

**It cannot handle:** adding or removing a `UPROPERTY` or `UFUNCTION`, changing a class's layout, or
adding new reflected types. Unreal fixes reflection data and object layout when a module loads, and
nothing rewrites them in a live process.

That second list is the problem, because **almost every feature requested so far is a new control**,
which means a new `UPROPERTY`: Image Circle Scale, Chromatic Amount, the Fade group, the wobble and
noise controls, the bokeh blade and squeeze sources. Every one needs a restart in any layout. There
is no configuration that avoids it.

## Tested: `"Installed": true` does NOT block a project-plugin build

This was the assumption behind a proposal to strip the flag so collaborators could build the plugin
from source. **It is wrong, and the flag was therefore left alone.**

A throwaway Blueprint-only project was built with this plugin copied into its `Plugins` folder, the
flag left at `true`, and `Build.bat UnrealEditor Win64 Development -Project=...` run against it.
Unreal Build Tool accepted the plugin and proceeded to UnrealHeaderTool and compilation, writing
`Intermediate/Build/.../DynamicLens/` inside the project. What gates compilation is the plugin's
*location* (an installed engine's `Engine/Plugins` is read-only), not `bInstalled`.

So collaborators were never blocked by the descriptor. They were blocked by a README that pointed at
an absolute path on one machine and promised binaries that `.gitignore` excludes. That is fixed, and
a release with prebuilt binaries now exists.

What the flag *does* affect is engine-version strictness: with `Installed: true` and
`EngineVersion: 5.8.0`, a different engine version is refused. That is honest for a plugin that
genuinely targets 5.8, so it stays.

## Tested: you cannot run an external project build while the editor is open

The same test, run with the editor up, failed with:

```
Unable to build while Live Coding is active. Exit the editor and game,
or press Ctrl+Alt+F11 if iterating on code in the editor or game
```

This matters for comparing the two layouts, and it cuts against the project-plugin option:

| | Engine plugin (today) | Project plugin |
|---|---|---|
| Build while the editor is open | **yes**, `RunUAT BuildPlugin` uses its own host project | no, Live Coding holds the lock; you use Ctrl+Alt+F11 instead |
| Body-level change without restarting | no | **yes**, via Ctrl+Alt+F11 |
| New `UPROPERTY` | restart | restart |
| Projects needing a toolchain | none, built once centrally | every project carrying the plugin |

## Why the engine layout stays

21 of the 22 Unreal projects on this machine are Blueprint-only; only CitySample is C++. Making the
plugin a project plugin would turn each of those into a code project that prompts to compile on
first launch and after every engine hotfix, and refuses to open without a working toolchain. Against
that, the gain is hot-patching for the subset of changes that are body-only.

Measured build cost in the current layout: **76 s** for a full clean package, **12 s** of which is
the compile. Build time is not the friction. The restart is, and the restart is unavoidable for the
changes that actually get made.

**Decision, 2026-09-15: keep the engine-wide junction.** Revisit only if the work shifts toward
tuning existing maths rather than adding controls. Do not half-do it: a project that sees the plugin
at both an engine path and a project path fails to load it.

## What was streamlined instead

`Tools\build_dynamiclens.ps1 -Status` answers "where is this up to and what is next" in one
read-only call. The runbook is `../rules/updating-the-plugin.md`, and its first step is the useful
one: most updates are preset, profile, material or Python changes, which are already live with no
build and no restart at all.
