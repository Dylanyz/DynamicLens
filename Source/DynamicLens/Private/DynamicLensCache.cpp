// Copyright (c) 2026 Dylan Gitalis. Source-available under CPAL-1.0 with the Commons Clause; see LICENSE.
// SPDX-License-Identifier: CPAL-1.0 AND LicenseRef-Commons-Clause-1.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#include "DynamicLensCache.h"

#include "DynamicLensLibrary.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "LensFile.h"
#include "Models/SphericalLensModel.h"
#include "UObject/Package.h"

static TAutoConsoleVariable<int32> CVarCacheMaxMapMB(
	TEXT("DynamicLens.Cache.MaxMapMB"), 512,
	TEXT("Memory the shared cache of extended ST maps may use before the least recently used ones are dropped (MB). A map is 3-7 MB."));
static TAutoConsoleVariable<int32> CVarCacheMaxLensFiles(
	TEXT("DynamicLens.Cache.MaxLensFiles"), 12,
	TEXT("Shared ST-map lens files kept ready (each holds ten displacement render targets at DisplacementMapResolution). Least recently used go first."));

static FAutoConsoleCommand CmdCacheClear(TEXT("DynamicLens.Cache.Clear"), TEXT("Drop every shared extended ST map and lens file (cameras rebuild what they need)."),
	FConsoleCommandDelegate::CreateLambda([]() { FDynamicLensCache::Get().Clear(); }));
static FAutoConsoleCommand CmdCacheStatus(TEXT("DynamicLens.Cache.Status"), TEXT("Log what the shared ST-map cache holds."),
	FConsoleCommandDelegate::CreateLambda([]() { UE_LOG(LogTemp, Display, TEXT("%s"), *FDynamicLensCache::Get().Describe()); }));

FDynamicLensCache& FDynamicLensCache::Get()
{
	static FDynamicLensCache Instance;
	return Instance;
}

bool FDynamicLensCache::FindOrBuildExtendedMap(UTexture2D* Map, bool bBottomLeftOrigin, FVector2D DisplacementScale, FDynamicLensExtendedMap& Out)
{
	if (!Map) return false;
	FString Version;
#if WITH_EDITORONLY_DATA
	Version = Map->Source.GetId().ToString();   // a re-import changes it
#endif
	const FString Key = FString::Printf(TEXT("%s|%s|%d|%.4f|%.4f"), *Map->GetPathName(), *Version, bBottomLeftOrigin ? 1 : 0, DisplacementScale.X, DisplacementScale.Y);
	if (FMapEntry* Found = Maps.Find(Key))
	{
		Found->LastUsed = ++UseCounter;
		Out = Found->Map;
		return Out.Texture != nullptr;
	}
	FMapEntry New;
	New.Map.Texture = UDynamicLensLibrary::BuildExtendedSTMap(Map, bBottomLeftOrigin, DisplacementScale, 2.f, 1024, New.Map.NeededOverscan, New.Map.Extend);
	New.LastUsed = ++UseCounter;   // the newest entry is never the one trimmed
	New.Bytes = New.Map.Texture ? int64(New.Map.Texture->GetSizeX()) * New.Map.Texture->GetSizeY() * 8 : 0;
	Out = New.Map;
	Maps.Add(Key, MoveTemp(New));   // a failed build is remembered too, so it isn't retried every tick
	Trim();
	return Out.Texture != nullptr;
}

ULensFile* FDynamicLensCache::FindOrCreateSTMapLensFile(UTexture* MapToUse, const FCalibratedMapFormat& Format, FVector2D LensSensor, FVector2D FxFy)
{
	if (!MapToUse) return nullptr;
	const FString Key = FString::Printf(TEXT("%s|%d|%d|%d|%.4f|%.4f|%.5f|%.5f"), *MapToUse->GetPathName(), (int32)Format.PixelOrigin,
		(int32)Format.UndistortionChannels, (int32)Format.DistortionChannels, LensSensor.X, LensSensor.Y, FxFy.X, FxFy.Y);
	if (FLensFileEntry* Found = LensFiles.Find(Key))
	{
		if (Found->LensFile)
		{
			Found->LastUsed = ++UseCounter;
			return Found->LensFile;
		}
		LensFiles.Remove(Key);
	}
	ULensFile* LensFile = NewObject<ULensFile>(GetTransientPackage(), NAME_None, RF_Transient);
	LensFile->LensInfo.LensModel = USphericalLensModel::StaticClass();
	LensFile->LensInfo.SensorDimensions = LensSensor;
	LensFile->LensInfo.SqueezeFactor = 1.f;
	LensFile->DataMode = ELensDataMode::STMap;
	FSTMapInfo Info;
	Info.DistortionMap = MapToUse;
	Info.MapFormat = Format;
	LensFile->AddSTMapPoint(0.f, 0.f, Info);
	FFocalLengthInfo FL;
	FL.FxFy = FxFy;
	LensFile->AddFocalLengthPoint(0.f, 0.f, FL);
	LensFiles.Add(Key, FLensFileEntry{ LensFile, ++UseCounter });
	Trim();
	return LensFile;
}

void FDynamicLensCache::Trim()
{
	// cameras keep their own reference to what they use, so dropping an entry never pulls it from under one
	const int64 MaxBytes = int64(FMath::Max(CVarCacheMaxMapMB.GetValueOnGameThread(), 0)) * 1024 * 1024;
	int64 Total = 0;
	for (const TPair<FString, FMapEntry>& It : Maps) Total += It.Value.Bytes;
	while (Total > MaxBytes && Maps.Num() > 1)
	{
		const FString* Oldest = nullptr; uint64 Best = MAX_uint64;
		for (const TPair<FString, FMapEntry>& It : Maps) { if (It.Value.LastUsed < Best) { Best = It.Value.LastUsed; Oldest = &It.Key; } }
		Total -= Maps[*Oldest].Bytes;
		Maps.Remove(FString(*Oldest));
	}
	const int32 MaxFiles = FMath::Max(CVarCacheMaxLensFiles.GetValueOnGameThread(), 1);
	while (LensFiles.Num() > MaxFiles)
	{
		const FString* Oldest = nullptr; uint64 Best = MAX_uint64;
		for (const TPair<FString, FLensFileEntry>& It : LensFiles) { if (It.Value.LastUsed < Best) { Best = It.Value.LastUsed; Oldest = &It.Key; } }
		LensFiles.Remove(FString(*Oldest));
	}
}

void FDynamicLensCache::Clear()
{
	Maps.Reset();
	LensFiles.Reset();
}

FString FDynamicLensCache::Describe() const
{
	int64 Bytes = 0;
	for (const TPair<FString, FMapEntry>& It : Maps) Bytes += It.Value.Bytes;
	return FString::Printf(TEXT("DynamicLens cache: %d extended ST maps (%.1f MB), %d lens files"), Maps.Num(), Bytes / (1024.0 * 1024.0), LensFiles.Num());
}

void FDynamicLensCache::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (TPair<FString, FMapEntry>& It : Maps) Collector.AddReferencedObject(It.Value.Map.Texture);
	for (TPair<FString, FLensFileEntry>& It : LensFiles) Collector.AddReferencedObject(It.Value.LensFile);
}
