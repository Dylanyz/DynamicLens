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

## Panavision C 30 mm map is sample-identical to the 20 mm one

Found 2026-10-08 (`UDynamicLensLibrary.ReadSTMapSamples` 9x5 on both, every value equal; their extended maps give the same
overscan). tiedtke's own lens files point at different textures (`Panavision_C_Series_2x_20mm` / `_30mm`, the 30 mm
lens file is named `..._30mm_`), and his pack's two `.uasset`s differ in size only by what the names would. So it is
probably in his data, not our import. Check: load both pack textures in a project that has the pack (search for
`Content/Lenses/2x/Panavision_C_Series_2x`) and compare samples. If his data, tell Dylan (a film
in production shoots this lens at 30 mm) and tiedtke; if ours, re-import the 30 mm.
