> **Status 2026-09-24:** phase 1 is built and verified (keyed DL_AD_Master -> DL_AD_Supreme at frame 24 in
> `/Game/Claude/DynamicLens/LS_PresetKey`, CitySample; scrubbing swaps the lens both ways).
> **Phases 2 and 3 built and installed later the same night.** Phase 2: the locked-focal Notes line and the
> Preset / Camera-row tooltips; `bMatchSqueezeWhenKeyed` and the Overscan Mode tooltip fix were left out (the
> first is Dylan's call, the second needs a render to confirm). Phase 3: `UDynamicLensKit`, `Kit` +
> `bKitSnapsFocal` on the component, the `kits` JSON section and `dl.import_kits()`, as designed below, with
> `DLK_L_PoorThings` and `DLK_L_Favourite` shipped. Verified from Python on a camera in a throwaway map: picks
> switch at the log-focal midpoints (5.7 / 8.9 / 24 / 70 mm on Poor Things), focal snaps, Notes names the lens.
> **Not verified:** a Sequencer focal track driving a kit while scrubbing, and a render across a kit switch.
> Phase 4 not started.

# Sequencer integration — keying the lens, not just the camera

Researched 2026-09-24 against the plugin source and the UE 5.8 engine source. Nothing here is
implemented yet. The ask was "Sequencer integration for dynamic lens, especially the focal length
param / lens set". Engine files are cited relative to `Engine/Source` (or `Engine/Plugins`).

**Short answer:** a lens change can already be keyed today for any prime *series* preset, by keying
the CineCamera's focal length. Keying the **Preset** itself needs one `Interp` flag and one setter
function (phase 1, a C++ build and a restart). A "lens kit" asset that picks a preset per focal is
optional phase 3. No custom track is needed.

## 1. What is keyable today, and does the component follow it

### Keyable now

| Where | Property | Notes |
|---|---|---|
| Dynamic Lens component | `bEnabled`, `AmountMultiplier`, `VignetteMultiplier`, `SwirlMultiplier` | the only `Interp` properties in `Source/` |
| CineCamera component | `CurrentFocalLength`, `CurrentAperture`, `Filmback`, focus settings (manual distance, tracking actor/offset), custom near clip | `Runtime/CinematicCamera/Public/CineCameraComponent.h`, `CineCameraSettings.h` |
| CineCamera component | **not** `LensSettings.SqueezeFactor` | no `Interp` flag on `FCameraLensSettings`, so squeeze cannot be keyed. It matters for spherical↔anamorphic switches, see §2 |

`Preset`, the override blocks and the `Camera` mirror row are **not** keyable.

Sequencer's keyability rule is `CPF_Interp` or a native `Setter`
(`Editor/Sequencer/Private/SequencerObjectChangeListener.cpp`, `GetPropertyAccessibility`).

### Tick order: correct, same frame, in the editor, in PIE and in renders

- **Runtime / PIE / MRQ / MRG:** `UWorld::Tick` broadcasts `MovieSceneSequenceTick` (every level
  sequence player) *before* `RunTickGroup(TG_PrePhysics)` (`Runtime/Engine/Private/LevelTick.cpp`).
  The component ticks in `TG_PostUpdateWork`, so it always reads this frame's keyed focal, focus and
  aperture.
- **Editor with Sequencer open:** `FSequencer` is an `FTickableEditorObject`, and
  `UEditorEngine::Tick` runs `FTickableEditorObject::TickObjects` before
  `EditorContext.World()->Tick` (`Editor/UnrealEd/Private/EditorEngine.cpp`). Scrubbing is therefore
  also same-frame. The component only ticks when the viewport is realtime
  (`LEVELTICK_ViewportsOnly`). Sequencer adds a realtime override while it is open
  (`LevelEditorSequencerIntegration.cpp`), so this works by default.
- `CurrentFocusDistance` is computed in the CineCamera's own tick, which runs in an earlier group,
  so it is also current by `TG_PostUpdateWork`.

**Conclusion:** no tick changes are needed. The comment on the constructor's `TickGroup` line
already states the intent.

### The Camera mirror row does not fight Sequencer, but it cannot key anything

- `PullCameraQuick` runs at the top of every `Apply` and copies camera → mirror. `PushCameraQuick`
  runs **only** from `PostEditChangeProperty` on the `Camera` member. So per frame the camera is the
  source of truth, and Sequencer's values flow into the mirror. There is no per-frame fight.
- **The trap:** editing the Camera row with auto-key on does **not** create a key. Sequencer
  auto-keys from details-panel `IPropertyHandle` changes (`FSequencerObjectChangeListener::OnPropertyChanged`).
  The handle is `Camera.FocalLengthMm`, which is not `Interp`, and `PushCameraQuick` writes
  `Cam->SetCurrentFocalLength` directly. If the focal already has a track, the edit holds only
  until the next evaluation (scrub or play) and then snaps back. **Key on the CineCamera
  component's own properties.** Phase 2 adds a hint for this.
- **Locked primes snap Sequencer's value.** With `bLockFocalLength` (every prime and ST-map series
  preset), `Apply` calls `SetCurrentFocalLength(GetLockedFocal(...))` each frame, after Sequencer
  has written. A focal curve that ramps 25→50 on a locked series therefore *jumps* at the nearest
  prime boundary. That is correct for primes, and it is what makes §3 option A work. The curve in
  Sequencer then shows a value that differs from what renders. Phase 2 surfaces this in Notes.
