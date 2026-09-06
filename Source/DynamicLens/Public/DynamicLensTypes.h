// DynamicLens — data types: lens profiles (parametric, ST map, projection) and look presets.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LensData.h"
#include "DynamicLensTypes.generated.h"

class UTexture;

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

/** One ST map (lens grid solved to a UV map) at a fixed focal length. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensSTMapEntry
{
	GENERATED_BODY()

	/** Focal length printed on the lens, in mm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ST Map", meta = (ClampMin = "1.0", ClampMax = "2000.0"))
	float FocalMm = 50.f;

	/** Focus distance the map was shot at, in cm. 0 = unknown / single focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ST Map", meta = (ClampMin = "0.0"))
	float FocusCm = 0.f;

	/** The ST map texture (32-bit float, linear, no mips). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ST Map")
	TObjectPtr<UTexture> Map;

	/** How the texture encodes the map (channels, pixel origin). Copied from the Lens File it came from. */
	UPROPERTY(EditAnywhere, Category = "ST Map")
	FCalibratedMapFormat MapFormat;

	/** Overscan this map needs so its whole frame has source pixels (measured from the map's border when imported). 1 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ST Map", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float NeededOverscan = 1.f;
};

/** How a profile describes its distortion. */
UENUM(BlueprintType)
enum class EDynamicLensProfileType : uint8
{
	/** Measured K coefficients on a focal x focus grid. Any focal length, any focus. */
	Parametric UMETA(DisplayName = "Parametric (K values, zoomable)"),
	/** Measured ST maps, one per focal length (prime lenses). Exact pixel match of the real lens grid at those focal lengths. */
	STMap UMETA(DisplayName = "ST Map (measured primes)"),
	/** Ideal fisheye projection (equidistant, stereographic ...). Any focal length. */
	Projection UMETA(DisplayName = "Projection (fisheye maths)"),
};

/** Ideal wide-angle projections, r = f * g(theta). Rectilinear is g = tan(theta) (no distortion). */
UENUM(BlueprintType)
enum class EDynamicLensProjection : uint8
{
	/** r = f * theta. Most fisheyes (Nikkor 6mm/8mm, Canon 8-15, Optex 4mm): equal angular spacing. */
	Equidistant UMETA(DisplayName = "Equidistant (most fisheyes)"),
	/** r = 2f * tan(theta/2). Samyang 8mm: straighter lines, less squeeze at the edge. */
	Stereographic UMETA(DisplayName = "Stereographic"),
	/** r = 2f * sin(theta/2). Sigma 8mm/15mm, Nikkor 10.5mm: more edge squeeze. */
	Equisolid UMETA(DisplayName = "Equisolid angle"),
	/** r = f * sin(theta). Nikkor OP 10mm: extreme edge squeeze, 180 degrees max. */
	Orthographic UMETA(DisplayName = "Orthographic"),
};

