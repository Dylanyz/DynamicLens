# TODO — small, unblocked, nobody is waiting on a decision

The counterpart to `roadmap.md`. That file holds work that is researched but **gated**, and its rule
is *propose, do not start*. This file is the opposite: short jobs that are already decided, where the
right move is to pick one up and do it.

Same disposal rule as the roadmap: **delete an entry when it is done**, do not tick it off. Git
history is the record.

Anything here that touches `Source/` still needs a build, and installing still needs Dylan to close
the editor himself — `.claude/rules/editor-restarts.md`. Batch C++ items so he restarts once.

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
