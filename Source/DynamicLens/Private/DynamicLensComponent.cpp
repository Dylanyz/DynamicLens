#include "DynamicLensComponent.h"

#include "Camera/CameraActor.h"
#include "CameraCalibrationSubsystem.h"
#include "CineCameraComponent.h"
#include "Engine/Engine.h"
#include "LensDistortionModelHandlerBase.h"
#include "LensFileRendering.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SphericalLensDistortionModelHandler.h"

UDynamicLensComponent::UDynamicLensComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;   // after Sequencer has written focal length / focus
	bTickInEditor = true;
	bAutoActivate = true;
}

UCineCameraComponent* UDynamicLensComponent::GetTargetCamera() const
{
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<UCineCameraComponent>();
	}
	return nullptr;
}

void UDynamicLensComponent::OnUnregister()
{
	ClearEffect();
	Super::OnUnregister();
}

void UDynamicLensComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearEffect();
	Super::EndPlay(EndPlayReason);
}

void UDynamicLensComponent::BeginDestroy()
{
	ClearEffect();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UDynamicLensComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName Name = PropertyChangedEvent.GetPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bEnabled) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, RenderMode) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bApplyBokeh) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bApplyVignette) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Preset))
	{
		ClearEffect();   // re-applied cleanly on the next tick if still enabled
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDynamicLensComponent::EnsureHandler()
{
	if (!Handler)
	{
		Handler = NewObject<USphericalLensDistortionModelHandler>(this, NAME_None, RF_Transient);
	}
}

void UDynamicLensComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UCineCameraComponent* Cam = GetTargetCamera();
	if (!Cam || !bEnabled || !Preset)
	{
		ClearEffect();
		return;
	}
	if (AppliedCamera.IsValid() && AppliedCamera.Get() != Cam)
	{
		ClearEffect();
	}
	Apply(Cam);
}

