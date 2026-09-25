# Editor restarts — always ask, never assume

## The rule

**Never restart, close or relaunch the Unreal editor without asking Dylan first and getting an
explicit yes.** Offer to do it. Do not do it unprompted. If he prefers to close it himself, let him.

His words, 2026-09-15: *"it shouldnt always assume it can restart the engine. sometimes ill be
working on things in there. so it should ask me about that."* Twice the week before: *"dont restart
the editor!"* and *"dont restart for that too! ill get them on next restart."*

**Why:** he is usually mid-shot with unsaved work, and on a big level a relaunch costs minutes plus
his viewport and selection state. The cost of waiting is zero, because a built plugin package can
sit in `%TEMP%\dlb` for days with no harm.

## How to apply

1. Check before anything that needs the editor down: `Get-Process UnrealEditor`.
2. Do every part of the job that does *not* need a restart. Content, Python, presets, profiles,
   docs and commits all work against a live editor.
3. For the part that does, stop and say plainly what is built and what it changes, and ask one
   question: is the editor free so you can restart it and install?
4. **Never tell him to run the install command himself.** His words, 2026-09-25: *"im not gonna run
   it myself!!!!!! never tell me to do that please."* The install is your job.
5. On a yes: save dirty packages, quit the editor (`unreal.SystemLibrary.quit_editor()` via remote
   exec), run `Tools\build_dynamiclens.ps1 -InstallOnly`, relaunch the same `.uproject` (read the
   command line off the running process first), wait for init, re-run dependent Python, verify.
6. On a no or no answer, "built, waiting until the editor is free" is a complete end state. Do not
   keep asking.

`Tools/build_dynamiclens.ps1` enforces this in code: `-Install` and `-InstallOnly` throw if
`UnrealEditor.exe` is alive rather than corrupting a locked DLL.

## Do not reach for the clever workaround

A new DLL *can* be swapped in under a running editor by renaming the loaded one aside. It is not
the default here, because Live Coding is part of Dylan's workflow on this machine and would load the
new DLL into a process running the old one. The reasoning and the conditions under which it is
acceptable are in `.claude/refs/hot-swap.md`. Read that before offering it.

## Related

Waiting for a *freshly launched* editor is a different thing and is fine to do unattended: wait for
`Engine Initialization) Total time` in the project's `Saved/Logs/<Project>.log` before sending any
Python. Calling in early looks like a dropped connection. Never call `load_level` during startup.
