# "Update the plugin" — the runbook

When Dylan says **"update the plugin"**, "install the update", "push the new build", "get the
latest in", or opens a chat in this repo and asks for the changes to take effect, follow this
exactly. Do not improvise, and do not skip step 1.

## Step 0 — one call tells you where things stand

```powershell
Tools\build_dynamiclens.ps1 -Status
```

Read-only, safe with the editor running. It prints the newest source timestamp, the installed DLL,
any waiting build, whether the editor is up, and the single next action. Start here every time so
you never rebuild something already built or claim an install that never happened.

## Step 1 — is a C++ build even needed?

Most "updates" are not C++ and need no build and no restart:

| If the change is | Then |
|---|---|
| A preset or profile value, a new lens | edit `Tools/data/presets.json`, then `dl.import_presets()`. Done, live, no restart. |
| The image-circle look (HLSL) | `dl.build_image_circle_material(force=True)`. Done, live, no restart. |
| Editor tooling in `dynamiclens_tools.py` | re-import the module in the editor. Done, live. |
| A new control, new maths, anything in `Source/` | build, then install, which needs a restart. Continue below. |

Say which of these it is before doing anything. If it is not C++, you are finished in one step and
Dylan never has to close anything.

## Step 2 — build (safe while he works)

```powershell
Tools\build_dynamiclens.ps1
```

Packages to `%TEMP%\dlb`. The editor can stay open throughout. If the build fails, read the failure
table in `build-and-install.md` before retrying.

## Step 3 — stop and ask

**The install overwrites a DLL the running editor holds open, so the editor must be closed first.
Ask him. Wait for a yes. Never close it yourself.** See `editor-restarts.md` for why this is absolute.

Tell him three things in one short message: what the update changes, that it is built and waiting,
and the one command. Then stop.

```powershell
Tools\build_dynamiclens.ps1 -InstallOnly
```

He can run that himself whenever he next closes the editor, for any reason. He does not have to
close it now, and "I'll get it on my next restart" is a complete answer. Do not ask twice.

## Step 4 — after he relaunches

Re-run whatever the change depends on, then confirm it landed:

```python
import dynamiclens_tools as dl
dl.build_image_circle_material(force=True)   # only if the material HLSL changed
dl.import_presets()                          # REQUIRED if any preset field was added or renamed
dl.status()
```

**The re-import is not optional when a field was added.** A new field defaults to zero in existing
assets, so without it every shipped preset silently loses the new behaviour. That is exactly what
happened when chromatic aberration became an amount over per-channel offsets.

Wait for `Engine Initialization) Total time` in the project's `Saved/Logs/<Project>.log` before
sending any Python to a freshly launched editor.

## Step 5 — close the loop

- Verify in the editor, at more than one focal length, that the change does what he asked.
- Commit and push. The repo is public; see `licensing-and-credits.md`.
- Update `.claude/refs/` and `README.md` if behaviour changed, per `.claude/refs/maintenance.md`.

## Why there is no restart-free path for most changes

Adding a control means adding a `UPROPERTY`, which changes reflection data and class layout. Unreal
fixes both when the module loads, so a restart is unavoidable. Live Coding can patch function
bodies, but not add reflected members, and in this layout it cannot touch the plugin at all.
The full reasoning, and the restructure that would unlock hot patching for logic-only changes,
is in `.claude/refs/live-coding.md`. Do not propose the DLL rename trick instead; see
`.claude/refs/hot-swap.md`.