void UDynamicLensComponent::Apply(UCineCameraComponent* Cam)
{
	const float Focal = Cam->CurrentFocalLength;
	const float Focus = FMath::Max(Cam->CurrentFocusDistance, 1.f);
	const float FStop = Cam->CurrentAperture;

	// effective sensor: anamorphic squeeze widens the desqueezed image, a crop preset trims it
	float W = Cam->Filmback.SensorWidth * FMath::Max(Cam->LensSettings.SqueezeFactor, 0.01f);
	float H = Cam->Filmback.SensorHeight;
	const float CropAspect = Cam->CropSettings.AspectRatio;
	if (CropAspect > KINDA_SMALL_NUMBER && H > KINDA_SMALL_NUMBER)
	{
		if (CropAspect > W / H) H = W / CropAspect; else W = H * CropAspect;
	}
	W = FMath::Max(W, 0.01f); H = FMath::Max(H, 0.01f);

	const FDynamicLensEval Eval = Preset->Evaluate(Focal, Focus, FStop, W, H, AmountMultiplier);

	if (!bLookCaptured)
	{
		CaptureLook(Cam);
	}
	AppliedCamera = Cam;

	// --- distortion state -> Epic's handler (displacement maps)
	EnsureHandler();
	FLensDistortionState State;
	State.DistortionInfo.Parameters = Eval.Params.ToArray();
	State.FocalLengthInfo.FxFy = FVector2D(Focal / W, Focal / H);
	State.ImageCenter.PrincipalPoint = FVector2D(0.5, 0.5);
	Handler->SetDistortionState(State);

	FCameraFilmbackSettings FB;
	FB.SensorWidth = W; FB.SensorHeight = H; FB.SensorAspectRatio = W / H;
	Handler->SetCameraFilmback(FB);

	// exact overscan: dense border solve of the (monotonic) radial model
	float OverscanFactor = DynamicLensMath::ComputeOverscan(Eval.Params, Focal / W, Focal / H);
	OverscanFactor = 1.f + (OverscanFactor - 1.f) * FMath::Max(OverscanMultiplier, 0.f);
	const float CamOverscan = FMath::Clamp(OverscanFactor - 1.f, 0.f, 1.f);
	Cam->Overscan = CamOverscan;
	Cam->bScaleResolutionWithOverscan = bScaleResolutionWithOverscan;
	bOverscanTouched = true;
	Handler->SetOverscanFactor(CamOverscan + 1.f);   // material and camera must agree (same as Epic's LensComponent)
	Handler->ProcessCurrentDistortion();

	// --- rendering path
	UCameraCalibrationSubsystem* Sub = GEngine ? GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>() : nullptr;
	ACameraActor* CamActor = Cast<ACameraActor>(GetOwner());
	const bool bWantSVE = (RenderMode == EDynamicLensRenderMode::TemporalSuperResolution) && Sub && CamActor;

	if (bWantSVE)
	{
		if (AppliedMID)
		{
			Cam->RemoveBlendable(AppliedMID);
			AppliedMID = nullptr;
		}
		FDisplacementMapBlendingParams Blend;
		Blend.BlendType = EDisplacementMapBlendType::OneFocusOneZoom;
		Blend.States[0] = State;
		Blend.PrincipalPoint = State.ImageCenter.PrincipalPoint;
		Sub->SetLensDistortionSVEState(CamActor, Blend, Handler, EDistortionRenderingMode::TemporalSuperResolution);
		bSVEActive = true;
		Cam->bCropOverscan = true;
	}
	else
	{
		if (bSVEActive && Sub && CamActor)
		{
			Sub->ClearLensDistortionSVEState(CamActor);
			bSVEActive = false;
		}
		UMaterialInstanceDynamic* MID = Handler->GetDistortionMID();
		if (MID != AppliedMID)
		{
			if (AppliedMID) Cam->RemoveBlendable(AppliedMID);
			if (MID) Cam->AddOrUpdateBlendable(MID, 1.f);
			AppliedMID = MID;
		}
		Cam->bCropOverscan = false;
	}

	// --- vignette + bokeh
	ApplyLook(Cam, Eval);

	LastFocalMm = Focal; LastFocusCm = Focus; LastFStop = FStop;
	LastSensorMm = FVector2D(W, H);
	LastParams = Eval.Params;
	LastOverscanFactor = CamOverscan + 1.f;
	LastVignette = (Eval.bVignette && bApplyVignette) ? Eval.VignetteIntensity : 0.f;
	CornerFieldAngleDeg = Eval.CornerFieldAngleDeg;
	CornerPupilVisible = Eval.CornerPupilVisible;
	BarrelRadiusLengthMm = FVector2D(Eval.BarrelRadiusMm, Eval.BarrelLengthMm);
	if (Preset->Profile)
	{
		float MinMm, MaxMm; Preset->Profile->GetFocalRange(MinMm, MaxMm);
		ProfileFocalRangeMm = FVector2D(MinMm, MaxMm);
	}
}

void UDynamicLensComponent::CaptureLook(UCineCameraComponent* Cam)
{
	const FPostProcessSettings& P = Cam->PostProcessSettings;
	Backup.bBlade = P.bOverride_DepthOfFieldBladeCount; Backup.Blade = P.DepthOfFieldBladeCount;
	Backup.bPetzval = P.bOverride_DepthOfFieldPetzvalBokeh; Backup.Petzval = P.DepthOfFieldPetzvalBokeh;
	Backup.bPetzvalFalloff = P.bOverride_DepthOfFieldPetzvalBokehFalloff; Backup.PetzvalFalloff = P.DepthOfFieldPetzvalBokehFalloff;
	Backup.bExclBox = P.bOverride_DepthOfFieldPetzvalExclusionBoxExtents; Backup.ExclBox = P.DepthOfFieldPetzvalExclusionBoxExtents;
	Backup.bExclRadius = P.bOverride_DepthOfFieldPetzvalExclusionBoxRadius; Backup.ExclRadius = P.DepthOfFieldPetzvalExclusionBoxRadius;
	Backup.bBarrelRadius = P.bOverride_DepthOfFieldBarrelRadius; Backup.BarrelRadius = P.DepthOfFieldBarrelRadius;
	Backup.bBarrelLength = P.bOverride_DepthOfFieldBarrelLength; Backup.BarrelLength = P.DepthOfFieldBarrelLength;
	Backup.bVignette = P.bOverride_VignetteIntensity; Backup.Vignette = P.VignetteIntensity;
	Backup.Overscan = Cam->Overscan; Backup.bCropOverscan = Cam->bCropOverscan; Backup.bScaleRes = Cam->bScaleResolutionWithOverscan;
	bLookCaptured = true;
}

