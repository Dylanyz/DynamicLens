#include "DynamicLensComponent.h"

#include "Camera/CameraActor.h"
#include "CameraCalibrationSubsystem.h"
#include "CineCameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "LensDistortionModelHandlerBase.h"
#include "LensFile.h"
#include "LensFileRendering.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Models/SphericalLensModel.h"
#include "SphericalLensDistortionModelHandler.h"

namespace
{
	constexpr int32 ProjectionMapSize = 256;
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
	const FName CircleParamCA(TEXT("ChromaticAberration"));
	const FName CircleParamScatter(TEXT("Scatter"));
}

UDynamicLensComponent::UDynamicLensComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;   // after Sequencer has written focal length / focus
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
		if (bEnabled && Preset)
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
	if (Name == GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, bEnabled) ||
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

void UDynamicLensComponent::MatchCameraToProfile()
{
	UCineCameraComponent* Cam = GetTargetCamera();
	if (!Cam || !Preset || !Preset->Profile) return;
	const UDynamicLensProfile* P = Preset->Profile;
#if WITH_EDITOR
	Cam->Modify();
#endif
	const float Squeeze = FMath::Max(P->Squeeze, 1.f);
	Cam->Filmback.SensorWidth = P->NativeSensorMm.X / Squeeze;
	Cam->Filmback.SensorHeight = P->NativeSensorMm.Y;
	Cam->Filmback.SensorAspectRatio = Cam->Filmback.SensorWidth / FMath::Max(Cam->Filmback.SensorHeight, 0.01f);
	Cam->LensSettings.SqueezeFactor = Squeeze;
	Cam->CropSettings.AspectRatio = 0.f;
	ClearEffect();
	TransientLensFile = nullptr;
	LensFileSTMapIndex = -1;
}

// ------------------------------------------------------------------------------------------------ apply

void UDynamicLensComponent::Apply(UCineCameraComponent* Cam)
{
	const float Focal = FMath::Max(Cam->CurrentFocalLength, 0.1f);
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

	FDynamicLensEval Eval = Preset->Evaluate(Focal, Focus, FStop, W, H, AmountMultiplier, Cam->LensSettings.DiaphragmBladeCount, Cam->LensSettings.SqueezeFactor);
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

	const UDynamicLensProfile* Profile = Preset->Profile;
	const EDynamicLensProfileType Type = Profile ? Profile->Type : EDynamicLensProfileType::Parametric;

	FLensDistortionState State;
	float Needed = 1.f;
	float Applied = 1.f;
	float CircleRadius = Eval.ImageCircleRadiusNorm;   // 0 = no circle

	auto MinCircle = [&](float R) { if (R > 0.f) CircleRadius = (CircleRadius > 0.f) ? FMath::Min(CircleRadius, R) : R; };

	if (Type == EDynamicLensProfileType::Projection && Profile)
	{
		// fisheye maths: the whole frame wants as much source as it can get; the image circle takes the rest
		Applied = (OverscanMode == EDynamicLensOverscanMode::Fixed) ? FixedOverscan : MaxOverscan;
		float Circle = 0.f;
		if (DriveProjection(Cam, Eval, Focal, W, H, Applied, Needed, State, Circle))
		{
			MinCircle(Circle);
		}
	}
	else if (Type == EDynamicLensProfileType::STMap && Profile)
	{
		float Circle = 0.f;
		if (DriveSTMap(Cam, Eval, Focal, Focus, W, H, Needed, State, Circle))
		{
			Applied = (OverscanMode == EDynamicLensOverscanMode::Fixed) ? FixedOverscan : FMath::Min(Needed, MaxOverscan);
			if (Needed > Applied + 1e-3f)
			{
				MinCircle(Applied / Needed);   // approximation: the map's border needs Needed, the centre needs 1
			}
		}
		else
		{
			Notes += TEXT("ST map could not be evaluated. ");
		}
	}
	else
	{
		DriveParametric(Cam, Eval, Focal, W, H, Needed, State);
		Applied = (OverscanMode == EDynamicLensOverscanMode::Fixed) ? FixedOverscan : FMath::Min(Needed, MaxOverscan);
		if (Needed > Applied + 1e-3f)
		{
			MinCircle(DynamicLensMath::ValidCircleRadius(Eval.Params, Focal / W, Focal / H, Applied));
		}
	}

	Applied = FMath::Clamp(Applied, 1.f, 2.f);

	// vignette and cat's eye belong to the picture you can see: if the image circle is inside the frame corners,
	// evaluate them at the circle's edge instead of the (black) frame corner
	const float CornerNorm = FMath::Sqrt(1.f + FMath::Square(H / W));
	if (CircleRadius > 0.f && CircleRadius < CornerNorm)
	{
		const float S = CircleRadius / CornerNorm;
		const FDynamicLensEval EdgeEval = Preset->Evaluate(Focal, Focus, FStop, W * S, H * S, AmountMultiplier, Cam->LensSettings.DiaphragmBladeCount, Cam->LensSettings.SqueezeFactor);
		Eval.VignetteIntensity = FMath::Clamp(EdgeEval.VignetteIntensity * VignetteMultiplier, 0.f, 1.f);
		Eval.CornerPupilVisible = EdgeEval.CornerPupilVisible;
		Eval.CornerFieldAngleDeg = EdgeEval.CornerFieldAngleDeg;
		Eval.BarrelRadiusMm = EdgeEval.BarrelRadiusMm;
		Eval.BarrelLengthMm = EdgeEval.BarrelLengthMm;
	}

	ApplyRendering(Cam, State, Applied);
	ApplyLook(Cam, Eval, bApplyImageCircle ? CircleRadius : 0.f, W / H);
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
	return true;
}

