#include "DynamicLensTypes.h"
#if WITH_EDITOR
#include "IPythonScriptPlugin.h"
#endif

namespace
{
	float Smooth01(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** index i and blend t such that value = lerp(v[i], v[i+1], t); clamped at both ends. */
	void Bracket(const TArray<float>& Xs, float X, int32& OutIndex, float& OutT)
	{
		const int32 N = Xs.Num();
		if (N < 2)
		{
			OutIndex = 0; OutT = 0.f; return;
		}
		if (X <= Xs[0]) { OutIndex = 0; OutT = 0.f; return; }
		if (X >= Xs[N - 1]) { OutIndex = N - 2; OutT = 1.f; return; }
		int32 Lo = 0, Hi = N - 1;
		while (Hi - Lo > 1)
		{
			const int32 Mid = (Lo + Hi) / 2;
			if (Xs[Mid] <= X) Lo = Mid; else Hi = Mid;
		}
		OutIndex = Lo;
		OutT = (X - Xs[Lo]) / FMath::Max(Xs[Hi] - Xs[Lo], KINDA_SMALL_NUMBER);
	}
}

// ------------------------------------------------------------------------------------------------ math

float DynamicLensMath::DiscOverlapFraction(float A, float B, float D)
{
	if (A <= KINDA_SMALL_NUMBER) return 1.f;
	if (B <= KINDA_SMALL_NUMBER) return 0.f;
	if (D >= A + B) return 0.f;
	if (D <= FMath::Abs(B - A)) return (B >= A) ? 1.f : (B * B) / (A * A);
	const float A2 = A * A, B2 = B * B, D2 = D * D;
	const float Alpha = FMath::Acos(FMath::Clamp((D2 + A2 - B2) / (2.f * D * A), -1.f, 1.f));
	const float Beta = FMath::Acos(FMath::Clamp((D2 + B2 - A2) / (2.f * D * B), -1.f, 1.f));
	const float Area = A2 * (Alpha - 0.5f * FMath::Sin(2.f * Alpha)) + B2 * (Beta - 0.5f * FMath::Sin(2.f * Beta));
	return FMath::Clamp(Area / (PI * A2), 0.f, 1.f);
}

float DynamicLensMath::RadialForward(float R, const FDynamicLensParams& P)
{
	const float R2 = R * R;
	return R * (1.f + P.K1 * R2 + P.K2 * R2 * R2 + P.K3 * R2 * R2 * R2);
}

FDynamicLensParams DynamicLensMath::MakeMonotonic(const FDynamicLensParams& P, float MaxRadius)
{
	// derivative of r(1 + k1 r^2 + k2 r^4 + k3 r^6) = 1 + 3k1 r^2 + 5k2 r^4 + 7k3 r^6 must stay > 0 on [0, MaxRadius]
	FDynamicLensParams Out = P;
	for (int32 Iter = 0; Iter < 40; ++Iter)
	{
		float MinDeriv = 1.f;
		for (int32 I = 1; I <= 64; ++I)
		{
			const float R = MaxRadius * I / 64.f;
			const float R2 = R * R;
			const float Deriv = 1.f + 3.f * Out.K1 * R2 + 5.f * Out.K2 * R2 * R2 + 7.f * Out.K3 * R2 * R2 * R2;
			MinDeriv = FMath::Min(MinDeriv, Deriv);
		}
		if (MinDeriv > 0.05f) break;
		Out.K1 *= 0.9f; Out.K2 *= 0.9f; Out.K3 *= 0.9f;
	}
	return Out;
}

float DynamicLensMath::RadialInverse(float Rd, const FDynamicLensParams& P, float MaxRadius)
{
	if (Rd <= 0.f) return 0.f;
	float Lo = 0.f, Hi = MaxRadius;
	if (RadialForward(Hi, P) < Rd) return Hi;   // out of the monotonic range: cap
	for (int32 I = 0; I < 40; ++I)
	{
		const float Mid = 0.5f * (Lo + Hi);
		if (RadialForward(Mid, P) < Rd) Lo = Mid; else Hi = Mid;
	}
	return 0.5f * (Lo + Hi);
}

float DynamicLensMath::ComputeOverscan(const FDynamicLensParams& P, float Fx, float Fy, int32 SamplesPerEdge)
{
	// Output (distorted) frame border in normalized view space: x = (u-0.5)/Fx, y = (v-0.5)/Fy.
	// For each border point find the undistorted source point that lands there; overscan is how far outside the
	// frame that source point sits. Radial model => direction is preserved, only the radius changes.
	const float HalfX = 0.5f / FMath::Max(Fx, KINDA_SMALL_NUMBER);
	const float HalfY = 0.5f / FMath::Max(Fy, KINDA_SMALL_NUMBER);
	const float CornerR = FMath::Sqrt(HalfX * HalfX + HalfY * HalfY);
	const FDynamicLensParams Safe = MakeMonotonic(P, CornerR * 2.f);
	float Over = 1.f;
	const int32 N = FMath::Max(SamplesPerEdge, 2);
	auto Test = [&](float X, float Y)
	{
		const float Rd = FMath::Sqrt(X * X + Y * Y);
		if (Rd <= KINDA_SMALL_NUMBER) return;
		const float Ru = RadialInverse(Rd, Safe, CornerR * 4.f);
		const float S = Ru / Rd;
		Over = FMath::Max(Over, FMath::Abs(X * S) / HalfX);
		Over = FMath::Max(Over, FMath::Abs(Y * S) / HalfY);
	};
	for (int32 I = 0; I <= N; ++I)
	{
		const float T = -1.f + 2.f * I / N;
		Test(T * HalfX, -HalfY); Test(T * HalfX, HalfY);
		Test(-HalfX, T * HalfY); Test(HalfX, T * HalfY);
	}
	return FMath::Clamp(Over, 1.f, 4.f);
}

float DynamicLensMath::ValidCircleRadius(const FDynamicLensParams& P, float Fx, float Fy, float OverscanFactor)
{
	const float HalfX = 0.5f / FMath::Max(Fx, KINDA_SMALL_NUMBER);
	const float HalfY = 0.5f / FMath::Max(Fy, KINDA_SMALL_NUMBER);
	const float O = FMath::Max(OverscanFactor, 1.f);
	// the source render covers |x_u| <= O*HalfX, |y_u| <= O*HalfY; the nearest source edge limits the valid circle
	const float Rx = RadialForward(O * HalfX, P);
	const float Ry = RadialForward(O * HalfY, P);
	return FMath::Min(Rx, Ry) / HalfX;   // in half-frame-width units
}

float DynamicLensMath::ProjectionG(EDynamicLensProjection Projection, float Theta)
{
	switch (Projection)
	{
	case EDynamicLensProjection::Stereographic: return 2.f * FMath::Tan(0.5f * Theta);
	case EDynamicLensProjection::Equisolid:     return 2.f * FMath::Sin(0.5f * Theta);
	case EDynamicLensProjection::Orthographic:  return FMath::Sin(Theta);
	default:                                    return Theta;
	}
}

bool DynamicLensMath::ProjectionTheta(EDynamicLensProjection Projection, float ROverF, float& OutTheta)
{
	switch (Projection)
	{
	case EDynamicLensProjection::Stereographic: OutTheta = 2.f * FMath::Atan(0.5f * ROverF); return true;
	case EDynamicLensProjection::Equisolid:
		if (ROverF > 2.f) return false;
		OutTheta = 2.f * FMath::Asin(0.5f * ROverF); return true;
	case EDynamicLensProjection::Orthographic:
		if (ROverF > 1.f) return false;
		OutTheta = FMath::Asin(ROverF); return true;
	default: OutTheta = ROverF; return true;
	}
}

// ------------------------------------------------------------------------------------------------ profile

FDynamicLensParams FDynamicLensParams::Lerp(const FDynamicLensParams& A, const FDynamicLensParams& B, float T)
{
	FDynamicLensParams R;
	R.K1 = FMath::Lerp(A.K1, B.K1, T);
	R.K2 = FMath::Lerp(A.K2, B.K2, T);
	R.K3 = FMath::Lerp(A.K3, B.K3, T);
	R.P1 = FMath::Lerp(A.P1, B.P1, T);
	R.P2 = FMath::Lerp(A.P2, B.P2, T);
	return R;
}

bool UDynamicLensProfile::IsValidProfile() const
{
	switch (Type)
	{
	case EDynamicLensProfileType::Parametric:
	{
		if (FocusCm.Num() < 1 || Rows.Num() < 1) return false;
		for (const FDynamicLensProfileRow& Row : Rows)
		{
			if (Row.ByFocus.Num() != FocusCm.Num()) return false;
		}
		return true;
	}
	case EDynamicLensProfileType::STMap:
		for (const FDynamicLensSTMapEntry& E : STMaps) { if (E.Map) return true; }
		return false;
	case EDynamicLensProfileType::Projection:
		return true;
	}
	return false;
}

float UDynamicLensProfile::GetLockedFocal(float FocalMm) const
{
	if (NominalFocalMm > KINDA_SMALL_NUMBER) return NominalFocalMm;
	if (Type == EDynamicLensProfileType::STMap && STMaps.Num())
	{
		const int32 I = FindNearestSTMap(FocalMm);
		if (I >= 0) return STMaps[I].FocalMm;
	}
	return 0.f;
}

void UDynamicLensProfile::GetFocalRange(float& MinMm, float& MaxMm) const
{
	MinMm = MaxMm = 0.f;
	if (Type == EDynamicLensProfileType::Parametric && Rows.Num())
	{
		MinMm = Rows[0].FocalMm; MaxMm = Rows.Last().FocalMm;
	}
	else if (Type == EDynamicLensProfileType::STMap && STMaps.Num())
	{
		MinMm = MaxMm = STMaps[0].FocalMm;
		for (const FDynamicLensSTMapEntry& E : STMaps) { MinMm = FMath::Min(MinMm, E.FocalMm); MaxMm = FMath::Max(MaxMm, E.FocalMm); }
	}
}

int32 UDynamicLensProfile::FindNearestSTMap(float FocalMm) const
{
	int32 Best = -1; float BestD = FLT_MAX;
	for (int32 I = 0; I < STMaps.Num(); ++I)
	{
		if (!STMaps[I].Map) continue;
		const float D = FMath::Abs(FMath::Loge(FMath::Max(STMaps[I].FocalMm, 0.1f)) - FMath::Loge(FMath::Max(FocalMm, 0.1f)));
		if (D < BestD) { BestD = D; Best = I; }
	}
	return Best;
}

float UDynamicLensProfile::EffectiveImageCircleMm() const
{
	if (ImageCircleMm > KINDA_SMALL_NUMBER) return ImageCircleMm;
	return FMath::Sqrt(NativeSensorMm.X * NativeSensorMm.X + NativeSensorMm.Y * NativeSensorMm.Y);
}

float UDynamicLensProfile::ComputeBarrelLengthMm(float FocalMm, float BarrelRadiusMm) const
{
	// Find the barrel length at which the wide-open pupil is PupilVisibleAtImageCircle visible at the image-circle
	// edge. Pupil radius a = f / (2 T); its centre sits BarrelLength * tan(theta_ic) off the barrel axis at the
	// barrel opening. Overlap is monotonic in the offset, so bisect the offset, then convert to a length.
	const float F = FMath::Max(FocalMm, 0.1f);
	const float A = F / (2.f * FMath::Max(MaxAperture, 0.7f));
	const float R = FMath::Max(BarrelRadiusMm, 0.01f);
	const float Target = FMath::Clamp(PupilVisibleAtImageCircle, 0.05f, 1.f);
	const float TanIC = (0.5f * EffectiveImageCircleMm()) / F;
	if (TanIC <= KINDA_SMALL_NUMBER || Target >= 0.999f) return 0.f;
	float Lo = 0.f, Hi = R + A;
	if (DynamicLensMath::DiscOverlapFraction(A, R, Lo) <= Target) return 0.f;   // pupil already clipped on axis: no barrel model fits
	for (int32 I = 0; I < 40; ++I)
	{
		const float Mid = 0.5f * (Lo + Hi);
		if (DynamicLensMath::DiscOverlapFraction(A, R, Mid) > Target) Lo = Mid; else Hi = Mid;
	}
	return 0.5f * (Lo + Hi) / TanIC;
}

void UDynamicLensProfile::RefreshCoverage()
{
	const FString PrimeText = (NominalFocalMm > KINDA_SMALL_NUMBER) ? FString::Printf(TEXT("prime %.4g mm | "), NominalFocalMm) : FString();
	float MinMm, MaxMm; GetFocalRange(MinMm, MaxMm);
	FString Focal;
	switch (Type)
	{
	case EDynamicLensProfileType::Parametric: Focal = FString::Printf(TEXT("%d focal lengths %.0f–%.0f mm, %d focus steps, zoomable"), Rows.Num(), MinMm, MaxMm, FocusCm.Num()); break;
	case EDynamicLensProfileType::STMap: Focal = FString::Printf(TEXT("%d ST maps %.0f–%.0f mm (primes, nearest is used)"), STMaps.Num(), MinMm, MaxMm); break;
	case EDynamicLensProfileType::Projection: Focal = FString::Printf(TEXT("any focal length, %s up to %.0f° off-axis"), *StaticEnum<EDynamicLensProjection>()->GetDisplayNameTextByValue((int64)Projection).ToString(), MaxFieldAngleDeg); break;
	}
	if (!PrimeText.IsEmpty()) { Focal.RemoveFromStart(TEXT("any focal length, ")); }
	Coverage = FString::Printf(TEXT("%s%s | sensor %.2f x %.2f mm%s | image circle %.1f mm"), *PrimeText, *Focal, NativeSensorMm.X, NativeSensorMm.Y,
		Squeeze > 1.001f ? *FString::Printf(TEXT(" (%.1fx anamorphic)"), Squeeze) : TEXT(""), EffectiveImageCircleMm());
}

#if WITH_EDITOR
void UDynamicLensProfile::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshCoverage();
}
#endif