/**
 * A lens series. Holds the distortion data (measured K grid, measured ST maps, or an ideal projection), the sensor
 * it was measured on, and the physical dimensions that drive the bokeh, vignette and image-circle physics.
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

	/** What kind of distortion data this profile holds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	EDynamicLensProfileType Type = EDynamicLensProfileType::Parametric;

	/** Read-only summary: focal lengths covered (from the data), sensor, squeeze. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Profile")
	FString Coverage;

	// --- native format ---------------------------------------------------------------------------
	/** Sensor the data was captured on, in mm (desqueezed width for anamorphic). The distortion is exact for this sensor; other sensors use the component's Sensor Fit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Native Format", meta = (ClampMin = "1.0", ClampMax = "200.0"))
	FVector2D NativeSensorMm = FVector2D(23.76, 13.365);

	/** Anamorphic squeeze the maps were shot with (1 = spherical). Sets the camera's squeeze in Match Camera To Profile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Native Format", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float Squeeze = 1.f;

	/** Diameter of the image circle the lens projects, in mm, at the sensor. Beyond it the image goes black (hard mechanical vignette). 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Native Format", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float ImageCircleMm = 0.f;

	// --- parametric data --------------------------------------------------------------------------
	/** Focus distances of the grid, in Unreal cm, ascending. The last entry stands for infinity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Parametric Data", meta = (EditCondition = "Type == EDynamicLensProfileType::Parametric"))
	TArray<float> FocusCm;

	/** One row per measured focal length, ascending by FocalMm. Each row holds FocusCm.Num() entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Parametric Data", meta = (EditCondition = "Type == EDynamicLensProfileType::Parametric"))
	TArray<FDynamicLensProfileRow> Rows;

	// --- ST map data ------------------------------------------------------------------------------
	/** Measured maps, one per focal length. The component uses the entry whose focal length is nearest the camera's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|ST Map Data", meta = (EditCondition = "Type == EDynamicLensProfileType::STMap"))
	TArray<FDynamicLensSTMapEntry> STMaps;

	// --- projection data --------------------------------------------------------------------------
	/** Mapping function of the ideal lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Projection", meta = (EditCondition = "Type == EDynamicLensProfileType::Projection"))
	EDynamicLensProjection Projection = EDynamicLensProjection::Equidistant;

	/** Widest field angle the lens can show, in degrees from the optical axis (110 = a 220 degree fisheye). Unreal can only render up to about 80 degrees off-axis; beyond that the image circle is black. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Projection", meta = (EditCondition = "Type == EDynamicLensProfileType::Projection", ClampMin = "10.0", ClampMax = "110.0"))
	float MaxFieldAngleDeg = 90.f;

	/** Focal length of the lens if it is a prime (mm). 0 = zoom / any focal length. With "Lock Focal Length" on the component the camera is held here; Match Camera To Profile also sets it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile", meta = (ClampMin = "0.0", ClampMax = "2000.0", UIMin = "0.0", UIMax = "300.0"))
	float NominalFocalMm = 0.f;

	// --- physical specs (from the manufacturer's data sheet) --------------------------------------
	/** Diameter of the front of the lens barrel in mm (data sheet "front diameter"; 114 for ARRI Master Primes, 95 for Zeiss Supreme). Sets where cat's-eye clipping starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Physical", meta = (ClampMin = "10.0", ClampMax = "300.0", UIMin = "50.0", UIMax = "160.0"))
	float FrontDiameterMm = 114.f;

	/** Number of iris blades (data sheet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Physical", meta = (ClampMin = "4", ClampMax = "16"))
	int32 IrisBlades = 9;

	/** Blade shape: 0 = straight blades (polygon highlights), 1 = fully rounded (circular at every stop). Data sheets say "rounded" for most modern cine primes; 0.5 is a reasonable "rounded" value. Applies to Accumulation DOF's iris texture exactly, and to Unreal's DOF by rounding up the effective blade count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Physical", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BladeCurvature = 0.f;

	/** Widest aperture (T-stop) of the series, e.g. 1.3 for Master Primes. Used to normalise "wide open". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Physical", meta = (ClampMin = "0.7", ClampMax = "22.0"))
	float MaxAperture = 1.3f;

	/** Fraction of the entrance pupil still visible at the edge of the image circle, wide open. Sets the effective barrel length so cat's eye starts where a real lens of this coverage clips. 1 = never clips. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile|Physical", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float PupilVisibleAtImageCircle = 0.5f;

	/** Distortion at any focal length (mm) and focus distance (cm). Parametric profiles only; clamps outside the measured range. */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	FDynamicLensParams Evaluate(float FocalMm, float InFocusCm) const;

	/** Focal length a locked camera should sit at: the nominal prime, or for ST-map series the measured prime nearest to FocalMm. 0 = don't lock. */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	float GetLockedFocal(float FocalMm) const;

	/** Put this profile back to the values the plugin ships (re-runs the importer for this asset from Tools/data). */
	UFUNCTION(CallInEditor, Category = "Profile")
	void ResetToShipped();

	/** Shortest and longest focal length the data covers (0,0 = any). */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	void GetFocalRange(float& MinMm, float& MaxMm) const;

	/** Index of the ST map whose focal length is closest to FocalMm (-1 if none). */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	int32 FindNearestSTMap(float FocalMm) const;

	/** Effective barrel length (mm) for a focal length: the length at which the pupil is PupilVisibleAtImageCircle visible at the image-circle edge wide open. */
	float ComputeBarrelLengthMm(float FocalMm, float BarrelRadiusMm) const;

	/** Image circle diameter to use: the profile's value, or the native sensor diagonal if unset. */
	float EffectiveImageCircleMm() const;

	bool IsValidProfile() const;
	void RefreshCoverage();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;
};

