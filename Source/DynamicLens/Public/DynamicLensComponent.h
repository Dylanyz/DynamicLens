// DynamicLens — the component you add to a CineCameraActor.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicLensTypes.h"
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

/** How the render is enlarged so the distorted frame has source pixels out to its corners. */
UENUM(BlueprintType)
enum class EDynamicLensOverscanMode : uint8
{
	/** Exactly what this frame needs, recomputed every frame (up to Max Overscan). Movie Render Queue/Graph read the camera's overscan once per shot, so for zoom pulls in renders prefer Fixed. */
	Dynamic UMETA(DisplayName = "Dynamic (per frame)"),
	/** A constant overscan for the whole shot. Safe for renders; anything the frame needs beyond it goes black at the edges (image circle). */
	Fixed UMETA(DisplayName = "Fixed"),
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

	/** The look. Presets are assets (Content Browser: right-click > Miscellaneous > Data Asset > Dynamic Lens Preset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens")
	TObjectPtr<UDynamicLensPreset> Preset;

	/** Scales the preset's distortion amount for this camera only. 1 = as the preset. Keyable in Sequencer. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "2.0"))
	float AmountMultiplier = 1.f;

	/** Apply the preset's vignette to this camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Layers")
	bool bApplyVignette = true;

	/** Apply the preset's bokeh character (blades, cat's eye, swirl) to this camera's depth of field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Layers")
	bool bApplyBokeh = true;

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

	/** How the render is enlarged for the distortion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overscan")
	EDynamicLensOverscanMode OverscanMode = EDynamicLensOverscanMode::Dynamic;

	/** Dynamic: never overscan more than this factor (1.5 = 50% wider). Beyond it the corners go black instead of costing render time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overscan", meta = (EditCondition = "OverscanMode == EDynamicLensOverscanMode::Dynamic", ClampMin = "1.0", ClampMax = "2.0"))
	float MaxOverscan = 1.5f;

	/** Fixed: the constant overscan factor for the shot (1.2 = 20% wider render). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overscan", meta = (EditCondition = "OverscanMode == EDynamicLensOverscanMode::Fixed", ClampMin = "1.0", ClampMax = "2.0"))
	float FixedOverscan = 1.2f;

	/** Render the extra overscan pixels so the final frame keeps its full resolution (GPU cost grows with overscan squared). Off keeps the render cheaper but slightly softer at the edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Overscan")
	bool bScaleResolutionWithOverscan = true;

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
	/** Notes from the last evaluation (e.g. sensor fit fallback). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FString Notes;

	/** The CineCamera this component drives (first CineCameraComponent on the owner). */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	UCineCameraComponent* GetTargetCamera() const;

	/** Set the camera's filmback, squeeze and crop to the preset profile's native format (the sensor the lens data was made for). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dynamic Lens")
	void MatchCameraToProfile();

	/** Remove every trace of the effect from the camera (also called automatically when disabled or removed). */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	void ClearEffect();

	//~ UActorComponent
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void Apply(UCineCameraComponent* Cam);
	void EnsureHandler();
	bool DriveParametric(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float W, float H, float& OutNeededOverscan, FLensDistortionState& OutState);
	bool DriveSTMap(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float Focus, float W, float H, float& OutNeededOverscan, FLensDistortionState& OutState, float& OutCircleRadius);
	bool DriveProjection(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float W, float H, float AppliedOverscan, float& OutNeededOverscan, FLensDistortionState& OutState, float& OutCircleRadius);
	void ApplyRendering(UCineCameraComponent* Cam, const FLensDistortionState& State, float AppliedOverscan);
	void ApplyLook(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float CircleRadiusNorm, float Aspect);
	void CaptureLook(UCineCameraComponent* Cam);
	void RestoreLook(UCineCameraComponent* Cam);
	void StripForeignDistortionBlendables(UCineCameraComponent* Cam);

	UPROPERTY(Transient) TObjectPtr<ULensDistortionModelHandlerBase> Handler;
	UPROPERTY(Transient) TObjectPtr<ULensFile> TransientLensFile;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> ProjectionMap;
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
		float Overscan = 0.f; bool bCropOverscan = false; bool bScaleRes = false;
	} Backup;

	FDynamicLensEval LastEval;
	float LastCircleRadius = -1.f;
	bool bHasLastEval = false;
};
