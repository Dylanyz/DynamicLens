# Can you install a new DLL while the editor is running?

Short answer: **technically yes, but do not make it the default.** Ask Dylan and offer the safe path
first.

## Why overwriting fails and renaming does not

Windows will not let you overwrite a DLL that a process has loaded. But the loader opens DLLs with
`FILE_SHARE_DELETE`, so you *can* rename or delete one. A rename only changes the directory entry;
the file data and the running process's mapping of it are untouched. So:

1. `Rename-Item UnrealEditor-DynamicLens.dll -> UnrealEditor-DynamicLens.dll.old-<stamp>`
2. Copy the new DLL into the now-free name.
3. The running editor keeps executing the old image, including pages it demand-loads later, because
   the mapping follows the file object, not the path.
4. The next launch loads the new file. No restart is forced on anyone.

Check `UnrealEditor.modules` on both sides first. When the `BuildId` matches, the new DLL is
compatible with the installed engine and the next launch will not prompt to rebuild.

## The real risk, and why the safe path is the default

The already-running code is fine. The danger is **anything that re-resolves the module by path
mid-session**, which would load the new DLL into a process running the old one. In practice that
means **Live Coding** (Ctrl+Alt+F11), or any plugin/module reload.

Live Coding is part of Dylan's workflow on this machine:
his `EditorPerProjectUserSettings.ini` shows `LiveCoding.LastCompileMethod=External` and
`LiveCodingSettings=True`. So the risk here is not theoretical, and a crash would cost him unsaved
shot work. That is a bad trade for saving one command.

**Default to this instead:** leave the build in `%TEMP%\dlb`, tell him it is waiting, and give him
the one command to run himself once he has closed the editor:

```
Tools\build_dynamiclens.ps1 -InstallOnly
```

Zero risk, no waiting on an agent, and it refuses to run if the editor is still up.

## If he explicitly asks for the hot swap

Then do it, and tell him plainly: do not use Live Coding for the rest of the session. Rename aside
rather than delete, keep the `.old-<stamp>` files until he has relaunched successfully, and clean
them up only with his say-so. Swap all three files together (`.dll`, `.pdb`, `.modules`) so symbols
and the module manifest stay consistent with each other.