/** What happens when the camera's focal length leaves the measured range of the profile. */
UENUM(BlueprintType)
enum class EDynamicLensRangeMode : uint8
{
	/** Hold the coefficients of the nearest measured focal length and rescale them to the camera's focal length, so the picture shows that lens's distortion pattern (in millimetres on the sensor) - sane and smooth outside the range. */
	Clamp UMETA(DisplayName = "Clamp (rescaled)"),
	/** Extrapolate the trend of the two nearest measured focal lengths (can run away far outside the range). */
	Extrapolate,
	/** Hold the nearest focal length's coefficients as they are and apply them at the camera's field of view. Physically wrong outside the range but wildly stronger at wide focal lengths - kept for looks built on it (DL_C_Vintage_Raw). */
	ClampRaw UMETA(DisplayName = "Clamp (raw coefficients)"),
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

/** How the render is enlarged so the distorted frame has source pixels out to its corners. */
UENUM(BlueprintType)
enum class EDynamicLensOverscanMode : uint8
{
	/** Exactly what this frame needs, recomputed every frame (up to Max Overscan). Movie Render Queue/Graph read the camera's overscan once per shot, so for zoom pulls in renders prefer Fixed. */
	Dynamic UMETA(DisplayName = "Dynamic (per frame)"),
	/** A constant overscan for the whole shot. Safe for renders; anything the frame needs beyond it goes black at the edges (image circle). */
	Fixed UMETA(DisplayName = "Fixed"),
};

/** Where a bokeh value comes from. */
UENUM(BlueprintType)
enum class EDynamicLensValueSource : uint8
{
	/** From the lens profile (data sheet). */
	Profile,
	/** From the Cine Camera component (Lens Settings: diaphragm blade count / squeeze factor). */
	Camera,
	/** The value set here in the preset. */
	Custom
};

/**
 * Imperfections of the black image-circle edge. Real lens edges (The Favourite's 6 mm, Poor Things' 4 mm) are not a
 * perfect circle: the falloff is wide and uneven, the circle sits a little off-centre, and the boundary is soft and
 * slightly ragged. Everything here is in fractions of the circle radius / frame, so it scales with focal length.
 */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensImageCircleEdge
{
	GENERATED_BODY()

	/** Shape of the falloff across the soft band. 1 = smooth S-curve, 2+ = stays bright longer then drops fast (mechanical vignette), <1 = darkens early. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge", meta = (ClampMin = "0.25", ClampMax = "4.0", UIMin = "0.5", UIMax = "3.0"))
	float FalloffPower = 1.f;

	/** How black the edge gets. 1 = full black beyond the circle; 0.9 leaves a faint image (light leaking round a gate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 1.f;

	/** Offset of the circle centre from the frame centre, as a fraction of half the frame width (x) / height (y). A real 6 mm on a 35 mm gate sits a few percent off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Geometry", meta = (ClampMin = "-0.3", ClampMax = "0.3", UIMin = "-0.1", UIMax = "0.1"))
	FVector2D CenterOffset = FVector2D::ZeroVector;

	/** Vertical / horizontal radius ratio. 1 = round; 0.95 = slightly squashed (lens or gate not perfectly square to the sensor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Geometry", meta = (ClampMin = "0.5", ClampMax = "2.0", UIMin = "0.8", UIMax = "1.25"))
	float Ellipticity = 1.f;

	/** Low-frequency waviness of the circle radius (fraction of the radius). 0 = perfect circle; 0.02-0.05 = the edge wanders a little around the frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Geometry", meta = (ClampMin = "0.0", ClampMax = "0.3", UIMin = "0.0", UIMax = "0.1"))
	float Wobble = 0.f;

	/** How many bumps the waviness has around the circle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Geometry", meta = (ClampMin = "1", ClampMax = "12"))
	int32 WobbleLobes = 3;

	/** Rotates / re-seeds the waviness so two lenses don't look identical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Geometry", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float WobbleSeed = 0.f;

	/** Colour fringing on the rim: how much further out (+) or in (-) the RED channel's edge sits, as a fraction of the radius. Poor Things' 4 mm: red -0.03, blue +0.03 (blue ring). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Optics", meta = (ClampMin = "-0.1", ClampMax = "0.1", UIMin = "-0.05", UIMax = "0.05"))
	float ChromaticRed = 0.f;

	/** Same for the GREEN channel's edge (usually 0: green is the reference). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Optics", meta = (ClampMin = "-0.1", ClampMax = "0.1", UIMin = "-0.05", UIMax = "0.05"))
	float ChromaticGreen = 0.f;

	/** Same for the BLUE channel's edge (+ = blue reaches further out = blue rim). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Optics", meta = (ClampMin = "-0.1", ClampMax = "0.1", UIMin = "-0.05", UIMax = "0.05"))
	float ChromaticBlue = 0.f;

	/** Light scatter in the soft band: the picture smears radially and glows a little before it goes dark, instead of just dimming. 0 = plain darkening, 1 = strong optical rolloff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Optics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Scatter = 0.f;

	/** Waviness of the FALLOFF WIDTH around the circle (the soft band gets wider and narrower), as a fraction of the softness. 0 = even band. Independent of the radius waviness above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Geometry", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FalloffWobble = 0.f;

	/** How many bumps the falloff-width waviness has around the circle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Geometry", meta = (ClampMin = "1", ClampMax = "12"))
	int32 FalloffWobbleLobes = 3;

	/** Rotates / re-seeds the falloff-width waviness (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Geometry", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float FalloffWobbleSeed = 0.f;

	/** Fine breakup of the soft band (grain-like raggedness of the boundary), 0-1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Texture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeNoise = 0.f;

	/** Size of the breakup: cells around the circle (higher = finer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Texture", meta = (ClampMin = "4.0", ClampMax = "512.0", UIMin = "16.0", UIMax = "256.0"))
	float NoiseScale = 96.f;

	/** How deep the breakup reaches into the picture, as a fraction of the radius: it is full strength at the black edge and fades to nothing this far inside. 1 = everywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Texture", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float NoiseDepth = 0.3f;

	/** Blurs the breakup pattern (0 = crisp cells, 1 = soft blobs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Texture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoiseBlur = 0.f;

	/** Amount of fine detail layered on the breakup (second octave): 0 = only the large cells, 1 = full fine grain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Texture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoiseDetail = 0.5f;

	/** Optional full-frame mask (your own asset: a scan or paint of a real lens edge, dust, gate hairs). Multiplied over the picture in screen space; white = untouched. Linear, R channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Texture")
	TSoftObjectPtr<UTexture2D> MaskTexture;

	/** How strongly the mask is applied (0 = ignored). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge|Texture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaskStrength = 1.f;

	bool Equals(const FDynamicLensImageCircleEdge& O) const
	{
		return FMath::IsNearlyEqual(FalloffPower, O.FalloffPower) && FMath::IsNearlyEqual(Opacity, O.Opacity)
			&& CenterOffset.Equals(O.CenterOffset, 1e-4) && FMath::IsNearlyEqual(Ellipticity, O.Ellipticity)
			&& FMath::IsNearlyEqual(Wobble, O.Wobble) && WobbleLobes == O.WobbleLobes && FMath::IsNearlyEqual(WobbleSeed, O.WobbleSeed)
			&& FMath::IsNearlyEqual(EdgeNoise, O.EdgeNoise) && FMath::IsNearlyEqual(NoiseScale, O.NoiseScale)
			&& FMath::IsNearlyEqual(ChromaticRed, O.ChromaticRed) && FMath::IsNearlyEqual(ChromaticGreen, O.ChromaticGreen) && FMath::IsNearlyEqual(ChromaticBlue, O.ChromaticBlue)
			&& FMath::IsNearlyEqual(FalloffWobble, O.FalloffWobble) && FalloffWobbleLobes == O.FalloffWobbleLobes && FMath::IsNearlyEqual(FalloffWobbleSeed, O.FalloffWobbleSeed)
			&& FMath::IsNearlyEqual(NoiseDepth, O.NoiseDepth) && FMath::IsNearlyEqual(NoiseBlur, O.NoiseBlur) && FMath::IsNearlyEqual(NoiseDetail, O.NoiseDetail)
			&& FMath::IsNearlyEqual(Scatter, O.Scatter)
			&& MaskTexture == O.MaskTexture && FMath::IsNearlyEqual(MaskStrength, O.MaskStrength);
	}
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

	/** Where the blade count comes from: the lens profile (data sheet), the Cine Camera's Lens Settings, or the custom value below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Iris", meta = (EditCondition = "bEnabled"))
	EDynamicLensValueSource BladeSource = EDynamicLensValueSource::Profile;

	/** Custom number of iris blades. Shapes out-of-focus highlights (9 = ARRI Master Prime, 11-16 = rounder). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Iris", meta = (EditCondition = "bEnabled && BladeSource == EDynamicLensValueSource::Custom", ClampMin = "4", ClampMax = "16"))
	int32 Blades = 9;

	/** Override the profile's blade shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Iris", meta = (EditCondition = "bEnabled", InlineEditConditionToggle))
	bool bOverrideBladeCurvature = false;

	/** Blade shape: 0 = straight blades (polygon highlights), 1 = fully rounded (circular). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Iris", meta = (EditCondition = "bOverrideBladeCurvature", ClampMin = "0.0", ClampMax = "1.0"))
	float BladeCurvature = 0.f;

	/** Anamorphic bokeh: where the squeeze of out-of-focus highlights comes from - the profile's squeeze (2 for a 2x anamorphic), the camera's Lens Settings squeeze, or the custom value below. Highlights become ovals this many times taller than wide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Iris", meta = (EditCondition = "bEnabled"))
	EDynamicLensValueSource SqueezeSource = EDynamicLensValueSource::Profile;

	/** Custom bokeh squeeze (1 = round, 2 = 2x anamorphic ovals). Unreal's DOF accepts 1-2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Iris", meta = (EditCondition = "bEnabled && SqueezeSource == EDynamicLensValueSource::Custom", ClampMin = "1.0", ClampMax = "2.0"))
	float Squeeze = 1.f;

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

	/** If the camera also has Epic's Accumulation DOF component, feed it an iris-shaped bokeh texture (blade count) and the aberrations below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Accumulation DOF", meta = (EditCondition = "bEnabled"))
	bool bDriveAccumulationDOF = true;

	/** Accumulation DOF only: spherical aberration (Zeiss bokeh paper: under-corrected = soft-edged, bright-core background blur; over-corrected = bright rim / soap-bubble). 0 = ideal lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Accumulation DOF", meta = (EditCondition = "bEnabled && bDriveAccumulationDOF", ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "30.0"))
	float SphericalAberration = 0.f;

	/** Accumulation DOF only: coma (comet-shaped highlights toward the edges, typical of fast vintage glass). 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Accumulation DOF", meta = (EditCondition = "bEnabled && bDriveAccumulationDOF", ClampMin = "0.0", ClampMax = "1.0"))
	float Coma = 0.f;

	/** Rotation of the iris polygon in degrees (data sheets rarely specify it; only matters for the highlight shape). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh|Iris", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "360.0"))
	float BladeRotationDeg = 0.f;
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
	/** Image circle radius in normalized frame units (1 = half the frame width). 0 = none. */
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float ImageCircleRadiusNorm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float ImageCircleSoftness = 0.05f;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") bool bImageCircle = false;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") bool bDriveAccumulationDOF = false;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float SphericalAberration = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float Coma = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float BladeRotationDeg = 0.f;
	/** Resolved blade shape (0 straight - 1 round). */
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float BladeCurvature = 0.f;
	/** Resolved bokeh squeeze (1 = round). */
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") float BokehSqueeze = 1.f;
	/** Image-circle edge imperfections, as set in the preset. */
	UPROPERTY(BlueprintReadOnly, Category = "Dynamic Lens") FDynamicLensImageCircleEdge Edge;
};