void UDynamicLensComponent::RestoreLook(UCineCameraComponent* Cam)
{
	if (!bLookCaptured) return;
	FPostProcessSettings& P = Cam->PostProcessSettings;
	if (bLookApplied)
	{
		P.bOverride_DepthOfFieldBladeCount = Backup.bBlade; P.DepthOfFieldBladeCount = Backup.Blade;
		P.bOverride_DepthOfFieldPetzvalBokeh = Backup.bPetzval; P.DepthOfFieldPetzvalBokeh = Backup.Petzval;
		P.bOverride_DepthOfFieldPetzvalBokehFalloff = Backup.bPetzvalFalloff; P.DepthOfFieldPetzvalBokehFalloff = Backup.PetzvalFalloff;
		P.bOverride_DepthOfFieldPetzvalExclusionBoxExtents = Backup.bExclBox; P.DepthOfFieldPetzvalExclusionBoxExtents = Backup.ExclBox;
		P.bOverride_DepthOfFieldPetzvalExclusionBoxRadius = Backup.bExclRadius; P.DepthOfFieldPetzvalExclusionBoxRadius = Backup.ExclRadius;
		P.bOverride_DepthOfFieldBarrelRadius = Backup.bBarrelRadius; P.DepthOfFieldBarrelRadius = Backup.BarrelRadius;
		P.bOverride_DepthOfFieldBarrelLength = Backup.bBarrelLength; P.DepthOfFieldBarrelLength = Backup.BarrelLength;
		P.bOverride_VignetteIntensity = Backup.bVignette; P.VignetteIntensity = Backup.Vignette;
	}
	if (bOverscanTouched)
	{
		Cam->Overscan = Backup.Overscan;
		Cam->bCropOverscan = Backup.bCropOverscan;
		Cam->bScaleResolutionWithOverscan = Backup.bScaleRes;
	}
	bLookApplied = false;
	bOverscanTouched = false;
	bLookCaptured = false;
	bHasLastEval = false;
}

