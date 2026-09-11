// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

// DynamicLens — Blueprint / Python helpers.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DynamicLensTypes.h"
#include "DynamicLensLibrary.generated.h"

class UDynamicLensComponent;
class ULensFile;
class UTexture2D;

UCLASS()
class DYNAMICLENS_API UDynamicLensLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Add a Dynamic Lens component (with the given preset) to every CineCameraActor in the world.
	 * Cameras that already have one keep theirs unless bOverwriteExisting is set. Returns the number of cameras touched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens", meta = (WorldContext = "WorldContextObject"))
	static int32 AddToAllCineCameras(UObject* WorldContextObject, UDynamicLensPreset* Preset, bool bOverwriteExisting = false);

	/** Remove Dynamic Lens components from every CineCameraActor in the world. Returns the number removed. */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens", meta = (WorldContext = "WorldContextObject"))
	static int32 RemoveFromAllCineCameras(UObject* WorldContextObject);

	/** Add (or fetch) a Dynamic Lens component on one actor. */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	static UDynamicLensComponent* AddToActor(AActor* Actor, UDynamicLensPreset* Preset);

	/** Fill a parametric profile from arrays (used by the Python importer). Rows must be ascending by focal length; FlatParams = focals x focus x 5. */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	static bool FillProfile(UDynamicLensProfile* Profile, const FString& Label, const FString& Source, const TArray<float>& FocusCm, const TArray<float>& FocalsMm, const TArray<float>& FlatParams);

	/**
	 * Add one ST map entry to a profile from an Epic Lens File (copies the texture reference, map format and sensor).
	 * FocalMm: focal length printed on the lens. MapTexture overrides the Lens File's texture (use when its reference is broken).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	static bool AddSTMapFromLensFile(UDynamicLensProfile* Profile, ULensFile* LensFile, float FocalMm, UTexture* MapTexture, float Squeeze);

	/**
	 * Sample an ST map texture on a regular grid: returns Rows*Cols UV pairs (row-major, top-left origin as stored),
	 * flattened as [u0,v0,u1,v1,...]. Editor only (reads the texture's source data).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	static bool ReadSTMapSamples(UTexture2D* Map, int32 Cols, int32 Rows, TArray<float>& OutUV);

	/**
	 * Build a transient copy of an ST map that covers Extend x the map's frame (1.4 = 40% wider), extrapolating the
	 * displacement field linearly beyond the border so an overscanned render has data instead of a smeared edge.
	 * Editor only (reads the texture source); returns null elsewhere. Same channel semantics and pixel origin as the input.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens|Import")
	static UTexture2D* BuildExtendedSTMap(UTexture2D* Map, bool bBottomLeftOrigin, FVector2D DisplacementScale, float MaxExtend, int32 OutWidth, float& OutNeededOverscan, float& OutExtend);

	/** Recompute the read-only coverage string of a profile. */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	static void RefreshProfile(UDynamicLensProfile* Profile);
};
