// DynamicLens — data types: measured lens profiles and look presets.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DynamicLensTypes.generated.h"

/** Spherical (Brown-Conrady) distortion coefficients in Unreal's normalized convention (same as a Lens File). */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensParams
{
	GENERATED_BODY()

	/** Radial term 1. Negative = barrel (wide lenses), positive = pincushion (long lenses). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (UIMin = "-0.5", UIMax = "0.5"))
	float K1 = 0.f;

	/** Radial term 2. Shapes how the distortion ramps toward the corners (mustache look when it opposes K1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (UIMin = "-0.5", UIMax = "0.5"))
	float K2 = 0.f;

	/** Radial term 3. Only matters in the extreme corners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (UIMin = "-0.5", UIMax = "0.5"))
	float K3 = 0.f;

	/** Tangential term 1 (decentering). Leave at 0 for a clean lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (UIMin = "-0.05", UIMax = "0.05"))
	float P1 = 0.f;

	/** Tangential term 2 (decentering). Leave at 0 for a clean lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (UIMin = "-0.05", UIMax = "0.05"))
	float P2 = 0.f;

	TArray<float> ToArray() const { return { K1, K2, K3, P1, P2 }; }
	static FDynamicLensParams Lerp(const FDynamicLensParams& A, const FDynamicLensParams& B, float T);
};

/** One measured focal length of a lens series: distortion sampled along the profile's focus grid. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensProfileRow
{
	GENERATED_BODY()

	/** Focal length this row was measured at, in mm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile", meta = (ClampMin = "1.0", ClampMax = "2000.0"))
	float FocalMm = 50.f;

	/** One entry per focus distance in the profile's Focus Cm list (same order). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	TArray<FDynamicLensParams> ByFocus;
};

/**
 * A lens series measured across focal lengths and focus distances (e.g. ARRI/Zeiss Master Primes), plus the
 * physical dimensions that drive the bokeh and vignette physics.
 * Distortion evaluation is bilinear in log(focal) x log(focus), so any focal length and focus distance is smooth.
 */
UCLASS(BlueprintType)
class DYNAMICLENS_API UDynamicLensProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Human readable name of the lens series. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	FString Label;

	/** Where the data came from (who measured it, which camera body, notes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile", meta = (MultiLine = "true"))
	FString Source;

	/** Focus distances of the grid, in Unreal cm, ascending. The last entry stands for infinity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Distortion Data")
	TArray<float> FocusCm;

	/** One row per measured focal length, ascending by FocalMm. Each row holds FocusCm.Num() entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Distortion Data")
	TArray<FDynamicLensProfileRow> Rows;

	// --- physical specs (from the manufacturer's data sheet) ------------------------------------------
	/** Diameter of the front of the lens barrel in mm (data sheet "front diameter"; 114 for ARRI Master Primes, 95 for Zeiss Supreme). Sets where cat's-eye clipping starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Physical", meta = (ClampMin = "10.0", ClampMax = "300.0", UIMin = "50.0", UIMax = "160.0"))
	float FrontDiameterMm = 114.f;

	/** Length of the lens barrel in front of the iris, in mm (roughly the lens length from the mount). Longer barrels clip more. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Physical", meta = (ClampMin = "10.0", ClampMax = "400.0", UIMin = "60.0", UIMax = "250.0"))
	float BarrelLengthMm = 140.f;

	/** Number of iris blades (data sheet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Physical", meta = (ClampMin = "4", ClampMax = "16"))
	int32 IrisBlades = 9;

	/** Widest aperture (T-stop) of the series, e.g. 1.3 for Master Primes. Used to normalise "wide open". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Physical", meta = (ClampMin = "0.7", ClampMax = "22.0"))
	float MaxAperture = 1.3f;

	/** Distortion at any focal length (mm) and focus distance (cm). Clamps outside the measured range. */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	FDynamicLensParams Evaluate(float FocalMm, float InFocusCm) const;

	/** Shortest and longest measured focal length. */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	void GetFocalRange(float& MinMm, float& MaxMm) const;

	bool IsValidProfile() const;
};