- The component's own snap does not trigger auto-key, because there is no property handle, so it
  never litters the sequence with keys.

## 2. Making the Preset (the lens) keyable

### Options

| Option | Verdict |
|---|---|
| **(a) Object property track on `Preset`** | **Recommended.** Engine support is complete and it needs about 15 lines. |
| (b) Integer "lens index" into a list | Keys show as bare numbers. It duplicates (a) and adds an indirection that breaks when the list is reordered. Rejected. |
| (c) Custom `UMovieSceneTrack` + entity system + track editor | Several hundred lines across both modules, for nothing (a) lacks. Rejected unless we ever need pre-roll loading or blending between lenses (meaningless for a discrete lens). |

### Why (a) works, with evidence

- `FObjectPropertyTrackEditor` is registered for every `FObjectPropertyBase`
  (`Editor/MovieSceneTools/Private/TrackEditors/ObjectPropertyTrackEditor.*`, registered in
  `MovieSceneToolsModule.cpp`). `TObjectPtr<UDynamicLensPreset>` is one of these.
  `InitializeNewTrack` sets `UMovieSceneObjectPropertyTrack::PropertyClass` from the property, so the
  key's asset picker is already filtered to Dynamic Lens Presets.
- Keys are `FMovieSceneObjectPathChannel` values holding a **soft path plus a hard pointer**
  (`Runtime/MovieScene/Public/Channels/MovieSceneObjectPathChannel.h`). Keyed presets load with the
  sequence and are cooked with it. There is no load hitch at the key, which matters because ST-map
  presets drag in their textures.
- The channel is stepped: the value switches exactly on the key's frame.
- Runtime assignment: object properties never take the fast offset path
  (`MovieScenePropertyRegistry.cpp`, `ComputeFastPropertyPtrOffset`). They go through
  `FTrackInstancePropertyBindings`, which looks up a UFunction named **`Set<PropertyName>`** and
  calls it if present (`TrackInstancePropertyBindings.cpp`, `CacheBinding`). Otherwise it writes the
  pointer raw. `FObjectPropertyTraits::CanAssignValue` enforces the class, and clearing to null is
  allowed unless the property is `NoClear`.
- Restore State / closing Sequencer restores the pre-animated value **through the same setter**.

### Why a setter is mandatory, not optional

A raw write of `Preset` skips everything `PostEditChangeProperty` / `ApplyPreset` do. In particular
it skips `ClearEffect()`, `TransientLensFile = nullptr` and `LensFileSTMapIndex = -1`. The transient
Lens File and the ST-map index are cached against the *old* profile. The next `Apply` would reuse a
stale lens file, and the captured look backup would carry over from the old lens.

