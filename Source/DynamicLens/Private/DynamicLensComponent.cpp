// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#include "DynamicLensComponent.h"

#include "Camera/CameraActor.h"
#include "CameraCalibrationSubsystem.h"
#include "CineCameraComponent.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "LensDistortionModelHandlerBase.h"
#include "LensFile.h"
#include "LensFileRendering.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "DynamicLensLibrary.h"
#include "AssetRegistry/AssetData.h"
#include "Models/SphericalLensModel.h"
#include "SphericalLensDistortionModelHandler.h"
#include "AnamorphicLensDistortionModelHandler.h"

namespace
{
	constexpr int32 ProjectionMapSize = 256;
	// Highest overscan the component will apply (render = frame * O). Presets default to 2; 2-4 trades centre
	// sharpness for field (Epic clamps the resolution fraction to 2, so past O=2 the centre softens by 2/O).
	constexpr float MaxOverscanCeiling = 4.f;
	const FName CircleParamRadius(TEXT("Radius"));
	const FName CircleParamSoftness(TEXT("Softness"));
	const FName CircleParamAspect(TEXT("Aspect"));
	const FName CircleParamFalloff(TEXT("FalloffPower"));
	const FName CircleParamOpacity(TEXT("Opacity"));
	const FName CircleParamCenterX(TEXT("CenterX"));
	const FName CircleParamCenterY(TEXT("CenterY"));
	const FName CircleParamEllipticity(TEXT("Ellipticity"));
	const FName CircleParamWobble(TEXT("Wobble"));
	const FName CircleParamWobbleLobes(TEXT("WobbleLobes"));
	const FName CircleParamWobbleSeed(TEXT("WobbleSeed"));
	const FName CircleParamEdgeNoise(TEXT("EdgeNoise"));
	const FName CircleParamNoiseScale(TEXT("NoiseScale"));
	const FName CircleParamMaskStrength(TEXT("MaskStrength"));
	const FName CircleParamMask(TEXT("Mask"));
	const FName CircleParamCAR(TEXT("CAR"));
	const FName CircleParamCAG(TEXT("CAG"));
	const FName CircleParamCAB(TEXT("CAB"));
	const FName CircleParamSoftWobble(TEXT("SoftWobble"));
	const FName CircleParamSoftWobbleLobes(TEXT("SoftWobbleLobes"));
	const FName CircleParamSoftWobbleSeed(TEXT("SoftWobbleSeed"));
	const FName CircleParamNoiseDepth(TEXT("NoiseDepth"));
	const FName CircleParamNoiseBlur(TEXT("NoiseBlur"));
	const FName CircleParamNoiseDetail(TEXT("NoiseDetail"));
	const FName CircleParamNoiseStretch(TEXT("NoiseStretch"));
	const FName CircleParamNoiseSeed(TEXT("NoiseSeed"));
	const FName CircleParamNoiseContrast(TEXT("NoiseContrast"));
	const FName CircleParamScatter(TEXT("Scatter"));
	const FName CircleParamFadeReach(TEXT("FadeReach"));
	const FName CircleParamFadeAmount(TEXT("FadeAmount"));
	const FName CircleParamFadeCurve(TEXT("FadeCurve"));
	const FName CircleParamSquareness(TEXT("Squareness"));

	// Scalability below Cinematic lowers these, and with them every bokeh setting the preset drives (swirl, barrel, blades).
	// Background compositing matters most: at 1 (High) bright highlights are not scattered as sprites, and the sprite
	// path is where Petzval stretch is applied regardless of bokeh shape. Measured 2026-09-24 at High scalability.
	// Global, so shared by every component: the first claim saves and raises them, the last release restores.
	struct FBokehCVar { const TCHAR* Name; int32 Wanted; };
	const FBokehCVar BokehCVars[] = {
		{ TEXT("r.DOF.Scatter.BackgroundCompositing"), 2 },
		{ TEXT("r.DOF.Gather.EnableBokehSettings"), 1 },
		{ TEXT("r.DOF.Scatter.EnableBokehSettings"), 1 },
		{ TEXT("r.DOF.Recombine.EnableBokehSettings"), 1 },
	};
	int32 BokehQualityClaims = 0;
	int32 SavedBokehCVars[UE_ARRAY_COUNT(BokehCVars)] = {};

	bool BokehSimulationOff()
	{
		for (const FBokehCVar& Entry : BokehCVars)
		{
			const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Entry.Name);
			if (CVar && CVar->GetInt() < Entry.Wanted)
			{
				return true;
			}
		}
		return false;
	}
}

UDynamicLensComponent::UDynamicLensComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	NewPresetFolder.Path = TEXT("/Game/DynamicLens/Presets");   // after Sequencer has written focal length / focus
	bTickInEditor = true;
	bAutoActivate = true;
	ImageCircleMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/DynamicLens/Materials/M_DL_ImageCircle.M_DL_ImageCircle")));
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

void UDynamicLensComponent::OnRegister()
{
	Super::OnRegister();
	if (AActor* Owner = GetOwner())
	{
		// procedural camera rigs (Black Eye etc.) set FOV / focal length in the actor tick: run after it, same frame
		AddTickPrerequisiteActor(Owner);
	}
}

void UDynamicLensComponent::BeginPlay()
{
	Super::BeginPlay();
	// Movie Render Queue/Graph read the camera's overscan once when a shot starts: make sure it is already there.
	if (UCineCameraComponent* Cam = GetTargetCamera())
	{
		if (bEnabled && HasLens())
		{
			Apply(Cam);
		}
	}
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
	const FName Member = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : Name;
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		// slider drag: the editor keeps the component unregistered until the mouse is released, so ticks stop.
		// Apply straight away so the viewport keeps previewing while dragging.
		if (UCineCameraComponent* Cam = GetTargetCamera())
		{
			if (Member == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Camera)) PushCameraQuick(Cam);
			if (bEnabled && HasLens()) Apply(Cam);
		}
		Super::PostEditChangeProperty(PropertyChangedEvent);
		return;
	}
	// ticking an override copies the preset's block in, so editing starts from the preset's values
	if (Preset)
	{
		if (Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bOverrideDistortion) && bOverrideDistortion) Distortion = Preset->Distortion;
		if (Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bOverrideImageCircle) && bOverrideImageCircle) ImageCircle = Preset->ImageCircle;
		if (Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bOverrideVignette) && bOverrideVignette) Vignette = Preset->Vignette;
		if (Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bOverrideBokeh) && bOverrideBokeh) Bokeh = Preset->Bokeh;
		if (Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bOverrideOverscan) && bOverrideOverscan) Overscan = Preset->Overscan;
	}
	if (Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bOverrideDistortion) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bOverrideOverscan) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bOverrideImageCircle) ||
		Member == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Distortion) ||
		Member == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Overscan) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bEnabled) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bApplyDistortion) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, RenderMode) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bApplyBokeh) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bApplyVignette) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bApplyImageCircle) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, SensorFit) ||
		Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Preset))
	{
		ClearEffect();   // re-applied cleanly on the next tick if still enabled
		TransientLensFile = nullptr;
		LensFileSTMapIndex = -1;
	}
	const bool bLensChanged = Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Preset)
		|| Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bOverrideDistortion)
		|| (Member == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Distortion) && Name == GET_MEMBER_NAME_CHECKED(FDynamicLensDistortion, Profile));
	if (bLensChanged && MatchCamera.bOnPresetChange)
	{
		MatchCameraToProfile();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Preset) && MatchCamera.bRefreshOverrides)
	{
		CopyAllFromPreset();
	}
	if (Member == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Camera))
	{
		if (UCineCameraComponent* Cam = GetTargetCamera()) PushCameraQuick(Cam);
	}
	UpdateProfileInfo();
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// don't wait for the next tick to show the change
	if (UCineCameraComponent* Cam = GetTargetCamera(); Cam && bEnabled && HasLens())
	{
		Apply(Cam);
	}
}
#endif

void UDynamicLensComponent::EnsureHandler()
{
	// the handler class follows the profile's model: Epic draws Brown-Conrady and 3DE4 anamorphic with different handlers
	const bool bAnamorphic = Resolved.Distortion.Profile && Resolved.Distortion.Profile->IsAnamorphicModel();
	UClass* Want = bAnamorphic ? UAnamorphicLensDistortionModelHandler::StaticClass() : USphericalLensDistortionModelHandler::StaticClass();
	if (Handler && Handler->GetClass() != Want)
	{
		if (UCineCameraComponent* Cam = AppliedCamera.Get())
		{
			if (AppliedMID) Cam->RemoveBlendable(AppliedMID);
			if (bSVEActive)
			{
				if (UCameraCalibrationSubsystem* Sub = GEngine ? GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>() : nullptr)
				{
					if (ACameraActor* CamActor = Cast<ACameraActor>(GetOwner())) Sub->ClearLensDistortionSVEState(CamActor);
				}
				bSVEActive = false;
			}
		}
		AppliedMID = nullptr;
		Handler = nullptr;
		TransientLensFile = nullptr;
		LensFileSTMapIndex = -1;
	}
	if (!Handler)
	{
		Handler = NewObject<ULensDistortionModelHandlerBase>(this, Want, NAME_None, RF_Transient);
	}
}

void UDynamicLensComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UCineCameraComponent* Cam = GetTargetCamera();
	if (!Cam || !bEnabled || !HasLens())
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

