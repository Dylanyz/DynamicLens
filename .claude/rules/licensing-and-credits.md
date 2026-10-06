# Licensing and credits — what may and may not be licensed

The repo is **public** and **source-available: CPAL-1.0 + Commons Clause** (since 2026-10-06; earlier
releases Apache-2.0). `LICENSE` and `NOTICE` at the root are the authority.

## The three names that must always travel together

- **Dylan Gitalis** (youtube.com/@madricetv, github.com/Dylanyz/DynamicLens) — the plugin.
- **tiedtke** — the *Real Cinema Lenses* ST maps. https://tiedtke.gumroad.com/l/realcinemalenses
- **Andy Davis (Imagery for Media)** — the VFX RnD Lens Files distortion grids. https://imag4media.com/

`NOTICE` names all three and must travel with every distributed copy; CPAL Exhibit B makes Dylan's
credit a visible condition. Why this licence: plugin hub `refs/licensing.md`.

## The carve-out — never relicense someone else's measurements

The licence covers the source, tools, materials and the preset data written for this plugin. It does
**not** cover:

| Path | Belongs to |
|---|---|
| `Content/Profiles/Tiedtke/**` (Lens Files, profiles, textures) | tiedtke |
| `Tools/data/raw/**` and the `AD_*` grids fitted from it | Andy Davis |
| `Content/Profiles/AndyDavis/**` and `Tools/data/stmaps/**` | Andy Davis |

Those stay under their authors' own terms. **Do not add them to the licence, and do not write
anything implying Dylan can sublicense them.**

**Both authors have given Dylan permission to redistribute their data in this repo**, which is why
it all ships here and a clean clone rebuilds everything. Permission to *redistribute* is not
permission to *relicense* - the carve-out above still stands, and it does not extend to people who
fork the repo. Keep that distinction in `NOTICE`; it is the whole point of the section.

## When X, do Y

| Situation | Do |
|---|---|
| Adding a new source file | copy the three-line SPDX header verbatim from an existing file |
| Adding a lens from someone else's measured data | add them to `NOTICE` in the *same commit*, as a credit **and** as an excluded path; mirror it in `.claude/refs/presets-and-profiles.md` and `SOURCES.md` |
| Borrowing an *idea* - a paper, a blog post, someone's method | it still gets credited. `SOURCES.md`, with what was actually taken from it. Ideas are not data, so no `NOTICE` excluded path. |
| Tempted to commit a sample pack | don't. `.gitignore` covers it; keep it that way. |

The rules every one of Dylan's plugin repos shares (no machine paths or film names, never commit
binaries, ask before any licence change) are in the plugin hub's `refs/licensing.md`.
