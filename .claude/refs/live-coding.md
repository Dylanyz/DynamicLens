# Live Coding — what it could give us, and what it cannot

The honest answer to "can we update the plugin without restarting the editor?"

## What Live Coding does and does not do

Unreal's Live Coding (Ctrl+Alt+F11) recompiles changed C++ and patches it into the running editor.

**It handles:** changes inside existing function bodies. Maths, thresholds, branching, new local
logic, fixes to an existing driver. That is a large share of the tuning work on this plugin, and it
patches in seconds.

**It cannot handle:** adding or removing a `UPROPERTY` or `UFUNCTION`, changing a class's layout,
or adding new reflected types. Unreal fixes reflection data and object layout when the module
loads, and nothing can rewrite them in a live process.

That second list is the problem, because **almost every feature Dylan has asked for is a new
control**, which means a new `UPROPERTY`: Image Circle Scale, Chromatic Amount, the Fade group, the
wobble and noise controls, the bokeh blade and squeeze sources. Every one of those needs a restart
no matter how the build is plumbed. There is no configuration that avoids it.

## Why Live Coding cannot touch the plugin in the current layout

Two blockers, either of which is sufficient:

1. **It lives in the engine.** The junction puts it at
   `UE_5.8\Engine\Plugins\Marketplace\DynamicLens`. Unreal Build Tool treats anything under
   `Engine\Plugins` in an installed (launcher) engine as precompiled and read-only.
2. **`DynamicLens.uplugin` sets `"Installed": true`**, the marketplace flag, which tells the editor
   the binaries are shipped and not to be rebuilt.

So today Live Coding will not compile or patch this plugin at all, and the only route is
RunUAT plus a restart.

## The restructure that would unlock it

Make it a **project plugin** instead of an engine plugin: junction the repo into
`<project>\Plugins\DynamicLens` rather than into the engine, and drop `"Installed": true`.

**Gains:** Live Coding works for body-level changes, so logic and maths iterate with no restart.
Full rebuilds become an ordinary project compile instead of a RunUAT package plus a manual install,
which also removes the locked-DLL problem entirely.

**Costs:** one junction per project instead of one for the engine, so each new film project needs a
line adding. Any Blueprint-only project would become a code project and would prompt to compile on
first launch. CitySample is already a C++ project carrying ten source plugins, so it fits there with
no friction.

**Not done.** It changes how every project resolves the plugin and is Dylan's call, not a
maintenance decision. Raised 2026-09-15; he has not decided. Do not carry it out unasked, and do not
half-do it by leaving both junctions in place: a project that sees the plugin at an engine path and
a project path will fail to load it.

## What was streamlined instead

`Tools\build_dynamiclens.ps1 -Status` answers "where is this up to and what is next" in one
read-only call, so an agent never rebuilds needlessly or claims an install that did not happen.
The runbook is `.claude/rules/updating-the-plugin.md`, and the point of step 1 there is that most
updates are preset, profile, material or Python changes, which are already live with no build and
no restart at all.