void UDynamicLensComponent::ApplyLook(UCineCameraComponent* Cam, const FDynamicLensEval& Eval)
{
	const bool bDoBokeh = bApplyBokeh && Eval.bBokeh;
	const bool bDoVignette = bApplyVignette && Eval.bVignette;

	auto SameAsLast = [&]()
	{
		if (!bHasLastEval) return false;
		if (LastEval.bBokeh != bDoBokeh || LastEval.bVignette != bDoVignette) return false;
		if (bDoBokeh && (LastEval.Blades != Eval.Blades ||
			!FMath::IsNearlyEqual(LastEval.BarrelRadiusMm, Eval.BarrelRadiusMm, 1e-3f) ||
			!FMath::IsNearlyEqual(LastEval.BarrelLengthMm, Eval.BarrelLengthMm, 1e-3f) ||
			!FMath::IsNearlyEqual(LastEval.Petzval, Eval.Petzval, 1e-3f) ||
			!FMath::IsNearlyEqual(LastEval.PetzvalFalloff, Eval.PetzvalFalloff, 1e-3f) ||
			!LastEval.SwirlExclusionBox.Equals(Eval.SwirlExclusionBox, 1e-3) ||
			!FMath::IsNearlyEqual(LastEval.SwirlExclusionRadius, Eval.SwirlExclusionRadius, 1e-3f))) return false;
		if (bDoVignette && !FMath::IsNearlyEqual(LastEval.VignetteIntensity, Eval.VignetteIntensity, 1e-3f)) return false;
		return true;
	};
	if (SameAsLast()) return;

	FPostProcessSettings& P = Cam->PostProcessSettings;
	if (bDoBokeh)
	{
		P.bOverride_DepthOfFieldBladeCount = true; P.DepthOfFieldBladeCount = FMath::Clamp(Eval.Blades, 4, 16);
		P.bOverride_DepthOfFieldPetzvalBokeh = true; P.DepthOfFieldPetzvalBokeh = Eval.Petzval;
		P.bOverride_DepthOfFieldPetzvalBokehFalloff = true; P.DepthOfFieldPetzvalBokehFalloff = Eval.PetzvalFalloff;
		P.bOverride_DepthOfFieldPetzvalExclusionBoxExtents = true; P.DepthOfFieldPetzvalExclusionBoxExtents = FVector2f(Eval.SwirlExclusionBox);
		P.bOverride_DepthOfFieldPetzvalExclusionBoxRadius = true; P.DepthOfFieldPetzvalExclusionBoxRadius = Eval.SwirlExclusionRadius;
		P.bOverride_DepthOfFieldBarrelRadius = true; P.DepthOfFieldBarrelRadius = Eval.BarrelRadiusMm;
		P.bOverride_DepthOfFieldBarrelLength = true; P.DepthOfFieldBarrelLength = Eval.BarrelLengthMm;
	}
	else
	{
		P.bOverride_DepthOfFieldBladeCount = Backup.bBlade; P.DepthOfFieldBladeCount = Backup.Blade;
		P.bOverride_DepthOfFieldPetzvalBokeh = Backup.bPetzval; P.DepthOfFieldPetzvalBokeh = Backup.Petzval;
		P.bOverride_DepthOfFieldPetzvalBokehFalloff = Backup.bPetzvalFalloff; P.DepthOfFieldPetzvalBokehFalloff = Backup.PetzvalFalloff;
		P.bOverride_DepthOfFieldPetzvalExclusionBoxExtents = Backup.bExclBox; P.DepthOfFieldPetzvalExclusionBoxExtents = Backup.ExclBox;
		P.bOverride_DepthOfFieldPetzvalExclusionBoxRadius = Backup.bExclRadius; P.DepthOfFieldPetzvalExclusionBoxRadius = Backup.ExclRadius;
		P.bOverride_DepthOfFieldBarrelRadius = Backup.bBarrelRadius; P.DepthOfFieldBarrelRadius = Backup.BarrelRadius;
		P.bOverride_DepthOfFieldBarrelLength = Backup.bBarrelLength; P.DepthOfFieldBarrelLength = Backup.BarrelLength;
	}
	if (bDoVignette)
	{
		P.bOverride_VignetteIntensity = true; P.VignetteIntensity = Eval.VignetteIntensity;
	}
	else
	{
		P.bOverride_VignetteIntensity = Backup.bVignette; P.VignetteIntensity = Backup.Vignette;
	}
	bLookApplied = true;
	LastEval = Eval;
	LastEval.bBokeh = bDoBokeh;
	LastEval.bVignette = bDoVignette;
	bHasLastEval = true;
}

void UDynamicLensComponent::ClearEffect()
{
	UCineCameraComponent* Cam = AppliedCamera.IsValid() ? AppliedCamera.Get() : GetTargetCamera();
	if (Cam)
	{
		if (AppliedMID)
		{
			Cam->RemoveBlendable(AppliedMID);
		}
		if (bSVEActive)
		{
			if (UCameraCalibrationSubsystem* Sub = GEngine ? GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>() : nullptr)
			{
				if (ACameraActor* CamActor = Cast<ACameraActor>(GetOwner()))
				{
					Sub->ClearLensDistortionSVEState(CamActor);
				}
			}
		}
		RestoreLook(Cam);
	}
	AppliedMID = nullptr;
	bSVEActive = false;
	AppliedCamera = nullptr;
	LastOverscanFactor = 1.f;
	LastVignette = 0.f;
}