/** What happens when the camera's focal length leaves the measured range of the profile. */
UENUM(BlueprintType)
enum class EDynamicLensRangeMode : uint8
{
	/** Hold the distortion shape of the nearest measured lens (12 mm keeps its 12 mm coefficients at 8 mm). Safe, no surprises. */
	Clamp UMETA(DisplayName = "Clamp to measured range"),
	/** Let the coefficients extrapolate beyond the measured range. Wilder wide end; coefficients are still limited so the image never folds over. */
	Extrapolate UMETA(DisplayName = "Extrapolate"),
};

/** Extra barrel distortion blended in below a focal length, for a deliberate fisheye feel at the wide end. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensWideBoost
{
	GENERATED_BODY()

	/** Turn the wide-end boost on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wide Boost")
	bool bEnabled = false;

	/** The boost starts fading in below this focal length (mm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wide Boost", meta = (EditCondition = "bEnabled", ClampMin = "4.0", ClampMax = "200.0", UIMin = "8.0", UIMax = "50.0"))
	float BelowMm = 24.f;

	/** The boost reaches full strength at this focal length (mm) and below. Must be shorter than Below Mm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wide Boost", meta = (EditCondition = "bEnabled", ClampMin = "1.0", ClampMax = "200.0", UIMin = "6.0", UIMax = "30.0"))
	float FullMm = 10.f;

	/** K1 added at full strength. Negative pushes toward barrel / fisheye. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wide Boost", meta = (EditCondition = "bEnabled", ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-0.4", UIMax = "0.1"))
	float K1 = -0.12f;

	/** K2 added at full strength. A small positive value keeps the extreme corners from over-bending. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wide Boost", meta = (EditCondition = "bEnabled", ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-0.1", UIMax = "0.1"))
	float K2 = 0.02f;
};

/** How a layer gets its numbers. */
UENUM(BlueprintType)
enum class EDynamicLensLayerMode : uint8
{
	/** Derived from the lens profile's physical specs and the camera state (focal length, f-stop, sensor). */
	Physical UMETA(DisplayName = "Physical (from lens specs)"),
	/** Type the values yourself. */
	Manual UMETA(DisplayName = "Manual"),
};

