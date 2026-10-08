# Copyright (c) 2026 Dylan Gitalis. Source-available under CPAL-1.0 with the Commons Clause; see LICENSE.
# SPDX-License-Identifier: CPAL-1.0 AND LicenseRef-Commons-Clause-1.0
"""
Builds a Sequencer cut test for DynamicLens in any UE 5.8 project with Black Eye: a level with a grid of boxes and
a subject, angle shots that each spawn their own Black Eye camera (LookAt the subject), and masters that cut
between them every few frames. Production angles spawn their cameras at every cut, so this reproduces the
per-spawn cost of the component.

    cut_stutter_repro.py [--folder /Game/DLCut] [--cut 20] [--frames 300]

Angle kinds (three angles each):
    st     Black Eye camera + DynamicLens, ST-map preset (Panavision C, 35 mm)
    twin   as st, plus a plain CineCamera "_Bake" twin that also carries DynamicLens (the Fast Bake layout)
    param  Black Eye camera + DynamicLens, parametric preset (DL_AD_Master)
    fish   Black Eye camera + DynamicLens, projection (fisheye) preset (DL_L_Favourite_10mm)
    off    Black Eye camera, no DynamicLens
Masters: M_<kind> cuts between that kind's angles; M_mix cuts across all of them.
Re-running rebuilds everything in the folder except the level.
"""
import argparse
import sys

import unreal

parser = argparse.ArgumentParser()
parser.add_argument("--folder", default="/Game/DLCut")
parser.add_argument("--cut", type=int, default=20, help="frames per cut in the masters")
parser.add_argument("--frames", type=int, default=300)
args = parser.parse_args(sys.argv[1:])

F = args.folder
# Git Bash rewrites "/Game/..." arguments into Windows paths (set MSYS_NO_PATHCONV=1); never build into the open level
if not F.startswith("/Game/"):
    raise RuntimeError(f"--folder must be a /Game/ path, got {F!r}")
FPS = 30
N = args.frames
ext = unreal.MovieSceneSequenceExtensions
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lses = unreal.get_editor_subsystem(unreal.LevelSequenceEditorSubsystem)
seqlib = unreal.LevelSequenceEditorBlueprintLibrary

PRESETS = {
    "st": "/DynamicLens/Presets/Tiedtke/DL_T_Panavision_C_Series",
    "twin": "/DynamicLens/Presets/Tiedtke/DL_T_Panavision_C_Series",
    "param": "/DynamicLens/Presets/DL_AD_Master",
    "fish": "/DynamicLens/Presets/DL_L_Favourite_10mm",
}
SPOTS = [(-1900, -1500, 450), (1900, -1300, 500), (200, 2100, 400)]

# --- level --------------------------------------------------------------------------------------------------
level_path = f"{F}/L_DLCut"
if assets.does_asset_exist(level_path):
    les.load_level(level_path)
else:
    if not les.new_level(level_path):
        raise RuntimeError(f"could not create {level_path}")
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    floor = actors.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Plane"), unreal.Vector(0, 0, 0))
    floor.set_actor_scale3d(unreal.Vector(80, 80, 1))
    actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, -50, 30))
    actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300))
    for i in range(-4, 5):
        for j in range(-4, 5):
            if i == 0 and j == 0:
                continue
            b = actors.spawn_actor_from_object(cube, unreal.Vector(i * 350, j * 350, 50 + 40 * ((i * j) % 4)))
            b.set_actor_scale3d(unreal.Vector(0.6, 0.6, 1 + ((i + j) % 3)))
            b.set_folder_path("Grid")
    subject = actors.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Sphere"), unreal.Vector(0, 0, 120))
    subject.set_actor_label("Subject")
    les.save_current_level()
subject = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == "Subject")


def new_sequence(name):
    path = f"{F}/{name}"
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    seq = tools.create_asset(name, F, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
    ext.set_display_rate(seq, unreal.FrameRate(FPS, 1))
    ext.set_playback_start(seq, 0)
    ext.set_playback_end(seq, N)
    return seq


def spawned(seq, binding):
    seqlib.set_current_time(10)
    seqlib.refresh_current_level_sequence()
    objs = seqlib.get_bound_objects(ext.get_portable_binding_id(seq, seq, binding))
    return objs[0] if objs else None


# --- angles -------------------------------------------------------------------------------------------------
angles = {}
for kind in ("st", "twin", "param", "fish", "off"):
    for k, spot in enumerate(SPOTS):
        name = f"LS_DLCut_{kind}_{'ABC'[k]}"
        seq = new_sequence(name)
        seqlib.open_level_sequence(seq)
        cam = lses.add_spawnable_from_class(seq, unreal.BlackEyeCineCameraActor)
        cam.set_name("BEC")
        tmpl = cam.get_object_template()
        tmpl.get_editor_property("root_component").set_editor_property("relative_location", unreal.Vector(*spot))
        look = tmpl.get_editor_property("look_at")
        target = look.get_editor_property("target_0")
        target.set_editor_property("actor", subject)
        look.set_editor_property("target_0", target)
        cut = ext.add_track(seq, unreal.MovieSceneCameraCutTrack).add_section()
        cut.set_range(0, N)
        cut.set_camera_binding_id(ext.get_portable_binding_id(seq, seq, cam))
        cams = [cam]
        if kind == "twin":
            twin = lses.add_spawnable_from_class(seq, unreal.CineCameraActor)
            twin.set_name("BEC_Bake")
            twin.get_object_template().get_editor_property("root_component").set_editor_property("relative_location", unreal.Vector(*spot))
            cams.append(twin)
        preset = unreal.load_asset(PRESETS[kind]) if kind in PRESETS else None
        for b in cams:
            actor = spawned(seq, b)
            actor.get_cine_camera_component().set_editor_property("current_focal_length", 35.0)
            if preset:
                unreal.DynamicLensLibrary.add_to_actor(actor, preset)
            lses.save_default_spawnable_state(b)
        assets.save_loaded_asset(seq)
        angles.setdefault(kind, []).append(seq)
        print("angle", name)

# --- masters ------------------------------------------------------------------------------------------------
def make_master(name, pool):
    seq = new_sequence(name)
    track = ext.add_track(seq, unreal.MovieSceneCinematicShotTrack)
    for i, start in enumerate(range(0, N, args.cut)):
        inner = pool[i % len(pool)]
        s = track.add_section()
        s.set_sequence(inner)
        s.set_range(start, min(start + args.cut, N))
        res = ext.get_tick_resolution(inner)
        params = s.get_editor_property("parameters")
        params.set_editor_property("start_frame_offset", unreal.FrameNumber((start % 120) * (res.numerator // (res.denominator * FPS))))
        s.set_editor_property("parameters", params)
    assets.save_loaded_asset(seq)
    print("master", name, len(track.get_sections()), "cuts")


for kind, pool in angles.items():
    make_master(f"M_{kind}", pool)
make_master("M_mix", [a for pool in zip(*angles.values()) for a in pool])