/** Which lens, and how much of its measured distortion to use. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensDistortion
{
	GENERATED_BODY()

	/** Lens series that drives the distortion, and whose physical specs drive bokeh/vignette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	TObjectPtr<UDynamicLensProfile> Profile;

	/** Prime lenses: hold the camera's focal length at the profile's nominal focal length every frame (ST-map series: the nearest measured prime). Off = the camera's focal length is used as-is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	bool bLockFocalLength = false;

	/** Multiplier on all distortion coefficients (parametric profiles). 1 = the measured lens, 0 = straight lines, 2 = twice the bend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "3.0"))
	float Amount = 1.f;

	/** Multiplier on the focus-dependent part of the distortion (lens breathing). 0 = no change with focus, 1 = measured, 2 = exaggerated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "2.0"))
	float Breathing = 1.f;

	/** What to do when the camera's focal length is outside the profile's measured range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	EDynamicLensRangeMode OutOfRange = EDynamicLensRangeMode::Clamp;

	/** Creative layer, not measured data: extra barrel distortion that ramps in below a chosen focal length, for a fisheye feel at the wide end of a spherical lens. Off in the measured presets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion|Wide Boost", meta = (ShowOnlyInnerProperties))
	FDynamicLensWideBoost WideBoost;
};

/** The black edge of a lens that does not cover the sensor (fisheyes, S16 glass on 35), and the pixels the overscan can't provide. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensImageCircle
{
	GENERATED_BODY()

	/** Show the lens's image circle: black beyond the circle, like a lens that doesn't cover the sensor (Poor Things 4mm). Uses the profile's Image Circle Mm and the render's overscan limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image Circle")
	bool bEnabled = true;

	/** Width of the rolloff band as a fraction of the circle radius (hard porthole 0.05; Poor Things 4 mm measured 0.25; The Favourite corners 0.45). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image Circle", meta = (EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float Softness = 0.05f;

	/** Imperfections of the black edge: falloff shape, off-centre, waviness, breakup, chromatic rim, scatter, optional mask asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image Circle", meta = (EditCondition = "bEnabled"))
	FDynamicLensImageCircleEdge Edge;
};

/** How much extra picture is rendered so the distorted frame has pixels out to its corners. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensOverscan
{
	GENERATED_BODY()

	/** Dynamic = exactly what each frame needs (capped). Fixed = a constant for the shot: use this for renders with zoom pulls (Movie Render Queue/Graph read overscan once per shot) and for fisheyes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overscan")
	EDynamicLensOverscanMode Mode = EDynamicLensOverscanMode::Dynamic;

	/** Dynamic: never overscan more than this factor (1.5 = 50% wider render). Beyond it the corners go black instead of costing render time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overscan", meta = (EditCondition = "Mode == EDynamicLensOverscanMode::Dynamic", ClampMin = "1.0", ClampMax = "2.0"))
	float MaxOverscan = 1.5f;

	/** Dynamic: round the needed overscan up to this step (0.02 = 2%) and only shrink when it drops a full step, so focus breathing and small zooms don't resize the render every frame (each resize resets temporal anti-aliasing and pops). 0 = exact every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overscan", meta = (EditCondition = "Mode == EDynamicLensOverscanMode::Dynamic", ClampMin = "0.0", ClampMax = "0.25", UIMin = "0.0", UIMax = "0.1"))
	float DynamicStep = 0.02f;

	/** Fixed: the constant overscan factor (1.2 = 20% wider render; fisheyes want 2.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overscan", meta = (EditCondition = "Mode == EDynamicLensOverscanMode::Fixed", ClampMin = "1.0", ClampMax = "2.0"))
	float FixedOverscan = 1.2f;

	/** Render the extra overscan pixels so the final frame keeps its full resolution (GPU cost grows with overscan squared). Off keeps the render cheaper but slightly softer at the edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overscan")
	bool bScaleResolutionWithOverscan = true;
};

/** Everything that defines a look. A preset asset stores one; a component can override any block for its own camera. */
USTRUCT(BlueprintType)
struct DYNAMICLENS_API FDynamicLensSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ShowOnlyInnerProperties))
	FDynamicLensDistortion Distortion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image Circle", meta = (ShowOnlyInnerProperties))
	FDynamicLensImageCircle ImageCircle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette", meta = (ShowOnlyInnerProperties))
	FDynamicLensVignette Vignette;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh", meta = (ShowOnlyInnerProperties))
	FDynamicLensBokeh Bokeh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overscan", meta = (ShowOnlyInnerProperties))
	FDynamicLensOverscan Overscan;

	/**
	 * Resolve the look for a camera state.
	 * SensorWmm/SensorHmm: effective sensor (after squeeze and crop). AmountMultiplier scales Amount (per-component control).
	 * CameraBlades / CameraSqueeze: the Cine Camera's Lens Settings, used when a bokeh source is set to Camera.
	 */
	FDynamicLensEval Evaluate(float FocalMm, float FocusCm, float FStop, float SensorWmm, float SensorHmm, float AmountMultiplier = 1.f, int32 CameraBlades = 0, float CameraSqueeze = 1.f) const;
};

