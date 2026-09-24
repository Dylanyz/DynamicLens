# TODO — small, unblocked, nobody is waiting on a decision

The counterpart to `roadmap.md`. That file holds work that is researched but **gated**, and its rule
is *propose, do not start*. This file is the opposite: short jobs that are already decided, where the
right move is to pick one up and do it.

Same disposal rule as the roadmap: **delete an entry when it is done**, do not tick it off. Git
history is the record.

Anything here that touches `Source/` still needs a build, and installing still needs Dylan to close
the editor himself — `.claude/rules/editor-restarts.md`. Batch C++ items so he restarts once.

---

## Do this first: clear the Preset Browser's pending install

There is a built package on disk that has never been installed, and the editor is running an older
DLL than HEAD. Full detail, including why each step matters, is in the Preset Browser section of
`roadmap.md`. The short version:

1. Ask Dylan to close the editor, then:
   ```powershell
   powershell -ExecutionPolicy Bypass -File Tools\build_dynamiclens.ps1 -InstallOnly
   ```
   The bypass is because his interactive shell refuses unsigned scripts. Calls made through the
   PowerShell tool do not need it.
2. After he relaunches: `import dynamiclens_tools as dl; dl.resave_presets()`
3. Open **Window > Cinematics > Dynamic Lens Preset Browser** and get his eyes on the layout.

Until step 1 happens, **do not start anything else that touches `Source/DynamicLens`** — a second
build would overwrite the waiting package, and `roadmap.md` gates the anamorphic parametric work on
this being cleared.

---

## Document the Preset Browser once it has been seen

It exists only in `roadmap.md` right now, which is the wrong home for a shipped feature. Once the UI
has actually been looked at and is not about to change shape:

- `README.md` — it is a user-facing feature and the public should know it is there.
- `.claude/refs/using-the-component.md` — the **Browse** button beside the Preset field.
- `.claude/refs/presets-and-profiles.md` — how the `DL.*` Asset Registry tags are produced and what
  each one means, and the rule that browsing must never load a preset.
- `CLAUDE.md` — one line under "Using it on a camera", step 3.

Deliberately not done yet: the layout is unreviewed, so anything written now risks describing a UI
that changes. `dl.resave_presets()` is already in the CLAUDE.md table because it is needed to
*finish the install*, not because the feature is documented.

---

## `FCoreDelegates::OnPostEngineInit` is deprecated

`Source/DynamicLens/Private/DynamicLensModule.cpp:21` warns C4996 on every build:

```
'FCoreDelegates::OnPostEngineInit': OnPostEngineInit has been deprecated.
Please use GetOnPostEngineInit() instead.
```

Epic's own note says this stops compiling in the next release. One-line change to
`FCoreDelegates::GetOnPostEngineInit().AddLambda(...)`. It is the only warning the plugin produces,
so fixing it gets the build back to clean and makes a real warning visible when one appears.

Cheap, but it is C++ — fold it into whatever build goes out next rather than spending a restart on
it alone.
