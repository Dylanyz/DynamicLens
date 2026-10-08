// Copyright (c) 2026 Dylan Gitalis. Source-available under CPAL-1.0 with the Commons Clause; see LICENSE.
// SPDX-License-Identifier: CPAL-1.0 AND LicenseRef-Commons-Clause-1.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#pragma once

#include "CoreMinimal.h"
#include "DynamicLensTypes.h"
#include "UObject/GCObject.h"

class ULensFile;
class UTexture;
class UTexture2D;
struct FCalibratedMapFormat;

/** A fisheye (projection) map and what was derived with it. */
struct FDynamicLensProjectionMap
{
	TObjectPtr<UTexture2D> Texture;
	float FieldScale = 1.f;
	float CircleRx = 0.f;
	float CircleRy = 0.f;
};

/**
 * What every ST-map camera shares for the rest of the editor session: the extended maps (CPU-built, ~25 ms each)
 * and the lens files that render them (Epic derives their displacement on the GPU over a few frames, and each one
 * holds ten 2048 px render targets). Sequencer spawns a camera at every cut; without this, each spawn rebuilt both,
 * which was a ~24 ms hitch per camera and ~2 frames of undistorted picture at every cut. Fisheye maps are kept the
 * same way (~5 ms and the same 2 frames per cut).
 *
 * Keys carry everything the result depends on (source map + its import version + displacement scale; lens file:
 * map + sensor + FxFy + format), so a re-import or a different filmback simply makes a new entry. Least recently
 * used entries go first once a cap is reached.
 */
class FDynamicLensCache : public FGCObject
{
public:
	static FDynamicLensCache& Get();

	/** The extended map for (Map, displacement scale); built on first use. False if the map can't be read. */
	bool FindOrBuildExtendedMap(UTexture2D* Map, bool bBottomLeftOrigin, FVector2D DisplacementScale, FDynamicLensExtendedMap& Out);

	/** A lens file rendering MapToUse on LensSensor with FxFy. Shared by every camera that asks for the same thing. */
	ULensFile* FindOrCreateSTMapLensFile(UTexture* MapToUse, const FCalibratedMapFormat& Format, FVector2D LensSensor, FVector2D FxFy);

	/** Fisheye maps, keyed by the caller (everything the map depends on). */
	bool FindProjectionMap(const FString& Key, FDynamicLensProjectionMap& Out);
	void AddProjectionMap(const FString& Key, const FDynamicLensProjectionMap& Map);

	void Clear();
	FString Describe() const;

	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDynamicLensCache"); }

private:
	struct FMapEntry { FDynamicLensExtendedMap Map; uint64 LastUsed = 0; int64 Bytes = 0; };
	struct FLensFileEntry { TObjectPtr<ULensFile> LensFile; uint64 LastUsed = 0; };
	struct FProjectionEntry { FDynamicLensProjectionMap Map; uint64 LastUsed = 0; };
	TMap<FString, FMapEntry> Maps;
	TMap<FString, FProjectionEntry> Projections;
	TMap<FString, FLensFileEntry> LensFiles;
	uint64 UseCounter = 0;
	void Trim();
};