/**
 * A look: a lens profile plus creative layers (amount, breathing, wide-end boost, vignette, bokeh).
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

	/** Which lens, and how much of its distortion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ShowOnlyInnerProperties))
	FDynamicLensDistortion Distortion;

	/** The black edge of a lens that doesn't cover the sensor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image Circle", meta = (ShowOnlyInnerProperties))
	FDynamicLensImageCircle ImageCircle;

	/** Vignette that follows focal length and aperture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vignette", meta = (ShowOnlyInnerProperties))
	FDynamicLensVignette Vignette;

	/** Bokeh character (iris, cat's eye, swirl, accumulation DOF). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bokeh", meta = (ShowOnlyInnerProperties))
	FDynamicLensBokeh Bokeh;

	/** How much extra picture is rendered for the distortion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overscan", meta = (ShowOnlyInnerProperties))
	FDynamicLensOverscan Overscan;

	/** Put this preset back to the values the plugin ships (re-runs the importer for this asset from Tools/data/presets.json). */
	UFUNCTION(CallInEditor, Category = "Preset")
	void ResetToShipped();

	/** All blocks as one settings value (what a component resolves against its overrides). */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	FDynamicLensSettings GetSettings() const;

	/** Replace all blocks. */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	void SetSettings(const FDynamicLensSettings& In);

	/** Resolve the preset for a camera state (see FDynamicLensSettings::Evaluate). */
	UFUNCTION(BlueprintPure, Category = "Dynamic Lens")
	FDynamicLensEval Evaluate(float FocalMm, float FocusCm, float FStop, float SensorWmm, float SensorHmm, float AmountMultiplier = 1.f, int32 CameraBlades = 0, float CameraSqueeze = 1.f) const;
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

	/**
	 * Radius (in half-frame-width units) of the largest centred circle whose distorted pixels all sample inside a
	 * render overscanned by OverscanFactor. Beyond it the picture has no source pixels (black on a real lens).
	 */
	DYNAMICLENS_API float ValidCircleRadius(const FDynamicLensParams& P, float Fx, float Fy, float OverscanFactor);

	/** Projection radius in units of focal length: g(theta) for r = f * g(theta). */
	DYNAMICLENS_API float ProjectionG(EDynamicLensProjection Projection, float ThetaRad);
	/** Inverse: theta for r/f. Returns false when r/f is outside the projection's range. */
	DYNAMICLENS_API bool ProjectionTheta(EDynamicLensProjection Projection, float ROverF, float& OutThetaRad);
}