void UDynamicLensComponent::MatchCameraToProfile()
{
	UCineCameraComponent* Cam = GetTargetCamera();
	const UDynamicLensProfile* P = ResolveSettings().Distortion.Profile;
	if (!Cam || !P) return;
#if WITH_EDITOR
	Cam->Modify();
#endif
	const float Squeeze = FMath::Max(P->Squeeze, 1.f);
	if (MatchCamera.bFilmback)
	{
		Cam->Filmback.SensorWidth = P->NativeSensorMm.X / Squeeze;
		Cam->Filmback.SensorHeight = P->NativeSensorMm.Y;
		Cam->Filmback.SensorAspectRatio = Cam->Filmback.SensorWidth / FMath::Max(Cam->Filmback.SensorHeight, 0.01f);
	}
	if (MatchCamera.bSqueeze)
	{
		Cam->LensSettings.SqueezeFactor = Squeeze;
	}
	if (MatchCamera.bFocalLength)
	{
		if (const float Locked = P->GetLockedFocal(Cam->CurrentFocalLength); Locked > KINDA_SMALL_NUMBER)
		{
			Cam->SetCurrentFocalLength(Locked);
		}
		else
		{
			float MinMm, MaxMm; P->GetFocalRange(MinMm, MaxMm);
			if (MaxMm > MinMm && (Cam->CurrentFocalLength < MinMm || Cam->CurrentFocalLength > MaxMm))
			{
				Cam->SetCurrentFocalLength(FMath::Clamp(Cam->CurrentFocalLength, MinMm, MaxMm));   // a zoom: stay inside what was measured
			}
		}
	}
	if (MatchCamera.bCrop)
	{
		Cam->CropSettings.AspectRatio = 0.f;
	}
	if (MatchCamera.bRefreshOverrides)
	{
		CopyAllFromPreset();
	}
	PullCameraQuick(Cam);
	ClearEffect();
	TransientLensFile = nullptr;
	LensFileSTMapIndex = -1;
}

// ------------------------------------------------------------------------------------------------ apply

void UDynamicLensComponent::Apply(UCineCameraComponent* Cam)
{
	Resolved = ResolveSettings();
	PullCameraQuick(Cam);
	if (ProfileInfo.IsEmpty() || InfoProfile != Resolved.Distortion.Profile || !FMath::IsNearlyEqual(InfoFocal, Cam->CurrentFocalLength, 0.01f))
	{
		InfoProfile = Resolved.Distortion.Profile;
		InfoFocal = Cam->CurrentFocalLength;
		UpdateProfileInfo();
	}
	if (Resolved.Distortion.bLockFocalLength && Resolved.Distortion.Profile)
	{
		const float Locked = Resolved.Distortion.Profile->GetLockedFocal(Cam->CurrentFocalLength);
		if (Locked > KINDA_SMALL_NUMBER && !FMath::IsNearlyEqual(Cam->CurrentFocalLength, Locked, 1e-3f))
		{
			Cam->SetCurrentFocalLength(Locked);
		}
	}
	const float Focal = FMath::Max(Cam->CurrentFocalLength, 0.1f);
	const float Focus = FMath::Max(Cam->CurrentFocusDistance, 1.f);
	const float FStop = Cam->CurrentAperture;

	// effective sensor: anamorphic squeeze widens the desqueezed image, a crop preset trims it
	float W = Cam->Filmback.SensorWidth * FMath::Max(Cam->LensSettings.SqueezeFactor, 0.01f);
	float H = Cam->Filmback.SensorHeight;
	const float WFull = FMath::Max(W, 0.01f), HFull = FMath::Max(H, 0.01f);   // the whole sensor, before any crop
	const float CropAspect = Cam->CropSettings.AspectRatio;
	if (CropAspect > KINDA_SMALL_NUMBER && H > KINDA_SMALL_NUMBER)
	{
		if (CropAspect > W / H) H = W / CropAspect; else W = H * CropAspect;
	}
	W = FMath::Max(W, 0.01f); H = FMath::Max(H, 0.01f);

	FDynamicLensEval Eval = Resolved.Evaluate(Focal, Focus, FStop, W, H, AmountMultiplier, Cam->LensSettings.DiaphragmBladeCount, Cam->LensSettings.SqueezeFactor);
	Eval.VignetteIntensity = FMath::Clamp(Eval.VignetteIntensity * VignetteMultiplier, 0.f, 1.f);
	Eval.Petzval *= SwirlMultiplier;

	if (!bLookCaptured)
	{
		CaptureLook(Cam);
	}
	if (!bStrippedForeign)
	{
		StripForeignDistortionBlendables(Cam);
		bStrippedForeign = true;
	}
	AppliedCamera = Cam;
	Notes.Reset();
	EnsureHandler();

	const UDynamicLensProfile* Profile = Resolved.Distortion.Profile;
	const EDynamicLensProfileType Type = Profile ? Profile->Type : EDynamicLensProfileType::Parametric;

	FLensDistortionState State;
	float Needed = 1.f;
	float Applied = 1.f;
	float CircleRadius = Eval.ImageCircleRadiusNorm;   // 0 = no circle

	auto MinCircle = [&](float R) { if (R > 0.f) CircleRadius = (CircleRadius > 0.f) ? FMath::Min(CircleRadius, R) : R; };
	// what the render can show is the distorted image of the overscanned source rectangle, not a circle: an ellipse
	// through its half-extents (DataRx, DataRy, half-frame-width units) covers the corners while there are pixels and
	// sweeps in at the picture's own rate as the lens asks for more than the ceiling gives
	float DataRx = 0.f, DataRy = 0.f, DataCx = 0.f, DataCy = 0.f;   // axis extents and the distorted corner
	auto MinData = [&](float Rx, float Ry, float CxIn, float CyIn)
	{
		if (Rx <= 0.f || Ry <= 0.f) return;
		if (DataRx <= 0.f || Rx < DataRx) { DataRx = Rx; DataCx = CxIn; }
		if (DataRy <= 0.f || Ry < DataRy) { DataRy = Ry; DataCy = CyIn; }
	};
	const float OverscanCap = (RenderMode == EDynamicLensRenderMode::TemporalSuperResolution) ? 2.f : MaxOverscanCeiling;   // Epic clamps the SVE path to 2
	const float Ceiling = FMath::Max((Resolved.Overscan.Mode == EDynamicLensOverscanMode::Fixed) ? Resolved.Overscan.FixedOverscan : Resolved.Overscan.MaxOverscan, 1.f);
	// Dynamic overscan: round up to a step and only shrink by whole steps, so breathing doesn't resize the render every frame
	auto DynamicApplied = [&](float NeededIn)
	{
		const float MaxO = FMath::Max(Resolved.Overscan.MaxOverscan, 1.f);
		float V = FMath::Clamp(NeededIn, 1.f, MaxO);
		const float Step = Resolved.Overscan.DynamicStep;
		if (Step > 1e-4f)
		{
			V = 1.f + FMath::CeilToFloat((V - 1.f) / Step - 1e-4f) * Step;
			if (LastDynamicOverscan > 0.f && V < LastDynamicOverscan && NeededIn > LastDynamicOverscan - Step)
			{
				V = LastDynamicOverscan;   // hysteresis: hold until the need drops a full step
			}
			V = FMath::Min(V, MaxO);
		}
		LastDynamicOverscan = V;
		return V;
	};

	if (!bApplyDistortion)
	{
		ClearDistortionRendering(Cam);
	}
	else if (Type == EDynamicLensProfileType::Projection && Profile)
	{
		// fisheye maths: the whole frame wants as much source as it can get; the image circle takes the rest
		Applied = (Resolved.Overscan.Mode == EDynamicLensOverscanMode::Fixed) ? Resolved.Overscan.FixedOverscan : Resolved.Overscan.MaxOverscan;
		Applied = FMath::Min(Applied, OverscanCap);   // the bake must use what the camera will actually render
		float Rx = 0.f, Ry = 0.f;
		if (DriveProjection(Cam, Eval, Focal, W, H, Applied, Needed, State, Rx, Ry))
		{
			MinData(Rx, Ry, Rx * 0.7071f, Ry * 0.7071f);   // a true circle/ellipse
		}
	}
	else if (Type == EDynamicLensProfileType::STMap && Profile)
	{
		float Circle = 0.f;
		if (DriveSTMap(Cam, Eval, Focal, Focus, W, H, WFull, HFull, Needed, State, Circle))
		{
			Applied = (Resolved.Overscan.Mode == EDynamicLensOverscanMode::Fixed) ? Resolved.Overscan.FixedOverscan : DynamicApplied(Needed);
			// the edge of what the render can show, computed every frame at the overscan ceiling so it exists
			// continuously (outside the corners while there are pixels, sweeping inward at the picture's own rate as
			// the lens asks for more than the ceiling gives) instead of switching on at the corners
			const float Cover = Ceiling / FMath::Max(Needed, 1.f);   // fraction of each source edge the ceiling reaches (map border needs Needed, centre needs 1)
			MinData(Cover, Cover * H / W, Cover, Cover * H / W);      // a scaled rectangle: the corner is at the extents
			if (Circle > 0.f) MinData(Circle, Circle * H / W, Circle, Circle * H / W);   // where the extrapolated map data ends
		}
		else
		{
			Notes += TEXT("ST map could not be evaluated. ");
		}
	}
	else if (Profile && Profile->IsAnamorphicModel())
	{
		if (DriveAnamorphic(Cam, Focal, W, H, Needed, State))
		{
			Applied = (Resolved.Overscan.Mode == EDynamicLensOverscanMode::Fixed) ? Resolved.Overscan.FixedOverscan : DynamicApplied(Needed);
			const float Cover = Ceiling / FMath::Max(Needed, 1.f);   // as for ST maps: the source rectangle the ceiling reaches
			MinData(Cover, Cover * H / W, Cover, Cover * H / W);
		}
		else
		{
			Notes += TEXT("Anamorphic profile has no usable rows. ");
		}
	}
	else
	{
		DriveParametric(Cam, Eval, Focal, W, H, Needed, State);
		Applied = (Resolved.Overscan.Mode == EDynamicLensOverscanMode::Fixed) ? Resolved.Overscan.FixedOverscan : DynamicApplied(Needed);
		float Rx = 0.f, Ry = 0.f, CxD = 0.f, CyD = 0.f;
		DynamicLensMath::ValidExtents(Eval.Params, Focal / W, Focal / H, Ceiling, Rx, Ry);
		DynamicLensMath::ValidCorner(Eval.Params, Focal / W, Focal / H, Ceiling, CxD, CyD);
		MinData(Rx, Ry, CxD, CyD);
	}

	Applied = FMath::Clamp(Applied, 1.f, OverscanCap);
	if (Applied > 2.001f || (OverscanCap < 2.001f && Ceiling > 2.001f))
	{
		Notes += (RenderMode == EDynamicLensRenderMode::TemporalSuperResolution)
			? TEXT("Overscan above 2 is not possible inside TSR (Epic clamps it): capped at 2. Use Post Process Material for more. ")
			: FString::Printf(TEXT("Overscan %.2f: centre resolution is %.0f%% (Epic caps the render at 2x the frame). "), Applied, 200.f / Applied);
	}

	// vignette and cat's eye belong to the picture you can see: if the image circle is inside the frame corners,
	// evaluate them at the circle's edge instead of the (black) frame corner
	// pick the mask: the lens's physical image circle, or the data ellipse, whichever reaches the corner first
	const float CornerNorm = FMath::Sqrt(1.f + FMath::Square(H / W));
	const float Cx = 1.f / CornerNorm, Cy = (H / W) / CornerNorm;
	float CircleEll = 1.f, CircleSq = 2.f;
	float MaskCornerR = (CircleRadius > 0.f) ? CircleRadius : 1e6f;
	if (DataRx > 0.f && DataRy > 0.f)
	{
		// superellipse through the axis extents and the distorted corner: hugs the real valid region
		const float N = DynamicLensMath::SolveSquareness(DataRx, DataRy, DataCx, DataCy);
		const float DataCornerR = 1.f / FMath::Pow(FMath::Pow(Cx / DataRx, N) + FMath::Pow(Cy / DataRy, N), 1.f / N);
		if (DataCornerR < MaskCornerR)
		{
			CircleRadius = DataRx; CircleEll = DataRy / DataRx; CircleSq = N; MaskCornerR = DataCornerR;
		}
	}
	ActiveMask = (MaskCornerR >= CornerNorm || (CircleRadius <= 0.f)) ? TEXT("none (frame fully inside)")
		: (CircleEll == 1.f && CircleSq == 2.f) ? TEXT("lens image circle") : TEXT("data limit (edge of what the render can show)");
	if (CircleRadius > 0.f && MaskCornerR < CornerNorm)
	{
		const float S = MaskCornerR / CornerNorm;
		const FDynamicLensEval EdgeEval = Resolved.Evaluate(Focal, Focus, FStop, W * S, H * S, AmountMultiplier, Cam->LensSettings.DiaphragmBladeCount, Cam->LensSettings.SqueezeFactor);
		Eval.VignetteIntensity = FMath::Clamp(EdgeEval.VignetteIntensity * VignetteMultiplier, 0.f, 1.f);
		Eval.CornerPupilVisible = EdgeEval.CornerPupilVisible;
		Eval.CornerFieldAngleDeg = EdgeEval.CornerFieldAngleDeg;
		Eval.BarrelRadiusMm = EdgeEval.BarrelRadiusMm;
		Eval.BarrelLengthMm = EdgeEval.BarrelLengthMm;
	}

	UpdateNearClip(Cam, bApplyDistortion && Type == EDynamicLensProfileType::Projection && FisheyeNearClipCm > 0.f);
	if (bApplyDistortion) ApplyRendering(Cam, State, Applied);
	ApplyLook(Cam, Eval, bApplyImageCircle ? CircleRadius : 0.f, W / H, CircleEll, CircleSq);
	if (bApplyBokeh && Eval.bBokeh && Eval.bDriveAccumulationDOF)
	{
		ApplyAccumulationDOF(Eval);
	}
	else if (bAccumApplied)
	{
		RestoreAccumulationDOF();
	}

	LastFocalMm = Focal; LastFocusCm = Focus; LastFStop = FStop;
	LastSensorMm = FVector2D(W, H);
	LastParams = Eval.Params;
	NeededOverscanFactor = Needed;
	LastOverscanFactor = Applied;
	LastVignette = (Eval.bVignette && bApplyVignette) ? Eval.VignetteIntensity : 0.f;
	CornerFieldAngleDeg = Eval.CornerFieldAngleDeg;
	CornerPupilVisible = Eval.CornerPupilVisible;
	BarrelRadiusLengthMm = FVector2D(Eval.BarrelRadiusMm, Eval.BarrelLengthMm);
	ImageCircleRadius = bApplyImageCircle ? CircleRadius : 0.f;
	ProfileCoverage = Profile ? Profile->Coverage : TEXT("no profile");
}