`ApplyPreset` itself is also the **wrong** thing for Sequencer to call:

- `Modify()` on the component (and `Cam->Modify()` inside `MatchCameraToProfile`) marks the level
  dirty and writes undo transactions on every evaluation that changes lens.
- `MatchCamera.bOnPresetChange` rewrites the focal length, filmback and squeeze. That fights any
  focal or filmback track and leaves the camera changed after the sequence ends.
- `CopyAllFromPreset` rewrites the serialized override blocks as a side effect of playback.

### Phase 1 exact changes (`DynamicLensComponent.h/.cpp`)

```cpp
/** The look. ... Keyable in Sequencer: a key is a lens change, applied on that frame. */
UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPreset, Category = "Dynamic Lens")
TObjectPtr<UDynamicLensPreset> Preset;

/**
 * Swap the lens without touching the camera or the override blocks. This is what Sequencer and
 * Blueprint "Set Preset" call. The editor buttons and the Preset Browser keep using ApplyPreset,
 * which also honours Match Camera.
 */
UFUNCTION(BlueprintSetter)
void SetPreset(UDynamicLensPreset* NewPreset);
```

```cpp
void UDynamicLensComponent::SetPreset(UDynamicLensPreset* NewPreset)
{
	if (NewPreset == Preset) return;             // Sequencer re-sends the same value every evaluation
	Preset = NewPreset;                          // null allowed: "no lens", tick clears the effect
	ClearEffect();                               // restores the camera, drops MIDs, resets dynamic overscan
	TransientLensFile = nullptr;
	LensFileSTMapIndex = -1;
	InfoProfile = nullptr;                       // forces the Profile Info refresh in Apply
	if (UCineCameraComponent* Cam = GetTargetCamera(); Cam && bEnabled && HasLens()) Apply(Cam);
}
```

No `Modify()`, no Match Camera, no `CopyAllFromPreset`. `ApplyPreset` stays as it is.
Optionally it can call `SetPreset` for its common part.

Name check: the setter must be exactly `SetPreset`. `UActorComponent` has no member of that name,
so nothing is shadowed. Python `set_editor_property("preset", ...)` is unaffected.

### What happens when the preset changes mid-shot

- **Same frame, no flicker.** `ClearEffect` restores the camera (overscan, crop-overscan flag,
  squeeze-compensated sensor width, near clip, bokeh `LensSettings`), and `Apply` re-captures and
  re-applies before the frame renders.
- **Overscan pop.** `ClearEffect` zeroes `LastDynamicOverscan`, so the new lens's need is computed
  fresh with no hysteresis carried over. If it differs by a step, the render target resizes and
  TSR history resets, giving one soft frame. On a cut this is invisible. Mid-shot, a lens swap is a
  cut in reality anyway. To avoid it, give both lenses the same **Fixed** overscan (an Overscan
  override on the component).
- **Movie Render Graph / Queue.** In 5.8 both cache the camera's overscan **per output frame**, on
  the first temporal sample, not per shot:
  - `UMovieGraphDefaultRenderer::Render` empties `CameraOverscanCache` when
    `bIsFirstTemporalSampleForFrame`.
  - `MoviePipelineRendering.cpp` does the same on `IsFirstTemporalSample()`.

  A lens key on a whole frame is picked up cleanly. A key that lands between temporal sub-samples
  logs "Overscan on camera ... changed since start of frame" and uses the cached value for the rest
  of that frame. **Key lens changes on whole frames.**

  The tooltip on `FDynamicLensOverscan::Mode` says MRQ/MRG "read overscan once per shot". That
  looks stale for 5.8. Confirm with a render before rewording it (a docs/tooltip fix, not part of
  this work).
