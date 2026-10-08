# Copyright (c) 2026 Dylan Gitalis. Source-available under CPAL-1.0 with the Commons Clause; see LICENSE.
# SPDX-License-Identifier: CPAL-1.0 AND LicenseRef-Commons-Clause-1.0
"""
Plays a Level Sequence in the editor and logs every editor frame's duration with the Sequencer frame it showed, to
find hitches at camera cuts. Returns immediately; the log is written when playback reaches the end.

    cut_frametime_log.py <sequence path> <out .csv> [--passes 2]

Each pass plays the whole playback range once from the start, with the viewport locked to the camera cuts.
CSV columns: pass, editor frame, delta ms, sequencer display frame.
"""
import argparse
import sys
import time

import unreal

parser = argparse.ArgumentParser()
parser.add_argument("sequence")
parser.add_argument("out")
parser.add_argument("--passes", type=int, default=2)
args = parser.parse_args(sys.argv[1:])

seqlib = unreal.LevelSequenceEditorBlueprintLibrary
ext = unreal.MovieSceneSequenceExtensions
seq = unreal.load_asset(args.sequence)
seqlib.open_level_sequence(seq)
seqlib.set_lock_camera_cut_to_viewport(True)
end = ext.get_playback_end(seq)
state = {"pass": 0, "rows": [], "last": None, "n": 0, "settle": 30, "playing": False}


def start_pass():
    seqlib.pause()
    seqlib.set_current_time(ext.get_playback_start(seq))
    state["settle"] = 30          # let the first spawn and any streaming finish before timing
    state["playing"] = False


def tick(dt):
    if state["settle"] > 0:
        state["settle"] -= 1
        if state["settle"] == 0:
            seqlib.play()
            state["playing"] = True
            state["last"] = time.perf_counter()
        return
    now = time.perf_counter()
    frame = seqlib.get_current_time()
    state["rows"].append((state["pass"], state["n"], (now - state["last"]) * 1000.0, frame))
    state["last"] = now
    state["n"] += 1
    if not seqlib.is_playing() or frame >= end - 1:
        state["pass"] += 1
        if state["pass"] >= args.passes:
            seqlib.pause()
            unreal.unregister_slate_post_tick_callback(handle)
            with open(args.out, "w") as f:
                f.write("pass,n,ms,frame\n")
                f.writelines(f"{p},{n},{ms:.2f},{fr}\n" for p, n, ms, fr in state["rows"])
            unreal.log(f"cut_frametime_log: wrote {len(state['rows'])} rows to {args.out}")
        else:
            start_pass()


start_pass()
handle = unreal.register_slate_post_tick_callback(tick)