/** Vignette: natural cos^4 light falloff plus mechanical clipping by the barrel, or hand-set curves. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensVignette
{
	GENERATED_BODY()

	/** Drive the camera's vignette intensity from the lens. Off leaves the camera's own vignette untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette")
	bool bEnabled = true;

	/** Physical: cos^4 falloff at the frame corner (field angle from focal length + sensor) plus barrel clipping that fades as you stop down. Manual: the curves below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette", meta = (EditCondition = "bEnabled"))
	EDynamicLensLayerMode Mode = EDynamicLensLayerMode::Physical;

	/** Physical: how much of the textbook cos^4 falloff to apply. 1 = a simple lens; modern retrofocus wide angles correct much of it, so 0.3–0.6 is realistic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Physical", ClampMin = "0.0", ClampMax = "1.0"))
	float NaturalFalloff = 0.5f;

	/** Physical: scales the mechanical (barrel clipping) part. 1 = as computed from the lens specs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Physical", ClampMin = "0.0", ClampMax = "3.0"))
	float MechanicalStrength = 1.f;

	/** Manual: vignette intensity at the wide end (Wide Mm and below), wide open. 0 = none, 1 = heavy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette|Manual", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "0.0", ClampMax = "1.0"))
	float AtWide = 0.35f;

	/** Manual: vignette intensity at the long end (Long Mm and above), wide open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette|Manual", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "0.0", ClampMax = "1.0"))
	float AtLong = 0.1f;

	/** Manual: focal length (mm) treated as "wide" for the blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette|Manual", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "1.0", ClampMax = "500.0", UIMin = "8.0", UIMax = "50.0"))
	float WideMm = 14.f;

	/** Manual: focal length (mm) treated as "long" for the blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette|Manual", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "1.0", ClampMax = "2000.0", UIMin = "50.0", UIMax = "300.0"))
	float LongMm = 100.f;

	/** Manual: f-stop considered wide open (full vignette). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette|Manual", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "0.7", ClampMax = "64.0", UIMin = "1.0", UIMax = "4.0"))
	float FStopOpen = 1.4f;

	/** Manual: f-stop at which the stop-down fade is complete. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette|Manual", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "0.7", ClampMax = "64.0", UIMin = "2.8", UIMax = "16.0"))
	float FStopClosed = 5.6f;

	/** Manual: how much of the vignette disappears when stopped down to F Stop Closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette|Manual", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "0.0", ClampMax = "1.0"))
	float StopDownFade = 0.7f;
};

/** Bokeh character through Unreal 5.8's depth-of-field controls: iris blades, cat's-eye barrel clipping, Petzval swirl. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensBokeh
{
	GENERATED_BODY()

	/** Drive the camera's depth-of-field bokeh settings from the lens. Off leaves the camera's own settings untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh")
	bool bEnabled = true;

	/** Physical: blades, barrel radius and length come from the profile's data-sheet specs, so cat's eye appears exactly where the entrance pupil (focal / f-stop) starts hitting the barrel. Manual: the values below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh", meta = (EditCondition = "bEnabled"))
	EDynamicLensLayerMode Mode = EDynamicLensLayerMode::Physical;

	/** Physical: 1 = the real lens. Above 1 narrows the barrel (more cat's eye), below 1 widens it (less). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Cat's Eye", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Physical", ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "2.0"))
	float CatsEyeStrength = 1.f;

	/** Manual: number of iris blades. Shapes out-of-focus highlights (9 = ARRI Master Prime, 11–16 = rounder). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Iris", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "4", ClampMax = "16"))
	int32 Blades = 9;

	/** Manual: radius of the lens barrel in mm. Smaller than the aperture cone = bokeh clipped into cat's eyes toward the edges. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Cat's Eye", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "0.0", ClampMax = "200.0", UIMin = "0.0", UIMax = "80.0"))
	float BarrelRadiusMm = 40.f;

	/** Manual: length of the lens barrel in mm. Longer barrels clip more at the edges. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Cat's Eye", meta = (EditCondition = "bEnabled && Mode == EDynamicLensLayerMode::Manual", ClampMin = "0.0", ClampMax = "400.0", UIMin = "0.0", UIMax = "200.0"))
	float BarrelLengthMm = 120.f;

	/** Petzval swirl: stretches bokeh tangentially toward the edges (vintage Petzval / Helios look). Real modern cinema primes: 0. Positive = sagittal swirl, negative = tangential. 1–2 is already a strong vintage look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Swirl", meta = (EditCondition = "bEnabled", ClampMin = "-10.0", ClampMax = "10.0", UIMin = "-3.0", UIMax = "3.0"))
	float Petzval = 0.f;

	/** How fast the swirl grows toward the edge of frame (1 = linear from the centre, higher = only the outer part swirls). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Swirl", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.5", UIMax = "4.0"))
	float PetzvalFalloff = 1.f;

	/** Half-size of a centre box (0–1 of the frame) that is protected from the swirl. 0 = swirl grows smoothly from the centre (no visible untouched circle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Swirl", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	FVector2D SwirlExclusionBox = FVector2D::ZeroVector;

	/** Corner rounding of the protected centre box (0–1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Swirl", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float SwirlExclusionRadius = 0.f;

	/** The swirl fades as the iris closes (real Petzval swirl is an aperture effect). F-stop at which it is fully faded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Swirl", meta = (EditCondition = "bEnabled", ClampMin = "0.7", ClampMax = "64.0", UIMin = "2.0", UIMax = "16.0"))
	float SwirlFadesByFStop = 5.6f;
};

/** Everything a preset resolved for one frame. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensEval
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") FDynamicLensParams Params;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") bool bVignette = false;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float VignetteIntensity = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") bool bBokeh = false;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") int32 Blades = 9;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float BarrelRadiusMm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float BarrelLengthMm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float Petzval = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float PetzvalFalloff = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") FVector2D SwirlExclusionBox = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float SwirlExclusionRadius = 0.f;
	/** Field angle at the frame corner, degrees (for the debug readout). */
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float CornerFieldAngleDeg = 0.f;
	/** Fraction of the entrance pupil still visible at the frame corner (1 = no cat's eye). */
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float CornerPupilVisible = 1.f;
};