- **Shot start.** `BeginPlay` applies the level's saved preset. Sequencer then evaluates at the
  first world tick (before `TG_PrePhysics`) and calls `SetPreset`, which applies synchronously.
  Warm-up frames absorb the switch. Nothing to change.
- **Format changes are not handled by the key.** Spherical↔anamorphic or a different native sensor
  needs squeeze and filmback to change too. Filmback is keyable on the camera; squeeze is not (§1).
  Use one camera actor per format, which is how a real shoot swaps bodies anyway. The alternative
  is an opt-in `MatchCamera.bMatchSqueezeWhenKeyed` that `SetPreset` applies without `Modify`
  (phase 2, only if Dylan wants it).
- **Override blocks still win.** With `bOverrideDistortion` ticked, a keyed preset changes only
  the blocks that are not overridden, and the Distortion override keeps its own profile. That is
  consistent with the component's model, and should be stated in the tooltip.
- **Preset Browser and A1/A2 do not auto-key.** They call `ApplyPreset`, which writes the member
  directly with no property handle. To key a lens change, use the Preset dropdown in the details
  panel with auto-key on, or its key (+) button, which appears once the property is `Interp`.
  Making the browser key directly needs `ISequencer` from the editor module (phase 4).

### Phase 1 verification (after Dylan restarts)

1. Sequencer: CineCameraActor binding → Dynamic Lens component → + Track → **Preset**. Key
   `DL_AD_Master` at frame 0 and `DL_T_*` at frame 48. Scrub across 48: the look switches on 48
   and Profile Info follows. Scrub back: it switches back, and the level is **not** marked dirty.
2. Close Sequencer: the component returns to its pre-animated preset.
3. Tick order: key `CurrentFocalLength` 18→75 linear on a zoom preset (`DL_AD_*` parametric) and
   step frame by frame. `Debug > Last Focal Mm` must equal the camera's value on the same frame.
   The code reading says it will.
4. MRG render with Render Mode = Inside TSR across the lens key. Check the log for the
   animated-overscan warning, and check that the frames either side of the key are framed correctly.

## 3. Focal length, zooms and prime sets

### What already exists

- **An ST-map series preset is already a prime set.** Every `DL_T_*` and every Andy Davis
  spherical `DL_AD_*` holds one ST map per measured prime (for example ARRI Signature 18/25/35/47/58/75)
  with `lock_focal_length: true`. `GetLockedFocal` → `FindNearestSTMap` snaps the camera to the
  nearest prime every frame.
- **Parametric grids are zooms** (`lock_focal_length: false`). Keyed focal length is a real zoom,
  and the distortion is evaluated continuously.

### Option A: key a lens change today, no code

On a series preset, key the **CineCamera's Current Focal Length** at the cut with **Constant**
(stepped) interpolation: 25 at the start, 50 from frame N. The component snaps to the measured
prime, and FOV and distortion change together on that frame. This is the DP's "go to the 50".
Use constant keys: an auto/cubic curve still works, but it swaps primes at an arbitrary midpoint.

### Option B: after phase 1, change series or glass

Key the **Preset** track (and focal if the new preset is a zoom). This covers everything that is
not inside one series: the single-prime presets (`DL_L_*` Petzval 58/85, Ultra Prime 10), switching
from Cooke to Panavision, and prime↔zoom.

### Option C, phase 3 (optional): a Lens Kit asset

This is only worth building if Dylan wants a *kit* that mixes presets, keyed by focal alone: for
example the Lanthimos kit of 6 mm porthole, 10 mm, 58 Petzval and 85 Petzval, or a series with a
per-prime tuned look.

```cpp
USTRUCT(BlueprintType)
struct FDynamicLensKitLens
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kit") float FocalMm = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kit") TObjectPtr<UDynamicLensPreset> Preset;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kit") FString Label;   // "Petzval 58"
};

/** A case of lenses. Key the camera's focal length; the kit picks the preset for it. */
UCLASS(BlueprintType)
class UDynamicLensKit : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kit", meta = (MultiLine = "true")) FString Description;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kit") TArray<FDynamicLensKitLens> Lenses;   // sorted by focal on save
	/** Nearest lens in log-focal space (the 25/32 boundary sits at 28.3, not 28.5). */
	const FDynamicLensKitLens* Pick(float FocalMm) const;
};
```