void UDynamicLensProfile::PostLoad()
{
	Super::PostLoad();
	RefreshCoverage();
}

FDynamicLensParams UDynamicLensProfile::Evaluate(float FocalMm, float InFocusCm) const
{
	FDynamicLensParams Out;
	if (Type != EDynamicLensProfileType::Parametric || !IsValidProfile()) return Out;

	TArray<float> LogFocals; LogFocals.Reserve(Rows.Num());
	for (const FDynamicLensProfileRow& Row : Rows) LogFocals.Add(FMath::Loge(FMath::Max(Row.FocalMm, 0.01f)));
	TArray<float> LogFocus; LogFocus.Reserve(FocusCm.Num());
	for (float F : FocusCm) LogFocus.Add(FMath::Loge(FMath::Max(F, 0.01f)));

	int32 Fi, Di; float Ft, Dt;
	Bracket(LogFocals, FMath::Loge(FMath::Max(FocalMm, 0.01f)), Fi, Ft);
	Bracket(LogFocus, FMath::Loge(FMath::Max(InFocusCm, 0.01f)), Di, Dt);

	const int32 Fi1 = FMath::Min(Fi + 1, Rows.Num() - 1);
	const int32 Di1 = FMath::Min(Di + 1, FocusCm.Num() - 1);

	const FDynamicLensParams A = FDynamicLensParams::Lerp(Rows[Fi].ByFocus[Di], Rows[Fi].ByFocus[Di1], Dt);
	const FDynamicLensParams B = FDynamicLensParams::Lerp(Rows[Fi1].ByFocus[Di], Rows[Fi1].ByFocus[Di1], Dt);
	return FDynamicLensParams::Lerp(A, B, Ft);
}

