// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#include "DynamicLensTypes.h"
#include "DynamicLensLibrary.h"
#include "Engine/Texture2D.h"
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
	// the polynomial is only monotonic up to some radius (beyond it barrel distortion folds back): search the rising
	// part only, otherwise a folded value at MaxRadius reads as "unreachable" and the overscan is reported as infinite
	float Lo = 0.f, Hi = MaxRadius;
	{
		const int32 Steps = 64;
		float Prev = 0.f;
		for (int32 I = 1; I <= Steps; ++I)
		{
			const float R = MaxRadius * I / Steps;
			const float F = RadialForward(R, P);
			if (F < Prev) { Hi = MaxRadius * (I - 1) / Steps; break; }
			Prev = F;
		}
	}
	if (RadialForward(Hi, P) < Rd) return Hi;   // genuinely beyond what the lens maps: cap
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

void DynamicLensMath::ValidExtents(const FDynamicLensParams& P, float Fx, float Fy, float OverscanFactor, float& OutRx, float& OutRy)
{
	const float HalfX = 0.5f / FMath::Max(Fx, KINDA_SMALL_NUMBER);
	const float HalfY = 0.5f / FMath::Max(Fy, KINDA_SMALL_NUMBER);
	const float O = FMath::Max(OverscanFactor, 1.f);
	OutRx = RadialForward(O * HalfX, P) / HalfX;
	OutRy = RadialForward(O * HalfY, P) / HalfX;
}

void DynamicLensMath::ValidCorner(const FDynamicLensParams& P, float Fx, float Fy, float OverscanFactor, float& OutX, float& OutY)
{
	const float HalfX = 0.5f / FMath::Max(Fx, KINDA_SMALL_NUMBER);
	const float HalfY = 0.5f / FMath::Max(Fy, KINDA_SMALL_NUMBER);
	const float O = FMath::Max(OverscanFactor, 1.f);
	const float Ru = O * FMath::Sqrt(HalfX * HalfX + HalfY * HalfY);
	const float S = RadialForward(Ru, P) / FMath::Max(Ru, KINDA_SMALL_NUMBER);   // radial model: direction kept
	OutX = O * S;                     // O * HalfX * S / HalfX
	OutY = O * (HalfY / HalfX) * S;
}