bool UDynamicLensComponent::DriveParametric(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float W, float H, float& OutNeededOverscan, FLensDistortionState& OutState)
{
	OutState.DistortionInfo.Parameters = Eval.Params.ToArray();
	OutState.FocalLengthInfo.FxFy = FVector2D(Focal / W, Focal / H);
	OutState.ImageCenter.PrincipalPoint = FVector2D(0.5, 0.5);
	Handler->SetDistortionState(OutState);
	FCameraFilmbackSettings FB;
	FB.SensorWidth = W; FB.SensorHeight = H; FB.SensorAspectRatio = W / H;
	Handler->SetCameraFilmback(FB);
	OutNeededOverscan = DynamicLensMath::ComputeOverscan(Eval.Params, Focal / W, Focal / H);
	// breathing must never resize the render in the middle of a focus pull: cover the whole focus range at this focal
	if (const UDynamicLensProfile* P = Resolved.Distortion.Profile; P && P->Type == EDynamicLensProfileType::Parametric && P->FocusCm.Num() > 0)
	{
		for (const float F : { P->FocusCm[0], P->FocusCm.Last(), 1e6f })
		{
			const FDynamicLensEval E2 = Resolved.Evaluate(Focal, F, Cam->CurrentAperture, W, H, AmountMultiplier, Cam->LensSettings.DiaphragmBladeCount, Cam->LensSettings.SqueezeFactor);
			OutNeededOverscan = FMath::Max(OutNeededOverscan, DynamicLensMath::ComputeOverscan(E2.Params, Focal / W, Focal / H));
		}
	}
	return true;
}

bool UDynamicLensComponent::DriveAnamorphic(UCineCameraComponent* Cam, float Focal, float W, float H, float& OutNeededOverscan, FLensDistortionState& OutState)
{
	const UDynamicLensProfile* Profile = Resolved.Distortion.Profile;
	TArray<float> P;
	if (!Profile || !Profile->EvaluateAnamorphic(Focal, Resolved.Distortion.Amount * AmountMultiplier, P)) return false;
	// Epic's model works on the squeezed filmback times the pixel aspect; W here is the desqueezed width, so hand it the
	// squeezed width and let PixelAspect (1.8 for a 1.8x lens) widen it back
	const float PA = FMath::Max(P[0], 0.01f);
	OutState.DistortionInfo.Parameters = P;
	OutState.FocalLengthInfo.FxFy = FVector2D(Focal / W, Focal / H);
	OutState.ImageCenter.PrincipalPoint = FVector2D(0.5, 0.5);
	Handler->SetDistortionState(OutState);
	FCameraFilmbackSettings FB;
	FB.SensorWidth = W / PA; FB.SensorHeight = H; FB.SensorAspectRatio = FB.SensorWidth / H;
	Handler->SetCameraFilmback(FB);
	OutNeededOverscan = FMath::Clamp(Handler->ComputeOverscanFactor(), 1.f, 4.f);
	return true;
}

