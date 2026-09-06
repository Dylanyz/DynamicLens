// DynamicLens — Blueprint / Python helpers.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DynamicLensTypes.h"
#include "DynamicLensLibrary.generated.h"

class UDynamicLensComponent;

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

	/** Fill a profile asset from arrays (used by the Python importer). Rows must be ascending by focal length. */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Lens")
	static bool FillProfile(UDynamicLensProfile* Profile, const FString& Label, const FString& Source, const TArray<float>& FocusCm, const TArray<float>& FocalsMm, const TArray<float>& FlatParams);
};
