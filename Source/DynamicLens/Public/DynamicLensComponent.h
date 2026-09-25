// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

// DynamicLens — the component you add to a CineCameraActor.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicLensTypes.h"
#include "Engine/EngineTypes.h"
#include "LensDistortionModelHandlerBase.h"
#include "DynamicLensComponent.generated.h"

class UCineCameraComponent;
class ULensDistortionModelHandlerBase;
class ULensFile;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;

/** Where in the frame the distortion is rendered. */
UENUM(BlueprintType)
enum class EDynamicLensRenderMode : uint8
{
	/** Post-process material on the camera. Works everywhere (editor viewport, PIE, Movie Render Queue/Graph). Resamples the image once. */
	PostProcessMaterial UMETA(DisplayName = "Post Process Material"),
	/** Applied inside Temporal Super Resolution: sharpest result, no extra resample. Needs TSR as the anti-aliasing method (r.TSR.LensDistortion=1). Falls back to the primary upscale pass otherwise. */
	TemporalSuperResolution UMETA(DisplayName = "Inside TSR (sharpest)"),
};


/** How a profile measured on one sensor is applied to a camera with another sensor. */
UENUM(BlueprintType)
enum class EDynamicLensSensorFit : uint8
{
	/** Stretch the profile's frame to this camera's frame. The distortion pattern scales with the sensor (parametric profiles are always exact; ST maps get scaled). */
	Scale UMETA(DisplayName = "Scale to sensor"),
	/** Keep the profile's physical scale: a smaller sensor sees the centre of the lens grid, exactly like putting that lens on that camera. If this sensor is larger than the profile's, falls back to Scale. */
	Crop UMETA(DisplayName = "Crop (physical)"),
};

/**
 * Dynamic Lens: focal-length / focus / f-stop driven distortion, vignette, image circle and bokeh character for the
 * CineCamera on the same actor. Pick a preset asset; the rest happens every frame, in the editor (Realtime viewport)
 * and in renders.
 */
UCLASS(ClassGroup = (Cinematics), meta = (BlueprintSpawnableComponent, DisplayName = "Dynamic Lens"),
	HideCategories = (Tags, Activation, Cooking, AssetUserData, Collision, ComponentReplication, Replication, Navigation, Sockets))