bool UDynamicLensComponent::DriveSTMap(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float Focus, float W, float H, float WFull, float HFull, float& OutNeededOverscan, FLensDistortionState& OutState, float& OutCircleRadius)
{
	const UDynamicLensProfile* Profile = Resolved.Distortion.Profile;
	const int32 Index = Profile->FindNearestSTMap(Focal);
	if (Index < 0) return false;
	const FDynamicLensSTMapEntry& Entry = Profile->STMaps[Index];

	// sensor fit: Crop keeps the map at the lens's physical scale (camera sensor must fit inside), Scale stretches it.
	// A camera crop (Cropped Aspect Ratio) always sees the centre of the map: the map covers the whole sensor.
	FVector2D LensSensor = FVector2D(WFull, HFull);
	if (SensorFit == EDynamicLensSensorFit::Crop)
	{
		if (WFull <= Profile->NativeSensorMm.X + 1e-3f && HFull <= Profile->NativeSensorMm.Y + 1e-3f)
		{
			LensSensor = Profile->NativeSensorMm;
		}
		else
		{
			Notes += TEXT("Sensor larger than the profile's: scaled instead of cropped. ");
		}
	}
	// the map only covers its own frame: extend it (extrapolated displacement, stored in the camera frame's units because
	// Epic's blend shader crops a larger sensor without rescaling values) and present it as a map for a larger sensor
	UTexture* MapToUse = Entry.Map;
	float Scale = 1.f;
	float NeededMap = Entry.NeededOverscan;   // in the map's own units (fallback: measured at import)
	const FVector2D DispScale(LensSensor.X / W, LensSensor.Y / H);   // map sensor / camera sensor
	{
		const FString Key = FString::Printf(TEXT("%s|%.4f|%.4f"), *GetPathNameSafe(Entry.Map), DispScale.X, DispScale.Y);
		if (FDynamicLensExtendedMap* Found = ExtendedMaps.Find(Key))
		{
			if (Found->Texture) { MapToUse = Found->Texture; Scale = Found->Extend; NeededMap = Found->NeededOverscan; }
		}
		else
		{
			FDynamicLensExtendedMap New;
			New.Texture = UDynamicLensLibrary::BuildExtendedSTMap(Cast<UTexture2D>(Entry.Map), Entry.MapFormat.PixelOrigin == ECalibratedMapPixelOrigin::BottomLeft, DispScale, 2.f, 1024, New.NeededOverscan, New.Extend);
			ExtendedMaps.Add(Key, New);
			if (New.Texture) { MapToUse = New.Texture; Scale = New.Extend; NeededMap = New.NeededOverscan; }
		}
	}
	const bool bExtended = (MapToUse != Entry.Map);
	LensSensor *= Scale;
	const FVector2D FxFy(Entry.FocalMm / LensSensor.X, Entry.FocalMm / LensSensor.Y);

	if (!TransientLensFile || LensFileSTMapIndex != Index || !LensFileSensor.Equals(LensSensor, 1e-3) || !LensFileFxFy.Equals(FxFy, 1e-4))
	{
		TransientLensFile = NewObject<ULensFile>(this, NAME_None, RF_Transient);
		TransientLensFile->LensInfo.LensModel = USphericalLensModel::StaticClass();
		TransientLensFile->LensInfo.SensorDimensions = LensSensor;
		TransientLensFile->LensInfo.SqueezeFactor = 1.f;
		TransientLensFile->DataMode = ELensDataMode::STMap;
		FSTMapInfo Info;
		Info.DistortionMap = MapToUse;
		Info.MapFormat = Entry.MapFormat;
		TransientLensFile->AddSTMapPoint(0.f, 0.f, Info);
		FFocalLengthInfo FL; FL.FxFy = FxFy;
		TransientLensFile->AddFocalLengthPoint(0.f, 0.f, FL);
		LensFileSTMapIndex = Index;
		LensFileSensor = LensSensor;
		LensFileFxFy = FxFy;
	}
	if (!TransientLensFile->EvaluateDistortionData(0.f, 0.f, FVector2D(W, H), Handler))
	{
		return false;
	}
	OutState = Handler->GetCurrentDistortionState();
	// what the camera frame needs: the map's border reach, scaled into camera units (Epic's own estimate is meaningless for
	// an extended map, so only use it when the map is used as-is)
	const float DispMax = FMath::Max(DispScale.X, DispScale.Y);
	const float NeededCam = 1.f + (NeededMap - 1.f) * DispMax;
	OutNeededOverscan = FMath::Clamp(bExtended ? NeededCam : FMath::Max(Handler->GetOverscanFactor(), NeededCam), 1.f, 4.f);
	OutCircleRadius = 0.f;
	if (bExtended)
	{
		const float DataExtent = Scale * FMath::Min(DispScale.X, DispScale.Y);   // how far the extended data reaches, camera units
		if (OutNeededOverscan > DataExtent + 1e-3f)
		{
			OutCircleRadius = DataExtent / OutNeededOverscan;   // beyond the extrapolated data: black, not smeared
		}
	}
	if (FMath::Abs(Entry.FocalMm - Focal) > 0.5f)
	{
		Notes += FString::Printf(TEXT("Nearest ST map is %.0f mm (camera at %.1f mm). "), Entry.FocalMm, Focal);
	}
	return true;
}

bool UDynamicLensComponent::DriveProjection(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float W, float H, float AppliedOverscan, float& OutNeededOverscan, FLensDistortionState& OutState, float& OutCircleRx, float& OutCircleRy)
{
	const UDynamicLensProfile* Profile = Resolved.Distortion.Profile;
	const float ThetaMax = FMath::DegreesToRadians(FMath::Clamp(Profile->MaxFieldAngleDeg, 10.f, 110.f));
	const float O = FMath::Clamp(AppliedOverscan, 1.f, MaxOverscanCeiling);
	const EDynamicLensProjection Proj = Profile->Projection;

	// The continuous family and the fit are opt-in per profile, so every preset authored before them renders exactly as
	// it did: the old path below is taken bit for bit when neither is set.
	const bool bKPath = Profile->bUseProjectionK || Profile->bFitFieldToCircle;
	float K = Profile->GetProjectionK();
	if (Profile->bUseProjectionK)
	{
		const float Amount = Resolved.Distortion.Amount * AmountMultiplier;   // 0 = rectilinear, 1 = the profile, >1 = more fisheye
		K = FMath::Clamp(1.f + (K - 1.f) * Amount, -1.5f, 1.f);
	}
	const bool bFit = Profile->bFitFieldToCircle;
	const float CircleMm = bFit ? 0.5f * Profile->EffectiveImageCircleMm() * FMath::Max(Resolved.ImageCircle.Scale, 0.1f) : 0.f;
	auto G = [&](float Theta) { return bKPath ? DynamicLensMath::ProjectionGK(K, Theta) : DynamicLensMath::ProjectionG(Proj, Theta); };
	// the lens shows nothing past its own field limit: a stated circle bigger than f*g(ThetaMax) would leave a ring of
	// unwarped picture inside the rim, so the fitted circle is capped there (the Optex 4 mm's 14.5 mm vs 12.6 mm)
	const float FitCircleMm = bFit ? FMath::Min(CircleMm, Focal * G(ThetaMax)) : 0.f;
	auto ThetaOf = [&](float ROverF, float& Out) { return bKPath ? DynamicLensMath::ProjectionThetaK(K, ROverF, Out) : DynamicLensMath::ProjectionTheta(Proj, ROverF, Out); };

	const bool bDirty = !ProjectionMap || !FMath::IsNearlyEqual(ProjectionKeyFocal, Focal, 1e-3f) || !ProjectionKeySensor.Equals(FVector2D(W, H), 1e-3)
		|| !FMath::IsNearlyEqual(ProjectionKeyOverscan, O, 1e-3f) || ProjectionKeyType != (int32)Proj || !FMath::IsNearlyEqual(ProjectionKeyMaxAngle, ThetaMax, 1e-4f) || !FMath::IsNearlyEqual(ProjectionKeyScale, Resolved.ImageCircle.Scale, 1e-4f)
		|| !FMath::IsNearlyEqual(ProjectionKeyK, bKPath ? K : 99.f, 1e-4f) || ProjectionKeyFit != bFit;
	if (bDirty)
	{
		if (!ProjectionMap)
		{
			ProjectionMap = UTexture2D::CreateTransient(ProjectionMapSize, ProjectionMapSize, PF_G32R32F);
			ProjectionMap->SRGB = false;
			ProjectionMap->Filter = TF_Bilinear;
			ProjectionMap->AddressX = TA_Clamp;
			ProjectionMap->AddressY = TA_Clamp;
			ProjectionMap->NeverStream = true;
		}
		// the rectilinear source covers |x| <= O*W/2, |y| <= O*H/2 (mm on the sensor plane, focal length f)
		const float LimX = O * 0.5f * W, LimY = O * 0.5f * H;
		const float ThetaCapX = FMath::Atan(LimX / Focal);
		const float ThetaCapY = FMath::Atan(LimY / Focal);

		// Fit: theta(r) = S * theta_lens(r), with the largest S <= 1 for which every in-frame point inside the circle has
		// source. The binding points are where each ray from the centre leaves the circle-and-frame region.
		float FieldScale = 1.f;
		if (bFit && FitCircleMm > KINDA_SMALL_NUMBER)
		{
			auto Feasible = [&](float S)
			{
				for (int32 A = 0; A <= 90; ++A)
				{
					const float Phi = FMath::DegreesToRadians((float)A);
					const float Cs = FMath::Cos(Phi), Sn = FMath::Sin(Phi);
					float REdge = FitCircleMm;
					if (Cs > 1e-4f) REdge = FMath::Min(REdge, 0.5f * W / Cs);
					if (Sn > 1e-4f) REdge = FMath::Min(REdge, 0.5f * H / Sn);
					float Theta;
					if (!ThetaOf(REdge / Focal, Theta)) Theta = ThetaMax;
					Theta = FMath::Min(Theta, ThetaMax) * S;
					if (Theta >= HALF_PI - 0.01f) return false;
					const float Ru = Focal * FMath::Tan(Theta);
					if (Ru * Cs > LimX || Ru * Sn > LimY) return false;
				}
				return true;
			};
			if (!Feasible(1.f))
			{
				float Lo = 0.02f, Hi = 1.f;
				for (int32 It = 0; It < 30; ++It) { const float Mid = 0.5f * (Lo + Hi); (Feasible(Mid) ? Lo : Hi) = Mid; }
				FieldScale = Lo;
			}
		}
		ProjectionFieldScale = FieldScale;

		FTexture2DMipMap& Mip = ProjectionMap->GetPlatformData()->Mips[0];
		float* Data = static_cast<float*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 J = 0; J < ProjectionMapSize; ++J)
		{
			const float V = (J + 0.5f) / ProjectionMapSize;
			for (int32 I = 0; I < ProjectionMapSize; ++I)
			{
				const float U = (I + 0.5f) / ProjectionMapSize;
				const float X = (U - 0.5f) * W, Y = (V - 0.5f) * H;   // mm on the fisheye image plane
				const float R = FMath::Sqrt(X * X + Y * Y);
				float SU = U, SV = V;                                     // identity for anything the lens can't show
				float Theta;
				if (R > KINDA_SMALL_NUMBER && ThetaOf(R / Focal, Theta) && Theta <= ThetaMax)
				{
					Theta *= FieldScale;
					if (Theta < HALF_PI - 0.01f)
					{
						const float Ru = Focal * FMath::Tan(Theta);       // radius in the rectilinear render
						const float SX = X / R * Ru, SY = Y / R * Ru;
						if (FMath::Abs(SX) <= LimX && FMath::Abs(SY) <= LimY)
						{
							SU = 0.5f + SX / W;
							SV = 0.5f + SY / H;
						}
					}
				}
				float* Px = Data + 2 * (J * ProjectionMapSize + I);
				Px[0] = SU; Px[1] = SV;
			}
		}
		Mip.BulkData.Unlock();
		ProjectionMap->UpdateResource();

		if (bFit && FitCircleMm > KINDA_SMALL_NUMBER)
		{
			// fitted: the circle is the lens's own, round, at its physical size
			ProjectionCircleRadius = ProjectionCircleRy = FitCircleMm / (0.5f * W);
		}
		else
		{
			// visible circle: where the source runs out (x or y edge) or the lens's own field limit, whichever is first
			const float RadX = Focal * G(FMath::Min(ThetaCapX, ThetaMax));
			const float RadY = Focal * G(FMath::Min(ThetaCapY, ThetaMax));
			const float RadMax = Focal * G(ThetaMax) * FMath::Max(Resolved.ImageCircle.Scale, 0.1f);
			ProjectionCircleRadius = FMath::Min(RadX, RadMax) / (0.5f * W);
			ProjectionCircleRy = FMath::Min(RadY, RadMax) / (0.5f * W);
		}
		ProjectionNeededOverscan = O;

		ProjectionKeyFocal = Focal; ProjectionKeySensor = FVector2D(W, H); ProjectionKeyOverscan = O;
		ProjectionKeyType = (int32)Proj; ProjectionKeyMaxAngle = ThetaMax; ProjectionKeyScale = Resolved.ImageCircle.Scale;
		ProjectionKeyK = bKPath ? K : 99.f; ProjectionKeyFit = bFit;
		TransientLensFile = nullptr;
	}
	if (bFit && ProjectionFieldScale < 0.999f)
	{
		Notes += FString::Printf(TEXT("Field fitted to the image circle: %.0f%% of the lens's angle (Unreal renders ~%.0f deg off-axis at this overscan). "),
			ProjectionFieldScale * 100.f, FMath::RadiansToDegrees(FMath::Atan(O * 0.5f * W / Focal)));
	}

	const FVector2D FxFy(Focal / W, Focal / H);
	if (!TransientLensFile || LensFileSTMapIndex != -2 || !LensFileSensor.Equals(FVector2D(W, H), 1e-3) || !LensFileFxFy.Equals(FxFy, 1e-4))
	{
		TransientLensFile = NewObject<ULensFile>(this, NAME_None, RF_Transient);
		TransientLensFile->LensInfo.LensModel = USphericalLensModel::StaticClass();
		TransientLensFile->LensInfo.SensorDimensions = FVector2D(W, H);
		TransientLensFile->DataMode = ELensDataMode::STMap;
		FSTMapInfo Info;
		Info.DistortionMap = ProjectionMap;
		Info.MapFormat.PixelOrigin = ECalibratedMapPixelOrigin::TopLeft;
		Info.MapFormat.UndistortionChannels = ECalibratedMapChannels::RG;
		Info.MapFormat.DistortionChannels = ECalibratedMapChannels::None;
		TransientLensFile->AddSTMapPoint(0.f, 0.f, Info);
		FFocalLengthInfo FL; FL.FxFy = FxFy;
		TransientLensFile->AddFocalLengthPoint(0.f, 0.f, FL);
		LensFileSTMapIndex = -2;
		LensFileSensor = FVector2D(W, H);
		LensFileFxFy = FxFy;
	}
	if (!TransientLensFile->EvaluateDistortionData(0.f, 0.f, FVector2D(W, H), Handler))
	{
		return false;
	}
	OutState = Handler->GetCurrentDistortionState();
	OutNeededOverscan = ProjectionNeededOverscan;
	OutCircleRx = ProjectionCircleRadius;
	OutCircleRy = ProjectionCircleRy;
	return true;
}