float DynamicLensMath::SolveSquareness(float A, float B, float Xc, float Yc)
{
	const float U = FMath::Abs(Xc) / FMath::Max(A, KINDA_SMALL_NUMBER), V = FMath::Abs(Yc) / FMath::Max(B, KINDA_SMALL_NUMBER);
	auto F = [&](float N) { return FMath::Pow(U, N) + FMath::Pow(V, N) - 1.f; };
	if (F(2.f) <= 0.f) return 2.f;        // corner already inside the ellipse
	if (F(64.f) > 0.f) return 64.f;       // corner outside even the near-rectangle: as square as it gets
	float Lo = 2.f, Hi = 64.f;
	for (int32 I = 0; I < 30; ++I) { const float M = 0.5f * (Lo + Hi); if (F(M) > 0.f) Lo = M; else Hi = M; }
	return 0.5f * (Lo + Hi);
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

float DynamicLensMath::ProjectionGK(float K, float Theta)
{
	if (FMath::Abs(K) < 1e-4f) return Theta;
	if (K > 0.f) return FMath::Tan(FMath::Min(K * Theta, HALF_PI - 1e-4f)) / K;
	const float A = -K;
	return FMath::Sin(FMath::Min(A * Theta, HALF_PI)) / A;
}

bool DynamicLensMath::ProjectionThetaK(float K, float ROverF, float& OutTheta)
{
	if (FMath::Abs(K) < 1e-4f) { OutTheta = ROverF; return true; }
	if (K > 0.f) { OutTheta = FMath::Atan(K * ROverF) / K; return true; }
	const float A = -K;
	if (A * ROverF > 1.f) return false;
	OutTheta = FMath::Asin(A * ROverF) / A;
	return true;
}

float UDynamicLensProfile::GetProjectionK() const
{
	if (bUseProjectionK) return ProjectionK;
	switch (Projection)
	{
	case EDynamicLensProjection::Stereographic: return 0.5f;
	case EDynamicLensProjection::Equisolid:     return -0.5f;
	case EDynamicLensProjection::Orthographic:  return -1.f;
	default:                                    return 0.f;
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
		E.ImageCircleRadiusNorm = Profile->ImageCircleMm * FMath::Max(ImageCircle.Scale, 0.1f) / SW;
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

// --- Asset Registry tags: the Preset Browser's whole data source ------------------------------
// Written when the asset is saved, read back from FAssetData without loading anything. See the
// comment on the declaration for why loading is not an option.

namespace
{
	/**
	 * How curved the lens is: the largest departure from a straight (linear) mapping, as a fraction
	 * of half the frame width. 0 = perfectly rectilinear, 0.05 = a visible bend, 0.2+ = a fisheye.
	 *
	 * Deliberately NOT the overscan the lens needs, which was the obvious choice and is wrong.
	 * Overscan measures how far the map's source samples fall outside the frame, and that depends on
	 * how whoever built the map chose to scale it. tiedtke's maps spread 1.06-2.0; Andy Davis's are
	 * pre-scaled to sit inside the gate and so report 1.0 across the board, which would draw an
	 * empty bar for 19 real lenses that visibly bend.
	 *
	 * So: fit the best pure scale through the sampled mapping, then report the worst residual left
	 * over. A global zoom is not distortion and divides out; curvature is what survives. Same
	 * measure for all three profile kinds, so the number compares across the whole catalogue.
	 */
	float DynamicLensCurvatureFromSamples(const TArray<FVector2f>& Src, const TArray<FVector2f>& Dst)
	{
		if (Src.Num() == 0 || Src.Num() != Dst.Num()) return 0.f;
		// least-squares scale S minimising |Dst - S * Src|, both centred on the optical axis
		double Num = 0.0, Den = 0.0;
		for (int32 I = 0; I < Src.Num(); ++I)
		{
			Num += (double)Src[I].X * Dst[I].X + (double)Src[I].Y * Dst[I].Y;
			Den += (double)Src[I].X * Src[I].X + (double)Src[I].Y * Src[I].Y;
		}
		if (Den <= UE_DOUBLE_SMALL_NUMBER) return 0.f;
		const float S = (float)(Num / Den);

		float Worst = 0.f;
		for (int32 I = 0; I < Src.Num(); ++I)
		{
			Worst = FMath::Max(Worst, (Dst[I] - Src[I] * S).Size());
		}
		return Worst;
	}

	float DynamicLensDistortionMagnitude(const UDynamicLensProfile& P)
	{
		constexpr int32 N = 32;
		TArray<FVector2f> Src, Dst;
		Src.Reserve(N * N);
		Dst.Reserve(N * N);

		switch (P.Type)
		{
		case EDynamicLensProfileType::STMap:
		{
#if WITH_EDITORONLY_DATA
			// the middle prime stands for the series; sampling every map would read a lot of
			// texture source for a number that barely moves between focal lengths
			if (P.STMaps.Num() == 0) return 0.f;
			const FDynamicLensSTMapEntry& Entry = P.STMaps[P.STMaps.Num() / 2];
			UTexture2D* Map = Cast<UTexture2D>(Entry.Map);
			TArray<float> UV;
			if (!Map || !UDynamicLensLibrary::ReadSTMapSamples(Map, N, N, UV)) return 0.f;

			// Peak displacement from identity, which for a measured map IS the curvature: these maps
			// carry no global zoom to divide out, so the scale fit the other branches need would only
			// add noise here.
			//
			// Two traps, both of which silently produce a number that looks fine and means nothing:
			//   ReadSTMapSamples walks rows top-down while the maps are BottomLeft origin, so V must
			//     be flipped or every lens reads ~1.9 (nearly a whole frame) and they all look alike.
			//   The map clamps to [0,1] where the source leaves frame; those pinned samples are not
			//     measurements and must be dropped, or they swamp the peak.
			constexpr float Eps = 1e-4f;
			float Worst = 0.f;
			for (int32 J = 0; J < N; ++J)
			{
				for (int32 I = 0; I < N; ++I)
				{
					const int32 K = 2 * (J * N + I);
					if (!UV.IsValidIndex(K + 1)) continue;
					const float SU = UV[K], SV = UV[K + 1];
					if (SU <= Eps || SU >= 1.f - Eps || SV <= Eps || SV >= 1.f - Eps) continue;
					const float U = (I + 0.5f) / N;
					const float V = 1.f - (J + 0.5f) / N;
					Worst = FMath::Max(Worst, FVector2f(SU - U, SV - V).Size());
				}
			}
			return Worst * 2.f;   // UV units -> half-frame units, matching the other branches
#else
			return 0.f;
#endif
		}
		case EDynamicLensProfileType::Parametric:
		{
			if (P.Rows.Num() == 0) return 0.f;
			const float W = FMath::Max(P.NativeSensorMm.X, 1.f);
			const float H = FMath::Max(P.NativeSensorMm.Y, 1.f);
			float Worst = 0.f;
			// every measured focal, not just the middle: a zoom usually bends most at its wide end
			for (const FDynamicLensProfileRow& Row : P.Rows)
			{
				if (Row.ByFocus.Num() == 0) continue;
				const float Fx = FMath::Max(Row.FocalMm / W, KINDA_SMALL_NUMBER);
				const float Fy = FMath::Max(Row.FocalMm / H, KINDA_SMALL_NUMBER);
				const float HalfX = 0.5f / Fx, HalfY = 0.5f / Fy;
				const float CornerR = FMath::Sqrt(HalfX * HalfX + HalfY * HalfY);
				// same guard ComputeOverscan uses: without it a large K3 runs away past the corner and
				// reports a bend no real lens has (Zeiss Supreme measured 0.89 against 0.05 with it)
				const FDynamicLensParams Params =
					DynamicLensMath::MakeMonotonic(Row.ByFocus.Last(), CornerR);   // last focus entry stands for infinity

				Src.Reset(); Dst.Reset();
				for (int32 J = 0; J < N; ++J)
				{
					for (int32 I = 0; I < N; ++I)
					{
						// view-space position on the frame, then where the lens bends it to
						const float X = (((I + 0.5f) / N) - 0.5f) * 2.f * HalfX;
						const float Y = (((J + 0.5f) / N) - 0.5f) * 2.f * HalfY;
						const float R = FMath::Sqrt(X * X + Y * Y);
						if (R <= KINDA_SMALL_NUMBER) continue;
						const float Scale = DynamicLensMath::RadialForward(R, Params) / R;
						// back to half-frame units so the result compares with the ST-map branch
						Src.Add(FVector2f(X / HalfX, Y / HalfY));
						Dst.Add(FVector2f(X * Scale / HalfX, Y * Scale / HalfY));
					}
				}
				Worst = FMath::Max(Worst, DynamicLensCurvatureFromSamples(Src, Dst));
			}
			return Worst;
		}
		case EDynamicLensProfileType::Projection:
		{
			// an ideal fisheye: compare its r = f * g(theta) against the rectilinear r = f * tan(theta).
			// Capped at 75 degrees because tan runs away towards 90 and would swamp the fit with one
			// sample; a fisheye pegs the bar either way, which is the honest answer for it.
			const float MaxTheta = FMath::DegreesToRadians(FMath::Clamp(P.MaxFieldAngleDeg, 1.f, 75.f));
			for (int32 I = 1; I <= N; ++I)
			{
				const float Theta = MaxTheta * I / N;
				Src.Add(FVector2f(FMath::Tan(Theta), 0.f));
				Dst.Add(FVector2f(DynamicLensMath::ProjectionG(P.Projection, Theta), 0.f));
			}
			return DynamicLensCurvatureFromSamples(Src, Dst);
		}
		default:
			return 0.f;
		}
	}

	/** Who measured the data, from the documented DL_<x>_ name prefix (see Tools/data/presets.json "prefixes"). */
	FString DynamicLensFamilyFromName(const FString& AssetName)
	{
		if (AssetName.StartsWith(TEXT("DL_AD_"))) return TEXT("AndyDavis");
		if (AssetName.StartsWith(TEXT("DL_T_")))  return TEXT("Tiedtke");
		if (AssetName.StartsWith(TEXT("DL_L_")))  return TEXT("Lanthimos");
		if (AssetName.StartsWith(TEXT("DL_C_")))  return TEXT("Custom");
		return TEXT("Custom");
	}
}

void UDynamicLensPreset::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	using FTag = UObject::FAssetRegistryTag;
	Context.AddTag(FTag(DynamicLensTags::Description, Description, FTag::TT_Hidden));
	Context.AddTag(FTag(DynamicLensTags::Family, DynamicLensFamilyFromName(GetName()), FTag::TT_Alphabetical));

	const UDynamicLensProfile* P = Distortion.Profile;
	if (!P)
	{
		// still tagged, so the browser can show and filter presets that carry no lens yet
		Context.AddTag(FTag(DynamicLensTags::Label, GetName(), FTag::TT_Alphabetical));
		Context.AddTag(FTag(DynamicLensTags::Type, TEXT("None"), FTag::TT_Alphabetical));
		return;
	}

	float MinMm = 0.f, MaxMm = 0.f;
	P->GetFocalRange(MinMm, MaxMm);
	// a prime reports its nominal focal; GetFocalRange only covers grids and map sets
	if (MaxMm <= 0.f && P->NominalFocalMm > 0.f) { MinMm = MaxMm = P->NominalFocalMm; }

	const int32 MapCount = (P->Type == EDynamicLensProfileType::STMap) ? P->STMaps.Num() : P->Rows.Num();
	const bool bBreathes = P->Type == EDynamicLensProfileType::Parametric && P->FocusCm.Num() > 1;

	const UEnum* TypeEnum = StaticEnum<EDynamicLensProfileType>();
	const FString TypeName = TypeEnum ? TypeEnum->GetNameStringByValue((int64)P->Type) : TEXT("Unknown");

	Context.AddTag(FTag(DynamicLensTags::Label, P->Label.IsEmpty() ? GetName() : P->Label, FTag::TT_Alphabetical));
	Context.AddTag(FTag(DynamicLensTags::Source, P->Source, FTag::TT_Hidden));
	Context.AddTag(FTag(DynamicLensTags::ProfilePath, P->GetPathName(), FTag::TT_Hidden));
	Context.AddTag(FTag(DynamicLensTags::Type, TypeName, FTag::TT_Alphabetical));
	Context.AddTag(FTag(DynamicLensTags::Squeeze, FString::SanitizeFloat(P->Squeeze), FTag::TT_Numerical));
	Context.AddTag(FTag(DynamicLensTags::FocalMin, FString::SanitizeFloat(MinMm), FTag::TT_Numerical));
	Context.AddTag(FTag(DynamicLensTags::FocalMax, FString::SanitizeFloat(MaxMm), FTag::TT_Numerical));
	Context.AddTag(FTag(DynamicLensTags::ImageCircleMm, FString::SanitizeFloat(P->EffectiveImageCircleMm()), FTag::TT_Numerical));
	Context.AddTag(FTag(DynamicLensTags::MaxAperture, FString::SanitizeFloat(P->MaxAperture), FTag::TT_Numerical));
	Context.AddTag(FTag(DynamicLensTags::SensorMm, FString::Printf(TEXT("%.2fx%.2f"), P->NativeSensorMm.X, P->NativeSensorMm.Y), FTag::TT_Dimensional));
	Context.AddTag(FTag(DynamicLensTags::MapCount, FString::FromInt(MapCount), FTag::TT_Numerical));
	Context.AddTag(FTag(DynamicLensTags::Breathes, bBreathes ? TEXT("1") : TEXT("0"), FTag::TT_Numerical));
	Context.AddTag(FTag(DynamicLensTags::Distortion, FString::SanitizeFloat(DynamicLensDistortionMagnitude(*P)), FTag::TT_Numerical));
}
