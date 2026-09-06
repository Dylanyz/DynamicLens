// DynamicLens — the component you add to a CineCameraActor.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicLensTypes.h"
#include "DynamicLensComponent.generated.h"

class UCineCameraComponent;
class ULensDistortionModelHandlerBase;
class UMaterialInstanceDynamic;

/** Where in the frame the distortion is rendered. */
UENUM(BlueprintType)
enum class EDynamicLensRenderMode : uint8
{
	/** Post-process material on the camera. Works everywhere (editor viewport, PIE, Movie Render Queue/Graph). Resamples the image once. */
	PostProcessMaterial UMETA(DisplayName = "Post Process Material"),
	/** Applied inside Temporal Super Resolution: sharpest result, no extra resample. Needs TSR as the anti-aliasing method (r.TSR.LensDistortion=1). Falls back to the primary upscale pass otherwise. */
	TemporalSuperResolution UMETA(DisplayName = "Inside TSR (sharpest)"),
};

/**
 * Dynamic Lens: focal-length / focus / f-stop driven distortion, vignette and bokeh character for the CineCamera
 * on the same actor. Pick a preset asset; the rest happens every frame, in the editor (Realtime viewport) and in renders.
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

	/** How the distortion is rendered. Post Process Material is the safe default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Advanced")
	EDynamicLensRenderMode RenderMode = EDynamicLensRenderMode::PostProcessMaterial;

	/** Overscan is computed exactly from the distortion (dense border solve), so 1 is the right value. Only lower it to trade black corners for render time, or raise it for extra safety margin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Advanced", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.9", UIMax = "1.1"))
	float OverscanMultiplier = 1.f;

	/** Render the extra overscan pixels so the final frame keeps its full resolution (GPU cost grows with overscan squared). Off keeps the render cheaper but slightly softer at the edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Lens|Advanced")
	bool bScaleResolutionWithOverscan = true;

	// --- read-only feedback -----------------------------------------------------------------
	/** Last evaluated focal length (mm). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastFocalMm = 0.f;
	/** Measured focal range of the preset's profile (mm). Outside it the preset's Out Of Range mode applies. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FVector2D ProfileFocalRangeMm = FVector2D::ZeroVector;
	/** Last evaluated focus distance (cm). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastFocusCm = 0.f;
	/** Last evaluated f-stop. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastFStop = 0.f;
	/** Sensor size actually used (after squeeze and crop), mm. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FVector2D LastSensorMm = FVector2D::ZeroVector;
	/** Distortion coefficients pushed to the renderer. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FDynamicLensParams LastParams;
	/** Overscan factor applied to the camera (1.05 = 5% wider render). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastOverscanFactor = 1.f;
	/** Vignette intensity pushed to the camera. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float LastVignette = 0.f;
	/** Field angle at the frame corner (degrees). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float CornerFieldAngleDeg = 0.f;
	/** Fraction of the entrance pupil still visible at the frame corner (1 = no cat's eye, 0.5 = half clipped). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") float CornerPupilVisible = 1.f;
	/** Barrel radius / length (mm) handed to the depth of field. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Dynamic Lens|Debug") FVector2D BarrelRadiusLengthMm = FVector2D::ZeroVector;

	/** The CineCamera this component drives (first CineCameraComponent on the owner). */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	UCineCameraComponent* GetTargetCamera() const;

	/** Remove every trace of the effect from the camera (also called automatically when disabled or removed). */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	void ClearEffect();

	//~ UActorComponent
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void Apply(UCineCameraComponent* Cam);
	void EnsureHandler();
	void ApplyLook(UCineCameraComponent* Cam, const FDynamicLensEval& Eval);
	void CaptureLook(UCineCameraComponent* Cam);
	void RestoreLook(UCineCameraComponent* Cam);

	UPROPERTY(Transient) TObjectPtr<ULensDistortionModelHandlerBase> Handler;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> AppliedMID;
	UPROPERTY(Transient) TWeakObjectPtr<UCineCameraComponent> AppliedCamera;

	bool bSVEActive = false;
	bool bLookCaptured = false;
	bool bLookApplied = false;
	bool bOverscanTouched = false;

	// originals of what we override on the camera
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
	bool bHasLastEval = false;
};