class DYNAMICLENS_API UDynamicLensComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDynamicLensComponent();

	/** Master switch. Off restores the camera to exactly how it was. Keyable in Sequencer. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens")
	bool bEnabled = true;

	/**
	 * The look. Presets are assets (Content Browser: right-click > Miscellaneous > Data Asset > Dynamic Lens Preset).
	 * Keyable in Sequencer: a key is a lens change, applied on that frame (the camera itself is left alone).
	 * Key on whole frames. Ticked Override blocks still win over a keyed preset, as they do over a picked one.
	 */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPreset, Category = "Dynamic Lens")
	TObjectPtr<UDynamicLensPreset> Preset;

	/**
	 * Swap the lens without touching the camera or the override blocks. This is what Sequencer and Blueprint "Set Preset"
	 * call; the editor buttons and the Preset Browser use ApplyPreset, which also honours Match Camera.
	 */
	UFUNCTION(BlueprintSetter)
	void SetPreset(UDynamicLensPreset* NewPreset);

	/**
	 * A case of lenses picked by focal length. Set, the kit chooses Preset from the camera's focal every frame, so in
	 * Sequencer you key only the Cine Camera's focal length (Constant interpolation) and the lens follows. Empty = Preset
	 * as usual. Keyable, to change cases between scenes. A kit overrides a Preset track and the preset buttons.
	 */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetKit, Category = "Dynamic Lens")
	TObjectPtr<UDynamicLensKit> Kit;

	/** With a kit: hold the camera at the picked lens's focal length, as a case of primes does. Off lets the camera sit between them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens", meta = (EditCondition = "Kit != nullptr"))
	bool bKitSnapsFocal = true;

	UFUNCTION(BlueprintSetter)
	void SetKit(UDynamicLensKit* NewKit);

	/** The preset's lens: what it covers, the filmback / squeeze / crop / focal length that Match Camera To Profile would set. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens", meta = (MultiLine = "true"))
	FString ProfileInfo;

	/** What Match Camera To Profile sets on the camera, and whether it runs by itself when the preset changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens", meta = (DisplayName = "Match Camera"))
	FDynamicLensMatchOptions MatchCamera;

	/**
	 * The camera settings you touch most (focal length, aperture, focus, crop, filmback, squeeze), mirrored from the Cine Camera component.
	 * Not keyable here: to animate focal length, focus or aperture in Sequencer, key the Cine Camera component's own properties.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens", meta = (DisplayName = "Camera"))
	FDynamicLensCameraQuick Camera;

	/** Scales the preset's distortion amount for this camera only. 1 = as the preset. Keyable in Sequencer. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "2.0"))
	float AmountMultiplier = 1.f;

	/** Apply the lens distortion. Off leaves the picture rectilinear but keeps vignette, bokeh and image circle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Layers")
	bool bApplyDistortion = true;

	/** Apply the preset's vignette to this camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Layers")
	bool bApplyVignette = true;

	/** Apply the preset's bokeh character (blades, cat's eye, swirl) to this camera's depth of field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Layers")
	bool bApplyBokeh = true;

	/**
	 * Below Cinematic post-process quality Unreal turns off depth-of-field bokeh simulation
	 * and lowers highlight scattering (r.DOF.*.EnableBokehSettings, r.DOF.Scatter.BackgroundCompositing), which silently removes swirl, cat's eye and iris shape.
	 * When on, these are raised while this camera applies bokeh and restored when it stops. Costs some DOF time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Dynamic Lens|Layers", meta = (EditCondition = "bApplyBokeh"))
	bool bForceBokehQuality = true;

	/**
	 * Near clip plane used while a fisheye (projection) preset is applied, cm. Unreal clips on view depth, not distance
	 * along the ray, so at 81 deg off-axis the default 10 cm hides everything nearer than 64 cm along the ray and eats
	 * the rim. The camera's own setting comes back when the preset changes or the component is removed. 0 = leave it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Dynamic Lens|Layers", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0", Units = cm))
	float FisheyeNearClipCm = 0.1f;

	/** Black out everything the lens can't show: beyond its image circle, and beyond the pixels the overscan provides. Real lenses do exactly this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Layers")
	bool bApplyImageCircle = true;

	/** Scales the preset's vignette strength for this camera. Keyable in Sequencer. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Layers", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "2.0"))
	float VignetteMultiplier = 1.f;

	/** Scales the preset's Petzval swirl for this camera. Keyable in Sequencer. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Layers", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "2.0"))
	float SwirlMultiplier = 1.f;

	/** How a profile measured on one sensor is mapped onto this camera's sensor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Sensor")
	EDynamicLensSensorFit SensorFit = EDynamicLensSensorFit::Scale;

	// --- per-camera overrides ---------------------------------------------------------------
	/** Override the preset's Distortion block for this camera only (ticking it copies the preset's values in; the preset asset is never changed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (InlineEditConditionToggle))
	bool bOverrideDistortion = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (EditCondition = "bOverrideDistortion", DisplayName = "Distortion"))
	FDynamicLensDistortion Distortion;

	/** Override the preset's Image Circle block for this camera only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (InlineEditConditionToggle))
	bool bOverrideImageCircle = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (EditCondition = "bOverrideImageCircle", DisplayName = "Image Circle"))
	FDynamicLensImageCircle ImageCircle;

	/** Override the preset's Vignette block for this camera only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (InlineEditConditionToggle))
	bool bOverrideVignette = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (EditCondition = "bOverrideVignette", DisplayName = "Vignette"))
	FDynamicLensVignette Vignette;

	/** Override the preset's Bokeh block for this camera only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (InlineEditConditionToggle))
	bool bOverrideBokeh = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (EditCondition = "bOverrideBokeh", DisplayName = "Bokeh"))
	FDynamicLensBokeh Bokeh;

	/** Override the preset's Overscan block for this camera only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (InlineEditConditionToggle))
	bool bOverrideOverscan = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overrides", meta = (EditCondition = "bOverrideOverscan", DisplayName = "Overscan"))
	FDynamicLensOverscan Overscan;

	// --- save as preset ---------------------------------------------------------------------
	/** Name of the preset asset that Save As New Preset creates (empty = <current preset>_Copy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Save As Preset")
	FString NewPresetName;

	/** Folder the new preset asset is saved to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Save As Preset", meta = (ContentDir))
	FDirectoryPath NewPresetFolder;

	/** How the distortion is rendered. Post Process Material is the safe default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Advanced")
	EDynamicLensRenderMode RenderMode = EDynamicLensRenderMode::PostProcessMaterial;

	/** Material used for the image-circle mask (plugin default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Advanced", AdvancedDisplay)
	TSoftObjectPtr<UMaterialInterface> ImageCircleMaterial;

	// --- read-only feedback -----------------------------------------------------------------
	/** Last evaluated focal length (mm). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastFocalMm = 0.f;
	/** What the profile covers. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FString ProfileCoverage;
	/** Last evaluated focus distance (cm). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastFocusCm = 0.f;
	/** Last evaluated f-stop. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastFStop = 0.f;
	/** Sensor size actually used (after squeeze and crop), mm. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FVector2D LastSensorMm = FVector2D::ZeroVector;
	/** Distortion coefficients pushed to the renderer (parametric profiles). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FDynamicLensParams LastParams;
	/** Overscan the frame needed, and what was applied (1.05 = 5% wider render). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float NeededOverscanFactor = 1.f;
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastOverscanFactor = 1.f;
	/** Vignette intensity pushed to the camera. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastVignette = 0.f;
	/** Field angle at the frame corner (degrees). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float CornerFieldAngleDeg = 0.f;
	/** Fraction of the entrance pupil still visible at the frame corner (1 = no cat's eye, 0.5 = half clipped). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float CornerPupilVisible = 1.f;
	/** Barrel radius / length (mm) handed to the depth of field. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FVector2D BarrelRadiusLengthMm = FVector2D::ZeroVector;
	/** Radius of the visible image circle, 1 = half the frame width (0 = whole frame visible). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float ImageCircleRadius = 0.f;
	/** Which edge is being drawn: the lens's own image circle, or the limit of what the render can show (the data limit), or none. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FString ActiveMask;
	/** Notes from the last evaluation (e.g. sensor fit fallback). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FString Notes;

	/** The CineCamera this component drives (first CineCameraComponent on the owner). */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	UCineCameraComponent* GetTargetCamera() const;

	/** Switch to the previous preset asset (alphabetical, all Dynamic Lens Preset assets in the project and plugin). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dynamic Lens", meta = (DisplayName = "Previous Preset"))
	void A1_PreviousPreset();

	/** Switch to the next preset asset (alphabetical, all Dynamic Lens Preset assets in the project and plugin). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dynamic Lens", meta = (DisplayName = "Next Preset"))
	void A2_NextPreset();

	/** Step the camera to the previous focal length the profile was measured at (ST-map series: the primes; parametric grids: the measured focals). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dynamic Lens", meta = (DisplayName = "Previous Focal"))
	void A3_PreviousFocal();

	/** Step the camera to the next focal length the profile was measured at. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dynamic Lens", meta = (DisplayName = "Next Focal"))
	void A4_NextFocal();

	/**
	 * Switch to a preset the same way the A1/A2 buttons do: clear the old effect, honour the Match
	 * Camera options (match on change, or just refresh the override blocks), and re-apply.
	 *
	 * This is the one path a preset change should ever take. The Preset Browser calls it, so the
	 * checkboxes in Match Camera mean exactly the same thing however the preset was picked.
	 * Returns false when the preset is null or already the current one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	bool ApplyPreset(UDynamicLensPreset* NewPreset);

	/** Set the camera's filmback, squeeze and crop to the preset profile's native format (the sensor the lens data was made for). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dynamic Lens")
	void MatchCameraToProfile();

	/** Remove every trace of the effect from the camera (also called automatically when disabled or removed). */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	void ClearEffect();

	/** The preset's settings with this camera's overrides applied: what is actually evaluated. */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	FDynamicLensSettings ResolveSettings() const;

	/** Copy every block from the preset into the override blocks (without turning them on), so you can start editing from the preset's values. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dynamic Lens")
	void CopyAllFromPreset();

	/** Write the resolved settings (preset + overrides) to a new preset asset, then point this component at it and clear the overrides. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dynamic Lens")
	void SaveAsNewPreset();

	/** Refresh the Profile Info text from the current preset / overrides. */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	void UpdateProfileInfo();

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void StepPreset(int32 Direction);
	void StepFocal(int32 Direction);
	void PullCameraQuick(UCineCameraComponent* Cam);
	void PushCameraQuick(UCineCameraComponent* Cam);
	void ClearDistortionRendering(UCineCameraComponent* Cam);
	TArray<float> MeasuredFocals() const;
	bool HasLens() const { return Preset != nullptr || bOverrideDistortion || Kit != nullptr; }
	/** Kit step at the top of Apply. True when it swapped the preset, which already re-ran Apply. */
	bool ApplyKit(UCineCameraComponent* Cam, FString& OutNote);
	void Apply(UCineCameraComponent* Cam);
	void EnsureHandler();
	bool DriveParametric(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float W, float H, float& OutNeededOverscan, FLensDistortionState& OutState);
	bool DriveAnamorphic(UCineCameraComponent* Cam, float Focal, float W, float H, float& OutNeededOverscan, FLensDistortionState& OutState);
	bool DriveSTMap(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float Focus, float W, float H, float WFull, float HFull, float& OutNeededOverscan, FLensDistortionState& OutState, float& OutCircleRadius);
	bool DriveProjection(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float W, float H, float AppliedOverscan, float& OutNeededOverscan, FLensDistortionState& OutState, float& OutCircleRx, float& OutCircleRy);
	void ApplyRendering(UCineCameraComponent* Cam, const FLensDistortionState& State, float AppliedOverscan);
	void ApplyLook(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float CircleRadiusNorm, float Aspect, float CircleEllipticity, float CircleSquareness);
	void CaptureLook(UCineCameraComponent* Cam);
	void RestoreLook(UCineCameraComponent* Cam);
	void StripForeignDistortionBlendables(UCineCameraComponent* Cam);
	/** Epic's Accumulation DOF component (found by class name, no hard dependency): iris texture + aberrations. */
	void ApplyAccumulationDOF(const FDynamicLensEval& Eval);
	void RestoreAccumulationDOF();
	UActorComponent* FindAccumulationDOF() const;
	/** Hold or release this component's claim on the DOF bokeh-simulation cvars (shared, ref-counted across components). */
	void SetBokehQualityRequest(bool bWant);
	bool bRequestingBokehQuality = false;

	/** Settings resolved by the last Apply (preset + overrides). */
	UPROPERTY(Transient) FDynamicLensSettings Resolved;
	float LastDynamicOverscan = 0.f;
	UPROPERTY(Transient) TWeakObjectPtr<const UDynamicLensProfile> InfoProfile;
	UPROPERTY(Transient) TObjectPtr<ULensDistortionModelHandlerBase> Handler;
	UPROPERTY(Transient) TObjectPtr<ULensFile> TransientLensFile;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> ProjectionMap;
	/** ST maps extrapolated beyond their frame so overscan has data (built on demand, editor only). Key = map path + displacement scale. */
	UPROPERTY(Transient) TMap<FString, FDynamicLensExtendedMap> ExtendedMaps;
	float InfoFocal = -1.f;
	bool bPushingCamera = false;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> IrisTexture;
	UPROPERTY(Transient) TWeakObjectPtr<UActorComponent> AccumulationDOF;
	int32 IrisTexKey = -1;
	bool bAccumApplied = false;
	struct FAccumBackup { UObject* Texture = nullptr; bool bEnable = true; float Spherical = 0.f; float Coma = 0.f; uint8 Channel = 0; } AccumBackup;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> AppliedMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CircleMID;
	UPROPERTY(Transient) TWeakObjectPtr<UCineCameraComponent> AppliedCamera;

	bool bSVEActive = false;
	bool bLookCaptured = false;
	bool bLookApplied = false;
	bool bOverscanTouched = false;
	bool bCircleApplied = false;
	bool bStrippedForeign = false;

	// keys of what the transient lens file / projection map currently hold
	int32 LensFileSTMapIndex = -1;
	FVector2D LensFileSensor = FVector2D::ZeroVector;
	FVector2D LensFileFxFy = FVector2D::ZeroVector;
	float ProjectionKeyFocal = 0.f;
	FVector2D ProjectionKeySensor = FVector2D::ZeroVector;
	float ProjectionKeyOverscan = 0.f;
	int32 ProjectionKeyType = -1;
	float ProjectionKeyMaxAngle = 0.f;
	float ProjectionNeededOverscan = 1.f;
	float ProjectionCircleRadius = 0.f;
	float ProjectionCircleRy = 0.f;
	float ProjectionKeyScale = 1.f;
	float ProjectionKeyK = 99.f;
	bool ProjectionKeyFit = false;
	float ProjectionFieldScale = 1.f;

	struct FLookBackup
	{
		bool bBlade = false; int32 Blade = 0;
		bool bPetzval = false; float Petzval = 0.f;
		bool bPetzvalFalloff = false; float PetzvalFalloff = 0.f;
		bool bExclBox = false; FVector2f ExclBox = FVector2f::ZeroVector;
		bool bExclRadius = false; float ExclRadius = 0.f;
		bool bBarrelRadius = false; float BarrelRadius = 0.f;
		bool bBarrelLength = false; float BarrelLength = 0.f;
		bool bVignette = false; float Vignette = 0.f;
		bool bSqueeze = false; float Squeeze = 1.f;
		int32 LensBlades = 7; float LensSqueeze = 1.f; float LensSensorWidth = 24.89f; bool bLensDriven = false;
		float Overscan = 0.f; bool bCropOverscan = false; bool bScaleRes = false;
		bool bNearClip = false; float NearClip = 10.f;
	} Backup;
	bool bNearClipTouched = false;
	void UpdateNearClip(UCineCameraComponent* Cam, bool bWant);

	FDynamicLensEval LastEval;
	float LastCircleRadius = -1.f;
	float LastCircleEllipticity = -1.f;
	float LastCircleSquareness = -1.f;
	bool bHasLastEval = false;
};
