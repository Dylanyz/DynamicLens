#include "DynamicLensTypes.h"

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
	// Output (distorted) frame corners in normalized view space: x = (u-0.5)/Fx, y = (v-0.5)/Fy.
	// For each border point of the output frame, find the undistorted point that lands there; the overscan is how far
	// outside the frame that source point sits. Radial model => direction is preserved, only the radius changes.
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
		const float Ux = X * S, Uy = Y * S;
		Over = FMath::Max(Over, FMath::Abs(Ux) / HalfX);
		Over = FMath::Max(Over, FMath::Abs(Uy) / HalfY);
	};
	for (int32 I = 0; I <= N; ++I)
	{
		const float T = -1.f + 2.f * I / N;
		Test(T * HalfX, -HalfY); Test(T * HalfX, HalfY);
		Test(-HalfX, T * HalfY); Test(HalfX, T * HalfY);
	}
	return FMath::Clamp(Over, 1.f, 2.f);
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
	if (FocusCm.Num() < 1 || Rows.Num() < 1) return false;
	for (const FDynamicLensProfileRow& Row : Rows)
	{
		if (Row.ByFocus.Num() != FocusCm.Num()) return false;
	}
	return true;
}

void UDynamicLensProfile::GetFocalRange(float& MinMm, float& MaxMm) const
{
	MinMm = Rows.Num() ? Rows[0].FocalMm : 0.f;
	MaxMm = Rows.Num() ? Rows.Last().FocalMm : 0.f;
}

