# Copyright (c) 2026 Dylan Gitalis. Source-available under CPAL-1.0 with the Commons Clause; see LICENSE.
# SPDX-License-Identifier: CPAL-1.0 AND LicenseRef-Commons-Clause-1.0
"""
Probes "the Black Eye camera loses its focal length" with DynamicLens on it. Places (or reuses) a level Black Eye
camera with DynamicLens, then runs a list of edits one per second and logs the focal length every editor tick.

    focal_loss_probe.py <out .csv> [--preset <preset path>] [--focal 35] [--label DL_FocalProbe]

Edits, in order: idle, Black Eye Follow property, Black Eye LookAt property, camera aperture, DynamicLens
vignette multiplier, actor move (set_actor_location), actor move with teleport, idle.
CSV: tick, step, focal mm, camera overscan.
"""
import argparse
import sys

import unreal

parser = argparse.ArgumentParser()
parser.add_argument("out")
parser.add_argument("--preset", default="/DynamicLens/Presets/Tiedtke/DL_T_Panavision_C_Series")
parser.add_argument("--focal", type=float, default=35.0)
parser.add_argument("--label", default="DL_FocalProbe")
args = parser.parse_args(sys.argv[1:])

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cam = next((a for a in eas.get_all_level_actors() if a.get_actor_label() == args.label), None)
if cam is None:
    cam = eas.spawn_actor_from_class(unreal.BlackEyeCineCameraActor, unreal.Vector(-1500, -1200, 400))
    cam.set_actor_label(args.label)
    subject = next((a for a in eas.get_all_level_actors() if a.get_actor_label() == "Subject"), None)
    if subject:
        look = cam.get_editor_property("look_at")
        t = look.get_editor_property("target_0")
        t.set_editor_property("actor", subject)
        look.set_editor_property("target_0", t)
    unreal.DynamicLensLibrary.add_to_actor(cam, unreal.load_asset(args.preset))
cc = cam.get_cine_camera_component()
cc.set_editor_property("current_focal_length", args.focal)
dl = cam.get_components_by_class(unreal.DynamicLensComponent)[0]
look = cam.get_editor_property("look_at")
follow = cam.get_editor_property("follow")


def edit_follow():
    follow.set_editor_property("damping", follow.get_editor_property("damping"))   # PostEditChange on Follow


def edit_look():
    look.set_editor_property("field_of_view_damping", look.get_editor_property("field_of_view_damping") + 0.01)


def edit_aperture():
    cc.set_editor_property("current_aperture", 2.8 if cc.get_editor_property("current_aperture") != 2.8 else 4.0)


def edit_dl():
    dl.set_editor_property("vignette_multiplier", 1.0 if dl.get_editor_property("vignette_multiplier") != 1.0 else 0.99)


def move():
    cam.set_actor_location(cam.get_actor_location() + unreal.Vector(0, 50, 0), False, False)


def move_teleport():
    cam.set_actor_location(cam.get_actor_location() + unreal.Vector(0, -50, 0), False, True)


STEPS = [("idle", None), ("follow", edit_follow), ("look", edit_look), ("aperture", edit_aperture),
         ("dl", edit_dl), ("move", move), ("move_tp", move_teleport), ("idle2", None)]
state = {"t": 0, "rows": []}
PER = 60


def tick(dt):
    t = state["t"]
    i = t // PER
    if i >= len(STEPS):
        unreal.unregister_slate_post_tick_callback(h)
        with open(args.out, "w") as f:
            f.write("tick,step,focal,overscan\n")
            f.writelines(f"{a},{b},{c:.3f},{d:.4f}\n" for a, b, c, d in state["rows"])
        unreal.log(f"focal_loss_probe: wrote {args.out}")
        return
    name, fn = STEPS[i]
    if t % PER == 10 and fn:
        try:
            fn()
        except Exception as e:
            unreal.log_warning(f"focal_loss_probe: {name} failed: {e}")
    state["rows"].append((t, name, cc.current_focal_length, cc.get_editor_property("overscan")))
    state["t"] += 1


h = unreal.register_slate_post_tick_callback(tick)
