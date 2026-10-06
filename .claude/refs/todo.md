# TODO — small, unblocked, nobody is waiting on a decision

The counterpart to `roadmap.md`. That file holds work that is researched but **gated**, and its rule
is *propose, do not start*. This file is the opposite: short jobs that are already decided, where the
right move is to pick one up and do it.

Same disposal rule as the roadmap: **delete an entry when it is done**, do not tick it off. Git
history is the record.

Anything here that touches `Source/` still needs a build, and installing needs the editor closed (ask
Dylan whether it is free; the agent then closes, installs and relaunches: `/ue-agent-control`). Batch C++
items so he restarts once.

---

## Six `DL_T_*` anamorphics crop at the default overscan ceiling

Cineovision (0.75), Todd-AO, E Series, Elite MK, D Series and Hawk show a rounded-rectangle crop that
is just the render running out at Max Overscan 1.5; 2.0 removes it (`.claude/refs/image-circle-guide.md`).
It changes the look of shipped presets, so **ask Dylan first**, then set it where the tiedtke presets
are generated and re-import.

## Set CreationMethod = Instance when the library adds the component (from BlackEyeCustom, 2026-10-06)

`DynamicLensLibrary.cpp:24-26` adds the component with `NewObject` + `AddInstanceComponent` + `RegisterComponent` but
leaves `CreationMethod` at its default, Native. Tools that copy "user-added" components by `CreationMethod` skip it
(BlackEyeCustom's Fast Bake had to also check the instance-component list). One line before `AddInstanceComponent`:
`Comp->CreationMethod = EComponentCreationMethod::Instance;` (what the editor's Add Component button does). C++,
so build + install with the next batch.