FDynamicLensParams UDynamicLensProfile::Evaluate(float FocalMm, float InFocusCm) const
{
	FDynamicLensParams Out;
	if (!IsValidProfile()) return Out;

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

FDynamicLensEval UDynamicLensPreset::Evaluate(float FocalMm, float FocusCm, float FStop, float SensorWmm, float SensorHmm, float AmountMultiplier) const
{
	FDynamicLensEval E;
	const float Focal = FMath::Max(FocalMm, 0.1f);
	const float Focus = FMath::Max(FocusCm, 1.f);
	const float Stop = FMath::Max(FStop, 0.7f);
	const float SW = FMath::Max(SensorWmm, 0.1f), SH = FMath::Max(SensorHmm, 0.1f);

	// field angle at the frame corner
	const float HalfDiag = 0.5f * FMath::Sqrt(SW * SW + SH * SH);
	const float TanCorner = HalfDiag / Focal;
	const float CornerAngle = FMath::Atan(TanCorner);
	E.CornerFieldAngleDeg = FMath::RadiansToDegrees(CornerAngle);

	// --- distortion
	const bool bProfile = Profile && Profile->IsValidProfile();
	if (bProfile)
	{
		float EvalFocal = Focal;
		if (OutOfRange == EDynamicLensRangeMode::Clamp)
		{
			float MinMm, MaxMm; Profile->GetFocalRange(MinMm, MaxMm);
			EvalFocal = FMath::Clamp(Focal, MinMm, MaxMm);
		}
		const FDynamicLensParams Base = Profile->Evaluate(EvalFocal, Focus);
		const FDynamicLensParams Inf = Profile->Evaluate(EvalFocal, 1.0e6f);
		E.Params = FDynamicLensParams::Lerp(Inf, Base, Breathing);
		if (OutOfRange == EDynamicLensRangeMode::Extrapolate && bProfile)
		{
			// linear extrapolation in log(focal) from the two outermost measured lenses
			float MinMm, MaxMm; Profile->GetFocalRange(MinMm, MaxMm);
			if (Focal < MinMm && Profile->Rows.Num() >= 2)
			{
				const float F0 = Profile->Rows[0].FocalMm, F1 = Profile->Rows[1].FocalMm;
				const FDynamicLensParams P0 = FDynamicLensParams::Lerp(Profile->Evaluate(F0, 1e6f), Profile->Evaluate(F0, Focus), Breathing);
				const FDynamicLensParams P1 = FDynamicLensParams::Lerp(Profile->Evaluate(F1, 1e6f), Profile->Evaluate(F1, Focus), Breathing);
				const float T = (FMath::Loge(Focal) - FMath::Loge(F0)) / FMath::Max(FMath::Loge(F1) - FMath::Loge(F0), 1e-4f); // negative
				E.Params = FDynamicLensParams::Lerp(P0, P1, T);
			}
			else if (Focal > MaxMm && Profile->Rows.Num() >= 2)
			{
				const int32 N = Profile->Rows.Num();
				const float F0 = Profile->Rows[N - 2].FocalMm, F1 = Profile->Rows[N - 1].FocalMm;
				const FDynamicLensParams P0 = FDynamicLensParams::Lerp(Profile->Evaluate(F0, 1e6f), Profile->Evaluate(F0, Focus), Breathing);
				const FDynamicLensParams P1 = FDynamicLensParams::Lerp(Profile->Evaluate(F1, 1e6f), Profile->Evaluate(F1, Focus), Breathing);
				const float T = (FMath::Loge(Focal) - FMath::Loge(F0)) / FMath::Max(FMath::Loge(F1) - FMath::Loge(F0), 1e-4f); // > 1
				E.Params = FDynamicLensParams::Lerp(P0, P1, T);
			}
		}
	}
	const float TotalAmount = Amount * FMath::Max(AmountMultiplier, 0.f);
	E.Params.K1 *= TotalAmount; E.Params.K2 *= TotalAmount; E.Params.K3 *= TotalAmount;
	E.Params.P1 *= TotalAmount; E.Params.P2 *= TotalAmount;

	if (WideBoost.bEnabled)
	{
		const float Span = FMath::Max(WideBoost.BelowMm - WideBoost.FullMm, 0.001f);
		const float T = Smooth01((WideBoost.BelowMm - Focal) / Span);
		E.Params.K1 += WideBoost.K1 * T;
		E.Params.K2 += WideBoost.K2 * T;
	}
	// never let the mapping fold over inside the frame (a real lens can't)
	{
		const float CornerR = FMath::Sqrt(FMath::Square(0.5f * SW / Focal) + FMath::Square(0.5f * SH / Focal));
		E.Params = DynamicLensMath::MakeMonotonic(E.Params, CornerR * 1.5f);
	}

	// --- physical pupil geometry (shared by vignette + bokeh)
	const float PupilDiameter = Focal / Stop;                                   // entrance pupil, mm
	const float FrontRadius = bProfile ? 0.5f * Profile->FrontDiameterMm : 57.f;
	const float BarrelLen = bProfile ? Profile->BarrelLengthMm : 140.f;
	const float CatsEye = FMath::Max(Bokeh.CatsEyeStrength, 0.f);
	// strength > 1 narrows the effective opening, < 1 widens it
	const float EffRadius = (CatsEye > KINDA_SMALL_NUMBER) ? FrontRadius / CatsEye : 1e6f;
	const float PupilOffsetAtCorner = BarrelLen * TanCorner;                     // where the corner bundle crosses the barrel opening
	E.CornerPupilVisible = DynamicLensMath::DiscOverlapFraction(0.5f * PupilDiameter, EffRadius, PupilOffsetAtCorner);

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
			E.Blades = bProfile ? Profile->IrisBlades : 9;
			E.BarrelRadiusMm = (CatsEye > KINDA_SMALL_NUMBER) ? EffRadius : 0.f;
			E.BarrelLengthMm = (CatsEye > KINDA_SMALL_NUMBER) ? BarrelLen : 0.f;
		}
		else
		{
			E.Blades = Bokeh.Blades;
			E.BarrelRadiusMm = Bokeh.BarrelRadiusMm;
			E.BarrelLengthMm = Bokeh.BarrelLengthMm;
		}
		const float OpenStop = bProfile ? Profile->MaxAperture : 1.4f;
		const float Fade = 1.f - Smooth01((Stop - OpenStop) / FMath::Max(Bokeh.SwirlFadesByFStop - OpenStop, 0.001f));
		E.Petzval = Bokeh.Petzval * Fade;
		E.PetzvalFalloff = Bokeh.PetzvalFalloff;
		E.SwirlExclusionBox = Bokeh.SwirlExclusionBox;
		E.SwirlExclusionRadius = Bokeh.SwirlExclusionRadius;
	}
	return E;
}