bool UDynamicLensComponent::DriveSTMap(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float Focus, float W, float H, float& OutNeededOverscan, FLensDistortionState& OutState, float& OutCircleRadius)
{
	const UDynamicLensProfile* Profile = Preset->Profile;
	const int32 Index = Profile->FindNearestSTMap(Focal);
	if (Index < 0) return false;
	const FDynamicLensSTMapEntry& Entry = Profile->STMaps[Index];

	// sensor fit: Crop keeps the map at the lens's physical scale (camera sensor must fit inside), Scale stretches it
	FVector2D LensSensor = FVector2D(W, H);
	if (SensorFit == EDynamicLensSensorFit::Crop)
	{
		if (W <= Profile->NativeSensorMm.X + 1e-3f && H <= Profile->NativeSensorMm.Y + 1e-3f)
		{
			LensSensor = Profile->NativeSensorMm;
		}
		else
		{
			Notes += TEXT("Sensor larger than the profile's: scaled instead of cropped. ");
		}
	}
	const FVector2D FxFy(Entry.FocalMm / LensSensor.X, Entry.FocalMm / LensSensor.Y);

	if (!TransientLensFile || LensFileSTMapIndex != Index || !LensFileSensor.Equals(LensSensor, 1e-3) || !LensFileFxFy.Equals(FxFy, 1e-4))
	{
		TransientLensFile = NewObject<ULensFile>(this, NAME_None, RF_Transient);
		TransientLensFile->LensInfo.LensModel = USphericalLensModel::StaticClass();
		TransientLensFile->LensInfo.SensorDimensions = LensSensor;
		TransientLensFile->LensInfo.SqueezeFactor = 1.f;
		TransientLensFile->DataMode = ELensDataMode::STMap;
		FSTMapInfo Info;
		Info.DistortionMap = Entry.Map;
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
	OutNeededOverscan = FMath::Clamp(FMath::Max(Handler->GetOverscanFactor(), Entry.NeededOverscan), 1.f, 4.f);
	OutCircleRadius = 0.f;
	if (FMath::Abs(Entry.FocalMm - Focal) > 0.5f)
	{
		Notes += FString::Printf(TEXT("Nearest ST map is %.0f mm (camera at %.1f mm). "), Entry.FocalMm, Focal);
	}
	return true;
}

bool UDynamicLensComponent::DriveProjection(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float Focal, float W, float H, float AppliedOverscan, float& OutNeededOverscan, FLensDistortionState& OutState, float& OutCircleRadius)
{
	const UDynamicLensProfile* Profile = Preset->Profile;
	const float ThetaMax = FMath::DegreesToRadians(FMath::Clamp(Profile->MaxFieldAngleDeg, 10.f, 110.f));
	const float O = FMath::Clamp(AppliedOverscan, 1.f, 2.f);
	const EDynamicLensProjection Proj = Profile->Projection;

	const bool bDirty = !ProjectionMap || !FMath::IsNearlyEqual(ProjectionKeyFocal, Focal, 1e-3f) || !ProjectionKeySensor.Equals(FVector2D(W, H), 1e-3)
		|| !FMath::IsNearlyEqual(ProjectionKeyOverscan, O, 1e-3f) || ProjectionKeyType != (int32)Proj || !FMath::IsNearlyEqual(ProjectionKeyMaxAngle, ThetaMax, 1e-4f);
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
				if (R > KINDA_SMALL_NUMBER && DynamicLensMath::ProjectionTheta(Proj, R / Focal, Theta) && Theta <= ThetaMax && Theta < HALF_PI - 0.01f)
				{
					const float Ru = Focal * FMath::Tan(Theta);       // radius in the rectilinear render
					const float SX = X / R * Ru, SY = Y / R * Ru;
					if (FMath::Abs(SX) <= LimX && FMath::Abs(SY) <= LimY)
					{
						SU = 0.5f + SX / W;
						SV = 0.5f + SY / H;
					}
				}
				float* Px = Data + 2 * (J * ProjectionMapSize + I);
				Px[0] = SU; Px[1] = SV;
			}
		}
		Mip.BulkData.Unlock();
		ProjectionMap->UpdateResource();

		// visible circle: where the source runs out (x or y edge) or the lens's own field limit, whichever is first
		const float RadX = Focal * DynamicLensMath::ProjectionG(Proj, FMath::Min(ThetaCapX, ThetaMax));
		const float RadY = Focal * DynamicLensMath::ProjectionG(Proj, FMath::Min(ThetaCapY, ThetaMax));
		const float RadMax = Focal * DynamicLensMath::ProjectionG(Proj, ThetaMax);
		ProjectionCircleRadius = FMath::Min3(RadX, RadY, RadMax) / (0.5f * W);
		ProjectionNeededOverscan = O;

		ProjectionKeyFocal = Focal; ProjectionKeySensor = FVector2D(W, H); ProjectionKeyOverscan = O;
		ProjectionKeyType = (int32)Proj; ProjectionKeyMaxAngle = ThetaMax;
		TransientLensFile = nullptr;
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
	OutCircleRadius = ProjectionCircleRadius;
	return true;
}