// ------------------------------------------------------------------------------------------------ preset

FDynamicLensEval FDynamicLensSettings::Evaluate(float FocalMm, float FocusCm, float FStop, float SensorWmm, float SensorHmm, float AmountMultiplier, int32 CameraBlades, float CameraSqueeze) const
{
	FDynamicLensEval E;
	const float Focal = FMath::Max(FocalMm, 0.1f);
	const float Focus = FMath::Max(FocusCm, 1.f);
	const float Stop = FMath::Max(FStop, 0.7f);
	const float SW = FMath::Max(SensorWmm, 0.1f), SH = FMath::Max(SensorHmm, 0.1f);

	const float HalfDiag = 0.5f * FMath::Sqrt(SW * SW + SH * SH);
	const float TanCorner = HalfDiag / Focal;
	const float CornerAngle = FMath::Atan(TanCorner);
	E.CornerFieldAngleDeg = FMath::RadiansToDegrees(CornerAngle);

	// data-sheet specs (blades, squeeze, front diameter, image circle) are valid whenever a profile is assigned;
	// only the distortion tables need IsValidProfile()
	const UDynamicLensProfile* Profile = Distortion.Profile;
	const bool bProfile = Profile != nullptr;
	const bool bParametric = bProfile && Profile->Type == EDynamicLensProfileType::Parametric && Profile->IsValidProfile();

	// --- distortion (parametric profiles; ST map / projection profiles are handled by the component)
	if (bParametric)
	{
		float MinMm, MaxMm; Profile->GetFocalRange(MinMm, MaxMm);
		const float EvalFocal = (Distortion.OutOfRange != EDynamicLensRangeMode::Extrapolate) ? FMath::Clamp(Focal, MinMm, MaxMm) : Focal;
		auto AtFocal = [&](float F)
		{
			return FDynamicLensParams::Lerp(Profile->Evaluate(F, 1e6f), Profile->Evaluate(F, Focus), Distortion.Breathing);
		};
		E.Params = AtFocal(EvalFocal);
		if (Distortion.OutOfRange == EDynamicLensRangeMode::Clamp && !FMath::IsNearlyEqual(EvalFocal, Focal, 1e-3f))
		{
			// coefficients live in focal-length-normalised coordinates (r = r_mm / f): keep the clamped lens's pattern in
			// millimetres by rescaling them to this focal length. r_c = r * (f / f_c) -> K1 * s^2, K2 * s^4, K3 * s^6, P * s
			const float S = Focal / FMath::Max(EvalFocal, 0.01f);
			const float S2 = S * S;
			E.Params.K1 *= S2; E.Params.K2 *= S2 * S2; E.Params.K3 *= S2 * S2 * S2;
			E.Params.P1 *= S; E.Params.P2 *= S;
		}
		if (Distortion.OutOfRange == EDynamicLensRangeMode::Extrapolate && Profile->Rows.Num() >= 2 && (Focal < MinMm || Focal > MaxMm))
		{
			const int32 N = Profile->Rows.Num();
			const float F0 = (Focal < MinMm) ? Profile->Rows[0].FocalMm : Profile->Rows[N - 2].FocalMm;
			const float F1 = (Focal < MinMm) ? Profile->Rows[1].FocalMm : Profile->Rows[N - 1].FocalMm;
			const float T = (FMath::Loge(Focal) - FMath::Loge(F0)) / FMath::Max(FMath::Loge(F1) - FMath::Loge(F0), 1e-4f);
			E.Params = FDynamicLensParams::Lerp(AtFocal(F0), AtFocal(F1), T);
		}
	}
	const float TotalAmount = Distortion.Amount * FMath::Max(AmountMultiplier, 0.f);
	E.Params.K1 *= TotalAmount; E.Params.K2 *= TotalAmount; E.Params.K3 *= TotalAmount;
	E.Params.P1 *= TotalAmount; E.Params.P2 *= TotalAmount;

	if (Distortion.WideBoost.bEnabled)
	{
		const float Span = FMath::Max(Distortion.WideBoost.BelowMm - Distortion.WideBoost.FullMm, 0.001f);
		const float T = Smooth01((Distortion.WideBoost.BelowMm - Focal) / Span);
		E.Params.K1 += Distortion.WideBoost.K1 * T;
		E.Params.K2 += Distortion.WideBoost.K2 * T;
	}
	{
		const float CornerR = FMath::Sqrt(FMath::Square(0.5f * SW / Focal) + FMath::Square(0.5f * SH / Focal));
		E.Params = DynamicLensMath::MakeMonotonic(E.Params, CornerR * 1.5f);
	}

	// --- physical pupil geometry (shared by vignette + bokeh)
	const float PupilDiameter = Focal / Stop;
	const float FrontRadius = bProfile ? 0.5f * Profile->FrontDiameterMm : 57.f;
	const float CatsEye = FMath::Max(Bokeh.CatsEyeStrength, 0.f);
	const float EffRadius = (CatsEye > KINDA_SMALL_NUMBER) ? FrontRadius / CatsEye : 1e6f;
	const float BarrelLen = bProfile ? Profile->ComputeBarrelLengthMm(Focal, EffRadius) : 0.f;
	E.CornerPupilVisible = (BarrelLen > 0.f) ? DynamicLensMath::DiscOverlapFraction(0.5f * PupilDiameter, EffRadius, BarrelLen * TanCorner) : 1.f;

	// --- image circle (normalized: 1 = half the frame width)
	E.bImageCircle = ImageCircle.bEnabled;
	E.ImageCircleSoftness = ImageCircle.Softness;
	E.Edge = ImageCircle.Edge;
	if (ImageCircle.bEnabled && bProfile && Profile->ImageCircleMm > KINDA_SMALL_NUMBER)
	{
		E.ImageCircleRadiusNorm = Profile->ImageCircleMm / SW;
	}

	// --- vignette
	E.bVignette = Vignette.bEnabled;
	if (Vignette.bEnabled)
	{
		if (Vignette.Mode == EDynamicLensLayerMode::Physical)
		{
			const float Cos = FMath::Cos(CornerAngle);
			const float Natural = (1.f - Cos * Cos * Cos * Cos) * Vignette.NaturalFalloff;
			const float Mechanical = (1.f - E.CornerPupilVisible) * Vignette.MechanicalStrength;
			E.VignetteIntensity = FMath::Clamp(Natural + Mechanical, 0.f, 1.f);
		}
		else
		{
			const float LW = FMath::Loge(FMath::Max(Vignette.WideMm, 0.1f));
			const float LL = FMath::Loge(FMath::Max(Vignette.LongMm, 0.1f));
			const float Vt = (FMath::Abs(LW - LL) < KINDA_SMALL_NUMBER) ? 0.f : Smooth01((FMath::Loge(Focal) - LL) / (LW - LL));
			const float Ft = Smooth01((Stop - Vignette.FStopOpen) / FMath::Max(Vignette.FStopClosed - Vignette.FStopOpen, 0.001f));
			E.VignetteIntensity = FMath::Clamp((Vignette.AtLong + (Vignette.AtWide - Vignette.AtLong) * Vt) * (1.f - Ft * Vignette.StopDownFade), 0.f, 1.f);
		}
	}

	// --- bokeh
	E.bBokeh = Bokeh.bEnabled;
	if (Bokeh.bEnabled)
	{
		if (Bokeh.Mode == EDynamicLensLayerMode::Physical)
		{
			E.BarrelRadiusMm = (CatsEye > KINDA_SMALL_NUMBER && BarrelLen > 0.f) ? EffRadius : 0.f;
			E.BarrelLengthMm = (CatsEye > KINDA_SMALL_NUMBER) ? BarrelLen : 0.f;
		}
		else
		{
			E.BarrelRadiusMm = Bokeh.BarrelRadiusMm;
			E.BarrelLengthMm = Bokeh.BarrelLengthMm;
		}
		// iris: count, shape and squeeze each from the profile, the camera or the preset
		switch (Bokeh.BladeSource)
		{
		case EDynamicLensValueSource::Camera:  E.Blades = (CameraBlades >= 4) ? CameraBlades : (bProfile ? Profile->IrisBlades : 9); break;
		case EDynamicLensValueSource::Custom:  E.Blades = Bokeh.Blades; break;
		default:                               E.Blades = bProfile ? Profile->IrisBlades : 9; break;
		}
		E.Blades = FMath::Clamp(E.Blades, 4, 16);
		E.BladeCurvature = FMath::Clamp(Bokeh.bOverrideBladeCurvature ? Bokeh.BladeCurvature : (bProfile ? Profile->BladeCurvature : 0.f), 0.f, 1.f);
		switch (Bokeh.SqueezeSource)
		{
		case EDynamicLensValueSource::Camera:  E.BokehSqueeze = CameraSqueeze; break;
		case EDynamicLensValueSource::Custom:  E.BokehSqueeze = Bokeh.Squeeze; break;
		default:                               E.BokehSqueeze = bProfile ? Profile->Squeeze : 1.f; break;
		}
		E.BokehSqueeze = FMath::Clamp(E.BokehSqueeze, 1.f, 2.f);
		const float OpenStop = bProfile ? Profile->MaxAperture : 1.4f;
		const float Fade = 1.f - Smooth01((Stop - OpenStop) / FMath::Max(Bokeh.SwirlFadesByFStop - OpenStop, 0.001f));
		E.Petzval = Bokeh.Petzval * Fade;
		E.PetzvalFalloff = Bokeh.PetzvalFalloff;
		E.SwirlExclusionBox = Bokeh.SwirlExclusionBox;
		E.SwirlExclusionRadius = Bokeh.SwirlExclusionRadius;
		E.bDriveAccumulationDOF = Bokeh.bDriveAccumulationDOF;
		E.SphericalAberration = Bokeh.SphericalAberration;
		E.Coma = Bokeh.Coma;
		E.BladeRotationDeg = Bokeh.BladeRotationDeg;
	}
	return E;
}