/**
 * A look: a measured lens profile plus creative layers (amount, breathing, wide-end boost, vignette, bokeh).
 * Save presets as assets and pick them on a Dynamic Lens component.
 */
UCLASS(BlueprintType)
class DYNAMICLENS_API UDynamicLensPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Short description shown to whoever picks this preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset", meta = (MultiLine = "true"))
	FString Description;

	/** Measured lens series that drives the distortion shape across focal length and focus, and the physical specs for bokeh/vignette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	TObjectPtr<UDynamicLensProfile> Profile;

	/** Multiplier on all distortion coefficients. 1 = the measured lens, 0 = straight lines, 2 = twice the bend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "3.0"))
	float Amount = 1.f;

	/** Multiplier on the focus-dependent part of the distortion (lens breathing). 0 = no change with focus, 1 = measured, 2 = exaggerated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "2.0"))
	float Breathing = 1.f;

	/** What to do when the camera's focal length is outside the profile's measured range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	EDynamicLensRangeMode OutOfRange = EDynamicLensRangeMode::Clamp;

	/** Extra barrel below a chosen focal length, for a fisheye feel at the wide end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ShowOnlyInnerProperties))
	FDynamicLensWideBoost WideBoost;

	/** Vignette that follows focal length and aperture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette", meta = (ShowOnlyInnerProperties))
	FDynamicLensVignette Vignette;

	/** Bokeh character (iris blades, cat's eye, swirl). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh", meta = (ShowOnlyInnerProperties))
	FDynamicLensBokeh Bokeh;

	/**
	 * Resolve the preset for a camera state.
	 * SensorWmm/SensorHmm: effective sensor (after squeeze and crop). AmountMultiplier scales Amount (per-component control).
	 */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	FDynamicLensEval Evaluate(float FocalMm, float FocusCm, float FStop, float SensorWmm, float SensorHmm, float AmountMultiplier = 1.f) const;
};

namespace DynamicLensMath
{
	/** Fraction of a disc of radius A, centred D away from a disc of radius B (the barrel opening), that lies inside B. */
	DYNAMICLENS_API float DiscOverlapFraction(float A, float B, float D);

	/** Radial forward distortion r_d = r_u (1 + K1 r^2 + K2 r^4 + K3 r^6). */
	DYNAMICLENS_API float RadialForward(float R, const FDynamicLensParams& P);

	/** Scale the radial coefficients down until the mapping stays monotonic out to MaxRadius (a real lens never folds the image). */
	DYNAMICLENS_API FDynamicLensParams MakeMonotonic(const FDynamicLensParams& P, float MaxRadius);

	/** Undistorted radius for a distorted radius (bisection; the mapping must be monotonic). */
	DYNAMICLENS_API float RadialInverse(float Rd, const FDynamicLensParams& P, float MaxRadius);

	/** Exact overscan factor: how much wider the render must be so every distorted output pixel samples inside the frame. */
	DYNAMICLENS_API float ComputeOverscan(const FDynamicLensParams& P, float Fx, float Fy, int32 SamplesPerEdge = 32);
}