void UDynamicLensComponent::ApplyRendering(UCineCameraComponent* Cam, const FLensDistortionState& State, float AppliedOverscan)
{
	const float CamOverscan = FMath::Clamp(AppliedOverscan - 1.f, 0.f, (RenderMode == EDynamicLensRenderMode::TemporalSuperResolution) ? 1.f : MaxOverscanCeiling - 1.f);
	Cam->Overscan = CamOverscan;
	Cam->bScaleResolutionWithOverscan = Resolved.Overscan.bScaleResolutionWithOverscan;
	bOverscanTouched = true;
	Handler->SetOverscanFactor(CamOverscan + 1.f);   // material and camera must agree (same as Epic's LensComponent)
	if (Resolved.Distortion.Profile == nullptr || Resolved.Distortion.Profile->Type == EDynamicLensProfileType::Parametric)
	{
		Handler->ProcessCurrentDistortion();
	}

	UCameraCalibrationSubsystem* Sub = GEngine ? GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>() : nullptr;
	ACameraActor* CamActor = Cast<ACameraActor>(GetOwner());
	// never hand the SVE a displacement map without a GPU resource: switching Render Mode mid-PIE on an ST map asserted
	// InTexture.IsValid() in ScreenPass.inl (2026-09-24). Stay on the material path for the frame instead.
	auto HasResource = [](const UTextureRenderTarget2D* RT) { return RT && RT->GetResource() && RT->GetResource()->TextureRHI.IsValid(); };
	const bool bSVEReady = HasResource(Handler->GetUndistortionDisplacementMap()) && HasResource(Handler->GetDistortionDisplacementMap());
	const bool bWantSVE = (RenderMode == EDynamicLensRenderMode::TemporalSuperResolution) && Sub && CamActor && bSVEReady;

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
}

void UDynamicLensComponent::UpdateNearClip(UCineCameraComponent* Cam, bool bWant)
{
	if (bWant)
	{
		if (!bNearClipTouched)
		{
			Backup.bNearClip = Cam->bOverride_CustomNearClippingPlane;
			Backup.NearClip = Cam->CustomNearClippingPlane;
			bNearClipTouched = true;
		}
		Cam->bOverride_CustomNearClippingPlane = true;
		Cam->CustomNearClippingPlane = FisheyeNearClipCm;
	}
	else if (bNearClipTouched)
	{
		Cam->bOverride_CustomNearClippingPlane = Backup.bNearClip;
		Cam->CustomNearClippingPlane = Backup.NearClip;
		bNearClipTouched = false;
	}
}

void UDynamicLensComponent::StripForeignDistortionBlendables(UCineCameraComponent* Cam)
{
	// PIE copies of a camera inherit the editor world's transient distortion materials; drop them
	TArray<FWeightedBlendable>& Arr = Cam->PostProcessSettings.WeightedBlendables.Array;
	for (int32 I = Arr.Num() - 1; I >= 0; --I)
	{
		UObject* Obj = Arr[I].Object;
		if (!Obj) { Arr.RemoveAt(I); continue; }
		if (Obj == AppliedMID || Obj == CircleMID) continue;
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Obj))
		{
			const UMaterial* Base = MID->GetBaseMaterial();
			const FString BaseName = Base ? Base->GetName() : FString();
			if (BaseName.Contains(TEXT("DistortionPostProcess")) || BaseName.Contains(TEXT("M_DL_ImageCircle")))
			{
				Arr.RemoveAt(I);
			}
		}
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
	Backup.bSqueeze = P.bOverride_DepthOfFieldSqueezeFactor; Backup.Squeeze = P.DepthOfFieldSqueezeFactor;
	Backup.LensBlades = Cam->LensSettings.DiaphragmBladeCount; Backup.LensSqueeze = Cam->LensSettings.SqueezeFactor; Backup.LensSensorWidth = Cam->Filmback.SensorWidth; Backup.bLensDriven = false;
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
		P.bOverride_DepthOfFieldSqueezeFactor = Backup.bSqueeze; P.DepthOfFieldSqueezeFactor = Backup.Squeeze;
		if (Backup.bLensDriven)
		{
			Cam->LensSettings.DiaphragmBladeCount = Backup.LensBlades;
			if (!FMath::IsNearlyEqual(Cam->LensSettings.SqueezeFactor, Backup.LensSqueeze))
			{
				Cam->LensSettings.SqueezeFactor = Backup.LensSqueeze;
				Cam->Filmback.SensorWidth = Backup.LensSensorWidth;
				Cam->Filmback.SensorAspectRatio = Cam->Filmback.SensorWidth / FMath::Max(Cam->Filmback.SensorHeight, 0.01f);
			}
			Backup.bLensDriven = false;
		}
	}
	UpdateNearClip(Cam, false);
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
	LastCircleRadius = -1.f;
}