void UDynamicLensComponent::ApplyRendering(UCineCameraComponent* Cam, const FLensDistortionState& State, float AppliedOverscan)
{
	const float CamOverscan = FMath::Clamp(AppliedOverscan - 1.f, 0.f, 1.f);
	Cam->Overscan = CamOverscan;
	Cam->bScaleResolutionWithOverscan = bScaleResolutionWithOverscan;
	bOverscanTouched = true;
	Handler->SetOverscanFactor(CamOverscan + 1.f);   // material and camera must agree (same as Epic's LensComponent)
	if (Preset->Profile == nullptr || Preset->Profile->Type == EDynamicLensProfileType::Parametric)
	{
		Handler->ProcessCurrentDistortion();
	}

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
	LastCircleRadius = -1.f;
}

void UDynamicLensComponent::ApplyLook(UCineCameraComponent* Cam, const FDynamicLensEval& Eval, float CircleRadiusNorm, float Aspect)
{
	const bool bDoBokeh = bApplyBokeh && Eval.bBokeh;
	const bool bDoVignette = bApplyVignette && Eval.bVignette;

	// --- image circle mask
	if (CircleRadiusNorm > 0.f)
	{
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
			if (!FMath::IsNearlyEqual(LastCircleRadius, CircleRadiusNorm, 1e-4f) || bEdgeChanged)
			{
				const FDynamicLensImageCircleEdge& Ed = Eval.Edge;
				CircleMID->SetScalarParameterValue(CircleParamRadius, CircleRadiusNorm);
				CircleMID->SetScalarParameterValue(CircleParamSoftness, Eval.ImageCircleSoftness);
				CircleMID->SetScalarParameterValue(CircleParamAspect, Aspect);
				CircleMID->SetScalarParameterValue(CircleParamFalloff, Ed.FalloffPower);
				CircleMID->SetScalarParameterValue(CircleParamOpacity, Ed.Opacity);
				CircleMID->SetScalarParameterValue(CircleParamCenterX, Ed.CenterOffset.X);
				CircleMID->SetScalarParameterValue(CircleParamCenterY, Ed.CenterOffset.Y);
				CircleMID->SetScalarParameterValue(CircleParamEllipticity, Ed.Ellipticity);
				CircleMID->SetScalarParameterValue(CircleParamWobble, Ed.Wobble);
				CircleMID->SetScalarParameterValue(CircleParamWobbleLobes, (float)Ed.WobbleLobes);
				CircleMID->SetScalarParameterValue(CircleParamWobbleSeed, FMath::DegreesToRadians(Ed.WobbleSeed));
				CircleMID->SetScalarParameterValue(CircleParamEdgeNoise, Ed.EdgeNoise);
				CircleMID->SetScalarParameterValue(CircleParamNoiseScale, Ed.NoiseScale);
				CircleMID->SetScalarParameterValue(CircleParamCA, Ed.ChromaticAberration);
				CircleMID->SetScalarParameterValue(CircleParamScatter, Ed.Scatter);
				UTexture2D* MaskTex = Ed.MaskTexture.IsNull() ? nullptr : Ed.MaskTexture.LoadSynchronous();
				CircleMID->SetScalarParameterValue(CircleParamMaskStrength, MaskTex ? Ed.MaskStrength : 0.f);
				if (MaskTex)
				{
					CircleMID->SetTextureParameterValue(CircleParamMask, MaskTex);
				}
				LastCircleRadius = CircleRadiusNorm;
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
		const int32 EffBlades = FMath::RoundToInt(FMath::Lerp((float)Eval.Blades, 16.f, Eval.BladeCurvature));
		P.bOverride_DepthOfFieldBladeCount = true; P.DepthOfFieldBladeCount = FMath::Clamp(EffBlades, 4, 16);
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
	AppliedMID = nullptr;
	bCircleApplied = false;
	bSVEActive = false;
	bStrippedForeign = false;
	AppliedCamera = nullptr;
	LastOverscanFactor = 1.f;
	LastVignette = 0.f;
	ImageCircleRadius = 0.f;
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