FDynamicLensSettings UDynamicLensPreset::GetSettings() const
{
	FDynamicLensSettings S;
	S.Distortion = Distortion; S.ImageCircle = ImageCircle; S.Vignette = Vignette; S.Bokeh = Bokeh; S.Overscan = Overscan;
	return S;
}

void UDynamicLensPreset::SetSettings(const FDynamicLensSettings& In)
{
	Distortion = In.Distortion; ImageCircle = In.ImageCircle; Vignette = In.Vignette; Bokeh = In.Bokeh; Overscan = In.Overscan;
}

FDynamicLensEval UDynamicLensPreset::Evaluate(float FocalMm, float FocusCm, float FStop, float SensorWmm, float SensorHmm, float AmountMultiplier, int32 CameraBlades, float CameraSqueeze) const
{
	return GetSettings().Evaluate(FocalMm, FocusCm, FStop, SensorWmm, SensorHmm, AmountMultiplier, CameraBlades, CameraSqueeze);
}


namespace
{
	void DynamicLensRunPython(const FString& Code)
	{
#if WITH_EDITOR
		if (IPythonScriptPlugin* Py = IPythonScriptPlugin::Get())
		{
			Py->ExecPythonCommand(*Code);
		}
#endif
	}
}

void UDynamicLensProfile::ResetToShipped()
{
	DynamicLensRunPython(FString::Printf(TEXT("import dynamiclens_tools as dl; dl.reset_asset('%s')"), *GetPathName()));
}

void UDynamicLensPreset::ResetToShipped()
{
	DynamicLensRunPython(FString::Printf(TEXT("import dynamiclens_tools as dl; dl.reset_asset('%s')"), *GetPathName()));
}