void UDynamicLensComponent::ApplyLook(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float CircleRadiusNorm, float Aspect, float CircleEllipticity, float CircleSquareness)
{
	const bool bDoBokeh = bApplyBokeh && Eval.bBokeh;
	const bool bDoVignette = bApplyVignette && Eval.bVignette;

	SetBokehQualityRequest(bDoBokeh && bForceBokehQuality);
	if (bDoBokeh && !bForceBokehQuality && BokehSimulationOff() && (Eval.Petzval != 0.f || Eval.BarrelLengthMm > 0.f))
	{
		Notes += TEXT("Swirl and cat's eye are off at this scalability (r.DOF bokeh settings are lowered): use Cinematic post-process quality, or turn on Force Bokeh Quality. ");
	}

	// --- image circle mask
	if (CircleRadiusNorm > 0.f)
	{
		if (CircleMID && !CircleMID->Parent)
		{
			// base material deleted/rebuilt under us: a parentless instance renders nothing and can drop the whole chain
			if (bCircleApplied) Cam->RemoveBlendable(CircleMID);
			CircleMID = nullptr; bCircleApplied = false;
		}
		if (!CircleMID)
		{
			if (UMaterialInterface* Mat = ImageCircleMaterial.LoadSynchronous())
			{
				CircleMID = UMaterialInstanceDynamic::Create(Mat, this);
			}
		}
		if (CircleMID)
		{
			if (!bCircleApplied)
			{
				Cam->AddOrUpdateBlendable(CircleMID, 1.f);
				bCircleApplied = true;
			}
			const bool bEdgeChanged = !bHasLastEval || !LastEval.Edge.Equals(Eval.Edge) || !FMath::IsNearlyEqual(LastEval.ImageCircleSoftness, Eval.ImageCircleSoftness);
			if (!FMath::IsNearlyEqual(LastCircleRadius, CircleRadiusNorm, 1e-4f) || !FMath::IsNearlyEqual(LastCircleEllipticity, CircleEllipticity, 1e-4f) || !FMath::IsNearlyEqual(LastCircleSquareness, CircleSquareness, 1e-3f) || bEdgeChanged)
			{
				const FDynamicLensImageCircleEdge& Ed = Eval.Edge;
				CircleMID->SetScalarParameterValue(CircleParamRadius, CircleRadiusNorm);
				CircleMID->SetScalarParameterValue(CircleParamSoftness, Eval.ImageCircleSoftness);
				CircleMID->SetScalarParameterValue(CircleParamAspect, Aspect);
				CircleMID->SetScalarParameterValue(CircleParamFalloff, Ed.FalloffPower);
				CircleMID->SetScalarParameterValue(CircleParamOpacity, Ed.Opacity);
				CircleMID->SetScalarParameterValue(CircleParamCenterX, Ed.CenterOffset.X);
				CircleMID->SetScalarParameterValue(CircleParamCenterY, Ed.CenterOffset.Y);
				CircleMID->SetScalarParameterValue(CircleParamEllipticity, Ed.Ellipticity * CircleEllipticity);
				CircleMID->SetScalarParameterValue(CircleParamSquareness, CircleSquareness);
				CircleMID->SetScalarParameterValue(CircleParamWobble, Ed.Wobble);
				CircleMID->SetScalarParameterValue(CircleParamWobbleLobes, (float)Ed.WobbleLobes);
				CircleMID->SetScalarParameterValue(CircleParamWobbleSeed, FMath::DegreesToRadians(Ed.WobbleSeed));
				CircleMID->SetScalarParameterValue(CircleParamEdgeNoise, Ed.EdgeNoise);
				CircleMID->SetScalarParameterValue(CircleParamNoiseScale, Ed.NoiseScale);
				CircleMID->SetScalarParameterValue(CircleParamCAR, Ed.ChromaticAmount * Ed.ChromaticRed);
				CircleMID->SetScalarParameterValue(CircleParamCAG, Ed.ChromaticAmount * Ed.ChromaticGreen);
				CircleMID->SetScalarParameterValue(CircleParamCAB, Ed.ChromaticAmount * Ed.ChromaticBlue);
				CircleMID->SetScalarParameterValue(CircleParamSoftWobble, Ed.FalloffWobble);
				CircleMID->SetScalarParameterValue(CircleParamSoftWobbleLobes, (float)Ed.FalloffWobbleLobes);
				CircleMID->SetScalarParameterValue(CircleParamSoftWobbleSeed, FMath::DegreesToRadians(Ed.FalloffWobbleSeed));
				CircleMID->SetScalarParameterValue(CircleParamNoiseDepth, Ed.NoiseDepth);
				CircleMID->SetScalarParameterValue(CircleParamNoiseBlur, Ed.NoiseBlur);
				CircleMID->SetScalarParameterValue(CircleParamNoiseDetail, Ed.NoiseDetail);
				CircleMID->SetScalarParameterValue(CircleParamNoiseStretch, Ed.NoiseStretch);
				CircleMID->SetScalarParameterValue(CircleParamNoiseSeed, Ed.NoiseSeed);
				CircleMID->SetScalarParameterValue(CircleParamNoiseContrast, Ed.NoiseContrast);
				CircleMID->SetScalarParameterValue(CircleParamScatter, Ed.Scatter);
				CircleMID->SetScalarParameterValue(CircleParamFadeReach, Ed.FadeReach);
				CircleMID->SetScalarParameterValue(CircleParamFadeAmount, Ed.FadeAmount);
				CircleMID->SetScalarParameterValue(CircleParamFadeCurve, Ed.FadeCurve);
				UTexture2D* MaskTex = Ed.MaskTexture.IsNull() ? nullptr : Ed.MaskTexture.LoadSynchronous();
				CircleMID->SetScalarParameterValue(CircleParamMaskStrength, MaskTex ? Ed.MaskStrength : 0.f);
				if (MaskTex)
				{
					CircleMID->SetTextureParameterValue(CircleParamMask, MaskTex);
				}
				LastCircleRadius = CircleRadiusNorm;
				LastCircleEllipticity = CircleEllipticity;
				LastCircleSquareness = CircleSquareness;
			}
		}
	}
	else if (bCircleApplied && CircleMID)
	{
		Cam->RemoveBlendable(CircleMID);
		bCircleApplied = false;
		LastCircleRadius = -1.f;
	}

	auto SameAsLast = [&]()
	{
		if (!bHasLastEval) return false;
		if (LastEval.bBokeh != bDoBokeh || LastEval.bVignette != bDoVignette) return false;
		if (!LastEval.Edge.Equals(Eval.Edge) || !FMath::IsNearlyEqual(LastEval.ImageCircleSoftness, Eval.ImageCircleSoftness)) return false;
		if (bDoBokeh && (LastEval.Blades != Eval.Blades ||
			!FMath::IsNearlyEqual(LastEval.BladeCurvature, Eval.BladeCurvature, 1e-3f) ||
			!FMath::IsNearlyEqual(LastEval.BokehSqueeze, Eval.BokehSqueeze, 1e-3f) ||
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
		// Unreal's DOF only knows a blade count: rounded blades read as "more blades"
		const int32 EffBlades = FMath::Clamp(FMath::RoundToInt(FMath::Lerp((float)Eval.Blades, 16.f, Eval.BladeCurvature)), 4, 16);
		P.bOverride_DepthOfFieldBladeCount = true; P.DepthOfFieldBladeCount = EffBlades;
		// UCineCameraComponent::GetCameraView overwrites DepthOfFieldBladeCount and DepthOfFieldSqueezeFactor from its
		// Lens Settings every frame, so the only way to shape the bokeh is through those settings themselves
		if (!Backup.bLensDriven)
		{
			Backup.LensBlades = Cam->LensSettings.DiaphragmBladeCount; Backup.LensSqueeze = Cam->LensSettings.SqueezeFactor; Backup.LensSensorWidth = Cam->Filmback.SensorWidth;
			Backup.bLensDriven = true;
		}
		if (Resolved.Bokeh.BladeSource != EDynamicLensValueSource::Camera)
		{
			Cam->LensSettings.DiaphragmBladeCount = EffBlades;
		}
		if (Resolved.Bokeh.SqueezeSource == EDynamicLensValueSource::Custom)
		{
			// custom bokeh squeeze on a camera whose own squeeze differs: change the camera squeeze and compensate the
			// filmback width so the desqueezed frame (and framing) stays the same
			const float S = FMath::Clamp(Eval.BokehSqueeze, 1.f, 2.f);
			if (!FMath::IsNearlyEqual(Cam->LensSettings.SqueezeFactor, S, 1e-3f))
			{
				const float Desqueezed = Cam->Filmback.SensorWidth * Cam->LensSettings.SqueezeFactor;
				Cam->LensSettings.SqueezeFactor = S;
				Cam->Filmback.SensorWidth = Desqueezed / S;
				Cam->Filmback.SensorAspectRatio = Cam->Filmback.SensorWidth / FMath::Max(Cam->Filmback.SensorHeight, 0.01f);
			}
		}
		P.bOverride_DepthOfFieldSqueezeFactor = true; P.DepthOfFieldSqueezeFactor = FMath::Clamp(Eval.BokehSqueeze, 1.f, 2.f);
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
		P.bOverride_DepthOfFieldSqueezeFactor = Backup.bSqueeze; P.DepthOfFieldSqueezeFactor = Backup.Squeeze;
		if (Backup.bLensDriven)
		{
			Cam->LensSettings.DiaphragmBladeCount = Backup.LensBlades;
			if (!FMath::IsNearlyEqual(Cam->LensSettings.SqueezeFactor, Backup.LensSqueeze))
			{
				Cam->LensSettings.SqueezeFactor = Backup.LensSqueeze;
				Cam->Filmback.SensorWidth = Backup.LensSensorWidth;
				Cam->Filmback.SensorAspectRatio = Cam->Filmback.SensorWidth / FMath::Max(Cam->Filmback.SensorHeight, 0.01f);
			}
			Backup.bLensDriven = false;
		}
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

void UDynamicLensComponent::SetBokehQualityRequest(bool bWant)
{
	if (bWant == bRequestingBokehQuality)
	{
		return;
	}
	bRequestingBokehQuality = bWant;
	if (bWant ? BokehQualityClaims++ != 0 : --BokehQualityClaims != 0)
	{
		return;
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(BokehCVars); ++i)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(BokehCVars[i].Name);
		if (!CVar)
		{
			continue;
		}
		if (bWant)
		{
			SavedBokehCVars[i] = CVar->GetInt();
			if (SavedBokehCVars[i] < BokehCVars[i].Wanted)
			{
				CVar->Set(BokehCVars[i].Wanted, ECVF_SetByCode);
			}
		}
		else if (SavedBokehCVars[i] < BokehCVars[i].Wanted)
		{
			CVar->Set(SavedBokehCVars[i], ECVF_SetByCode);
		}
	}
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
		if (bCircleApplied && CircleMID)
		{
			Cam->RemoveBlendable(CircleMID);
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
	RestoreAccumulationDOF();
	SetBokehQualityRequest(false);
	AppliedMID = nullptr;
	CircleMID = nullptr;          // recreated on the next apply (the base material may have been rebuilt)
	bCircleApplied = false;
	bSVEActive = false;
	bStrippedForeign = false;
	AppliedCamera = nullptr;
	LastCircleRadius = -1.f;
	bHasLastEval = false;
	LastDynamicOverscan = 0.f;
	LastOverscanFactor = 1.f;
	LastVignette = 0.f;
	ImageCircleRadius = 0.f;
}

// ------------------------------------------------------------------------------------------------ preset stepping

void UDynamicLensComponent::StepPreset(int32 Direction)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	FARFilter Filter;
	Filter.ClassPaths.Add(UDynamicLensPreset::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	ARM.Get().GetAssets(Filter, Assets);
	if (Assets.Num() == 0) return;
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
	int32 Cur = -1;
	if (Preset)
	{
		const FName CurPkg = Preset->GetOutermost()->GetFName();
		for (int32 I = 0; I < Assets.Num(); ++I) { if (Assets[I].PackageName == CurPkg) { Cur = I; break; } }
	}
	const int32 Next = (Cur < 0) ? (Direction > 0 ? 0 : Assets.Num() - 1) : (Cur + Direction + Assets.Num()) % Assets.Num();
	ApplyPreset(Cast<UDynamicLensPreset>(Assets[Next].GetAsset()));
}

bool UDynamicLensComponent::ApplyPreset(UDynamicLensPreset* NewPreset)
{
	if (!NewPreset || NewPreset == Preset) return false;
#if WITH_EDITOR
	Modify();
#endif
	Preset = NewPreset;
	ClearEffect();
	TransientLensFile = nullptr;
	LensFileSTMapIndex = -1;
	if (MatchCamera.bOnPresetChange) MatchCameraToProfile();
	else if (MatchCamera.bRefreshOverrides) CopyAllFromPreset();
	UpdateProfileInfo();
	if (UCineCameraComponent* Cam = GetTargetCamera(); Cam && bEnabled) Apply(Cam);
	return true;
}

void UDynamicLensComponent::A1_PreviousPreset() { StepPreset(-1); }
void UDynamicLensComponent::A2_NextPreset() { StepPreset(+1); }
void UDynamicLensComponent::A3_PreviousFocal() { StepFocal(-1); }
void UDynamicLensComponent::A4_NextFocal() { StepFocal(+1); }

TArray<float> UDynamicLensComponent::MeasuredFocals() const
{
	TArray<float> Out;
	const UDynamicLensProfile* P = ResolveSettings().Distortion.Profile;
	if (!P) return Out;
	if (P->Type == EDynamicLensProfileType::STMap) { for (const FDynamicLensSTMapEntry& E : P->STMaps) Out.AddUnique(E.FocalMm); }
	else if (P->Type == EDynamicLensProfileType::Parametric) { for (const FDynamicLensProfileRow& R : P->Rows) Out.AddUnique(R.FocalMm); }
	else if (P->NominalFocalMm > 0.f) Out.Add(P->NominalFocalMm);
	Out.Sort();
	return Out;
}

void UDynamicLensComponent::StepFocal(int32 Direction)
{
	UCineCameraComponent* Cam = GetTargetCamera();
	const TArray<float> Focals = MeasuredFocals();
	if (!Cam || Focals.Num() == 0) return;
	// nearest measured focal to where the camera is, then step
	int32 Cur = 0;
	for (int32 I = 1; I < Focals.Num(); ++I) { if (FMath::Abs(Focals[I] - Cam->CurrentFocalLength) < FMath::Abs(Focals[Cur] - Cam->CurrentFocalLength)) Cur = I; }
	if (FMath::Abs(Focals[Cur] - Cam->CurrentFocalLength) > 0.01f)
	{
		// camera sits between two: step to the neighbour in that direction
		if (Direction > 0 && Focals[Cur] < Cam->CurrentFocalLength) Cur = FMath::Min(Cur + 1, Focals.Num() - 1);
		else if (Direction < 0 && Focals[Cur] > Cam->CurrentFocalLength) Cur = FMath::Max(Cur - 1, 0);
	}
	else
	{
		Cur = FMath::Clamp(Cur + Direction, 0, Focals.Num() - 1);
	}
#if WITH_EDITOR
	Cam->Modify();
#endif
	Cam->SetCurrentFocalLength(Focals[Cur]);
	UpdateProfileInfo();
	if (bEnabled && HasLens()) Apply(Cam);
}

void UDynamicLensComponent::PullCameraQuick(UCineCameraComponent* Cam)
{
	if (bPushingCamera) return;
	Camera.FocalLengthMm = Cam->CurrentFocalLength;
	Camera.Aperture = Cam->CurrentAperture;
	Camera.FocusMethod = Cam->FocusSettings.FocusMethod;
	Camera.ManualFocusDistance = Cam->FocusSettings.ManualFocusDistance;
	Camera.ActorToTrack = Cam->FocusSettings.TrackingFocusSettings.ActorToTrack;
	Camera.FocusOffset = Cam->FocusSettings.FocusOffset;
	Camera.CroppedAspectRatio = Cam->CropSettings.AspectRatio;
	Camera.FilmbackMm = FVector2D(Cam->Filmback.SensorWidth, Cam->Filmback.SensorHeight);
	Camera.SqueezeFactor = Cam->LensSettings.SqueezeFactor;
}

void UDynamicLensComponent::PushCameraQuick(UCineCameraComponent* Cam)
{
	bPushingCamera = true;
#if WITH_EDITOR
	Cam->Modify();
#endif
	if (!FMath::IsNearlyEqual(Cam->CurrentFocalLength, Camera.FocalLengthMm)) Cam->SetCurrentFocalLength(Camera.FocalLengthMm);
	Cam->CurrentAperture = Camera.Aperture;
	Cam->FocusSettings.FocusMethod = Camera.FocusMethod;
	Cam->FocusSettings.ManualFocusDistance = Camera.ManualFocusDistance;
	Cam->FocusSettings.TrackingFocusSettings.ActorToTrack = Camera.ActorToTrack;
	Cam->FocusSettings.FocusOffset = Camera.FocusOffset;
	Cam->CropSettings.AspectRatio = Camera.CroppedAspectRatio;
	Cam->Filmback.SensorWidth = FMath::Max((float)Camera.FilmbackMm.X, 1.f);
	Cam->Filmback.SensorHeight = FMath::Max((float)Camera.FilmbackMm.Y, 1.f);
	Cam->Filmback.SensorAspectRatio = Cam->Filmback.SensorWidth / Cam->Filmback.SensorHeight;
	Cam->LensSettings.SqueezeFactor = FMath::Clamp(Camera.SqueezeFactor, 1.f, 2.f);
	bPushingCamera = false;
}

void UDynamicLensComponent::ClearDistortionRendering(UCineCameraComponent* Cam)
{
	if (AppliedMID)
	{
		Cam->RemoveBlendable(AppliedMID);
		AppliedMID = nullptr;
	}
	if (bSVEActive)
	{
		if (UCameraCalibrationSubsystem* Sub = GEngine ? GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>() : nullptr)
		{
			if (ACameraActor* CamActor = Cast<ACameraActor>(GetOwner())) Sub->ClearLensDistortionSVEState(CamActor);
		}
		bSVEActive = false;
	}
	if (bOverscanTouched)
	{
		Cam->Overscan = Backup.Overscan;
		Cam->bCropOverscan = Backup.bCropOverscan;
		Cam->bScaleResolutionWithOverscan = Backup.bScaleRes;
		bOverscanTouched = false;
	}
}

// ------------------------------------------------------------------------------------------------ settings / presets

FDynamicLensSettings UDynamicLensComponent::ResolveSettings() const
{
	FDynamicLensSettings S = Preset ? Preset->GetSettings() : FDynamicLensSettings();
	if (bOverrideDistortion)  S.Distortion = Distortion;
	if (bOverrideImageCircle) S.ImageCircle = ImageCircle;
	if (bOverrideVignette)    S.Vignette = Vignette;
	if (bOverrideBokeh)       S.Bokeh = Bokeh;
	if (bOverrideOverscan)    S.Overscan = Overscan;
	return S;
}

void UDynamicLensComponent::UpdateProfileInfo()
{
	const FDynamicLensSettings S = ResolveSettings();
	const UDynamicLensProfile* P = S.Distortion.Profile;
	if (!P)
	{
		ProfileInfo = TEXT("No profile.");
		return;
	}
	const float Squeeze = FMath::Max(P->Squeeze, 1.f);
	const UCineCameraComponent* InfoCam = GetTargetCamera();
	const float CamFocal = InfoCam ? InfoCam->CurrentFocalLength : (LastFocalMm > 0.f ? LastFocalMm : 35.f);
	const float Locked = P->GetLockedFocal(CamFocal);
	FString Info = FString::Printf(TEXT("%s\n%s\nMatch Camera To Profile sets: filmback %.2f x %.2f mm (%.2f:1), squeeze %.2gx, crop off%s."),
		*P->Label, *P->Coverage,
		P->NativeSensorMm.X / Squeeze, P->NativeSensorMm.Y, (P->NativeSensorMm.X / FMath::Max(P->NativeSensorMm.Y, 0.01f)), Squeeze,
		Locked > 0.f ? *FString::Printf(TEXT(", focal length %.4g mm"), Locked) : TEXT(""));
	if (S.Distortion.bLockFocalLength && Locked > 0.f) Info += TEXT(" Focal length is locked by the preset.");
	const TArray<float> Focals = MeasuredFocals();
	if (Focals.Num() > 1)
	{
		const float Cur = CamFocal;
		FString List;
		for (float F : Focals)
		{
			if (!List.IsEmpty()) List += TEXT(", ");
			List += FMath::IsNearlyEqual(F, Cur, 0.01f) ? FString::Printf(TEXT("[%.4g]"), F) : FString::Printf(TEXT("%.4g"), F);
		}
		Info += FString::Printf(TEXT("\nMeasured focal lengths (mm): %s"), *List);
	}
	ProfileInfo = Info;
}

void UDynamicLensComponent::CopyAllFromPreset()
{
	if (!Preset) return;
#if WITH_EDITOR
	Modify();
#endif
	Distortion = Preset->Distortion; ImageCircle = Preset->ImageCircle; Vignette = Preset->Vignette; Bokeh = Preset->Bokeh; Overscan = Preset->Overscan;
}

void UDynamicLensComponent::SaveAsNewPreset()
{
#if WITH_EDITOR
	FString Name = NewPresetName.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		Name = Preset ? Preset->GetName() + TEXT("_Copy") : TEXT("DL_Custom");
	}
	for (TCHAR& C : Name) { if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-')) C = TEXT('_'); }
	FString Folder = NewPresetFolder.Path.IsEmpty() ? TEXT("/Game/DynamicLens/Presets") : NewPresetFolder.Path;
	Folder.RemoveFromEnd(TEXT("/"));
	if (!Folder.StartsWith(TEXT("/"))) Folder = TEXT("/Game/") + Folder;

	FString PackageName = Folder / Name;
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		Notes = FString::Printf(TEXT("Save As New Preset: '%s' is not a valid asset path."), *PackageName);
		return;
	}
	// don't overwrite: number the name if it exists
	{
		const FString Base = PackageName; int32 N = 1;
		while (FPackageName::DoesPackageExist(PackageName)) { PackageName = FString::Printf(TEXT("%s_%d"), *Base, ++N); }
		Name = FPackageName::GetShortName(PackageName);
	}

	UPackage* Pkg = CreatePackage(*PackageName);
	Pkg->FullyLoad();
	UDynamicLensPreset* NewPreset = NewObject<UDynamicLensPreset>(Pkg, FName(*Name), RF_Public | RF_Standalone);
	NewPreset->SetSettings(ResolveSettings());
	NewPreset->Description = Preset ? FString::Printf(TEXT("From %s (%s), edited on %s."), *Preset->GetName(), *Preset->Description, *GetOwner()->GetActorNameOrLabel())
	                                : FString::Printf(TEXT("Saved from %s."), *GetOwner()->GetActorNameOrLabel());
	FAssetRegistryModule::AssetCreated(NewPreset);
	Pkg->MarkPackageDirty();

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Pkg, NewPreset, *FileName, Args))
	{
		Notes = FString::Printf(TEXT("Save As New Preset: could not save %s."), *PackageName);
		return;
	}
	Modify();
	Preset = NewPreset;
	bOverrideDistortion = bOverrideImageCircle = bOverrideVignette = bOverrideBokeh = bOverrideOverscan = false;
	NewPresetName.Reset();
	ClearEffect();
	TransientLensFile = nullptr;
	LensFileSTMapIndex = -1;
	Notes = FString::Printf(TEXT("Saved %s and switched this camera to it."), *PackageName);
#endif
}

// ------------------------------------------------------------------------------------------------ accumulation DOF

UActorComponent* UDynamicLensComponent::FindAccumulationDOF() const
{
	const AActor* Owner = GetOwner();
	if (!Owner) return nullptr;
	for (UActorComponent* C : Owner->GetComponents())
	{
		if (C && C->GetClass()->GetName() == TEXT("AccumulationDOFComponent"))
		{
			return C;
		}
	}
	return nullptr;
}

namespace
{
	template <typename TProp, typename TValue>
	bool SetReflected(UObject* Obj, const TCHAR* Name, TValue Value)
	{
		if (TProp* P = CastField<TProp>(Obj->GetClass()->FindPropertyByName(Name)))
		{
			P->SetPropertyValue_InContainer(Obj, Value);
			return true;
		}
		return false;
	}
	template <typename TProp, typename TValue>
	bool GetReflected(UObject* Obj, const TCHAR* Name, TValue& Out)
	{
		if (TProp* P = CastField<TProp>(Obj->GetClass()->FindPropertyByName(Name)))
		{
			Out = P->GetPropertyValue_InContainer(Obj);
			return true;
		}
		return false;
	}
}

void UDynamicLensComponent::ApplyAccumulationDOF(const FDynamicLensEval& Eval)
{
	UActorComponent* Accum = FindAccumulationDOF();
	if (!Accum)
	{
		if (bAccumApplied) RestoreAccumulationDOF();
		return;
	}
	if (!bAccumApplied || AccumulationDOF.Get() != Accum)
	{
		// remember what the user had
		UObject* Tex = nullptr;
		if (FObjectProperty* P = CastField<FObjectProperty>(Accum->GetClass()->FindPropertyByName(TEXT("BokehTexture")))) Tex = P->GetObjectPropertyValue_InContainer(Accum);
		AccumBackup.Texture = Tex;
		GetReflected<FBoolProperty>(Accum, TEXT("bEnableBokehTexture"), AccumBackup.bEnable);
		GetReflected<FFloatProperty>(Accum, TEXT("SphericalAberration"), AccumBackup.Spherical);
		GetReflected<FFloatProperty>(Accum, TEXT("ComaAberration"), AccumBackup.Coma);
		if (FByteProperty* P = CastField<FByteProperty>(Accum->GetClass()->FindPropertyByName(TEXT("WeightChannel")))) AccumBackup.Channel = P->GetPropertyValue_InContainer(Accum);
		else if (FEnumProperty* EP = CastField<FEnumProperty>(Accum->GetClass()->FindPropertyByName(TEXT("WeightChannel")))) AccumBackup.Channel = (uint8)EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Accum));
		AccumulationDOF = Accum;
		bAccumApplied = true;
		IrisTexKey = -1;
	}

	// iris polygon texture: blades + rotation, anti-aliased, linear, luminance = weight
	const int32 Blades = FMath::Clamp(Eval.Blades, 4, 16);
	const float Curv = FMath::Clamp(Eval.BladeCurvature, 0.f, 1.f);
	const float Sq = FMath::Clamp(Eval.BokehSqueeze, 1.f, 2.f);
	const int32 Key = Blades * 1000000 + FMath::RoundToInt(Eval.BladeRotationDeg) * 1000 + FMath::RoundToInt(Curv * 30.f) * 30 + FMath::RoundToInt((Sq - 1.f) * 29.f);
	if (!IrisTexture || IrisTexKey != Key)
	{
		constexpr int32 N = 256;
		if (!IrisTexture)
		{
			IrisTexture = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
			IrisTexture->SRGB = false;
			IrisTexture->Filter = TF_Bilinear;
			IrisTexture->NeverStream = true;
		}
		const float Rot = FMath::DegreesToRadians(Eval.BladeRotationDeg);
		const float Apothem = 0.96f * FMath::Cos(PI / Blades);   // polygon inscribed in a 0.96 circle
		FTexture2DMipMap& Mip = IrisTexture->GetPlatformData()->Mips[0];
		uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				// squeeze: anamorphic highlights are ovals Sq times taller than wide -> evaluate the shape at a compressed X
				const float X = ((I + 0.5f) / N * 2.f - 1.f) * Sq, Y = (J + 0.5f) / N * 2.f - 1.f;
				// signed distance to the regular polygon: max over edge normals of (p . n_k) - apothem
				float D = -1e9f;
				for (int32 K = 0; K < Blades; ++K)
				{
					const float A = Rot + (2.f * PI * K) / Blades;
					D = FMath::Max(D, X * FMath::Cos(A) + Y * FMath::Sin(A) - Apothem);
				}
				// rounded blades: blend toward the circumscribed circle
				D = FMath::Lerp(D, FMath::Sqrt(X * X + Y * Y) - 0.96f, Curv);
				const float Px = 2.f / N;
				const float Cov = FMath::Clamp(0.5f - D / Px, 0.f, 1.f);   // 1-pixel anti-aliasing
				const uint8 V = (uint8)FMath::RoundToInt(Cov * 255.f);
				uint8* P = Data + 4 * (J * N + I);
				P[0] = V; P[1] = V; P[2] = V; P[3] = 255;
			}
		}
		Mip.BulkData.Unlock();
		IrisTexture->UpdateResource();
		IrisTexKey = Key;
		if (FObjectProperty* P = CastField<FObjectProperty>(Accum->GetClass()->FindPropertyByName(TEXT("BokehTexture")))) P->SetObjectPropertyValue_InContainer(Accum, IrisTexture);
		SetReflected<FBoolProperty>(Accum, TEXT("bEnableBokehTexture"), true);
	}
	SetReflected<FFloatProperty>(Accum, TEXT("SphericalAberration"), Eval.SphericalAberration);
	SetReflected<FFloatProperty>(Accum, TEXT("ComaAberration"), Eval.Coma);
}