Component additions:

```cpp
UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetKit, Category = "Dynamic Lens")
TObjectPtr<UDynamicLensKit> Kit;          // null = use Preset as today
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens", meta = (EditCondition = "Kit != nullptr"))
bool bKitSnapsFocal = true;               // hold the camera at the picked lens's focal (a prime kit)
```

At the top of `Apply`, when `Kit` is set: `Pick(Cam->CurrentFocalLength)`, then `SetPreset(Entry->Preset)`
if it changed, then `SetCurrentFocalLength(Entry->FocalMm)` if `bKitSnapsFocal`. The DP keys only
the camera's focal length (constant interpolation), exactly as in option A, and the kit does the
rest. A kit can itself be keyed (`Kit` is `Interp`) to change cases between scenes.

Rules:

- **Kit and a Preset track on the same component:** the kit wins, and Notes says so. Do not try to
  merge them.
- **Data flow:** kits go in `Tools/data/presets.json` under a new `kits` section with a
  `dl.import_kits()` importer, per `.claude/rules/preset-data-flow.md`. They are never
  editor-only.
- **Preset Browser:** show kits as a second tab later. Not required.
- **Cost:** about 150 lines of C++ plus 40 of Python. A C++ build and restart. No preset re-import,
  because no preset field changes.

## 4. Editor niceties and their cost

| Nicety | Needed? | Cost |
|---|---|---|
| Asset picker on Preset keys filtered to presets | Already free, from `PropertyClass` | 0 |
| Details-panel key (+) button on Preset | Free once `Interp` | 0 |
| Notes line: "focal locked to 35 mm (Sequencer asks 30)" and "Camera row edits are not keyed - key the CineCamera" | Yes, phase 2 | ~15 lines, runtime module |
| Camera row tooltip pointing at the CineCamera for keying | Yes, phase 2 | 1 line |
| "Add Lens Track" entry on CineCameraActor bindings (a small `FMovieSceneTrackEditor` in `DynamicLensEditor` that adds the Preset property track to the component in one click) | Nice, not needed. + Track on the component already works | ~150 lines, editor module, `Sequencer` + `MovieSceneTools` deps |
| Preset Browser / A1-A2 keying when Sequencer is open and auto-key is on | Nice | ~80 lines in the editor module via `ISequencer` (from `ILevelSequenceEditorToolkit`) |
| Custom section painting preset names on keys | Probably unnecessary. Check how the stock object-path section draws first | ~100 lines if needed |
| Custom track type | No | several hundred lines, see §2 |

## Phased plan

| Phase | What | Build / restart | After install |
|---|---|---|---|
| **1** | `Preset` gets `Interp` + `BlueprintSetter = SetPreset`, and the `SetPreset` implementation above. Run the verification list. Document option A in `using-the-component.md` | C++ build; install needs the editor closed (ask Dylan) | nothing to re-import |
| 2 | Notes hints (locked focal vs Sequencer, Camera row not keyable), tooltips (Preset keyable, overrides win). Confirm the MRG per-frame overscan finding and fix the Overscan Mode tooltip if it holds. Optional `bMatchSqueezeWhenKeyed` | C++ build + restart | `dl.resave_presets()` only if `GetAssetRegistryTags` changed (it should not) |
| 3 (optional) | `UDynamicLensKit`, `Kit` + `bKitSnapsFocal` on the component, the `kits` JSON section and `dl.import_kits()` | C++ build + restart; the Python part is live | `dl.import_kits()` |
| 4 (optional) | Editor-module niceties from §4 | C++ build + restart | - |

Every phase touches `Source/`, so per `.claude/rules/updating-the-plugin.md` each one is: build
with the editor up, then stop and ask Dylan before install. Phases 1 and 2 are small enough to
ship in one build.