void UDynamicLensComponent::RestoreAccumulationDOF()
{
	if (!bAccumApplied) return;
	if (UActorComponent* Accum = AccumulationDOF.Get())
	{
		if (FObjectProperty* P = CastField<FObjectProperty>(Accum->GetClass()->FindPropertyByName(TEXT("BokehTexture")))) P->SetObjectPropertyValue_InContainer(Accum, AccumBackup.Texture);
		SetReflected<FBoolProperty>(Accum, TEXT("bEnableBokehTexture"), AccumBackup.bEnable);
		SetReflected<FFloatProperty>(Accum, TEXT("SphericalAberration"), AccumBackup.Spherical);
		SetReflected<FFloatProperty>(Accum, TEXT("ComaAberration"), AccumBackup.Coma);
		if (FByteProperty* P = CastField<FByteProperty>(Accum->GetClass()->FindPropertyByName(TEXT("WeightChannel")))) P->SetPropertyValue_InContainer(Accum, AccumBackup.Channel);
		else if (FEnumProperty* EP = CastField<FEnumProperty>(Accum->GetClass()->FindPropertyByName(TEXT("WeightChannel")))) EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Accum), (int64)AccumBackup.Channel);
	}
	bAccumApplied = false;
	AccumulationDOF = nullptr;
	IrisTexKey = -1;
}
