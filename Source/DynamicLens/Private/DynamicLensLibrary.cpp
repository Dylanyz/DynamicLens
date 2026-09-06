#include "DynamicLensLibrary.h"

#include "CineCameraActor.h"
#include "DynamicLensComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LensFile.h"
#include "Kismet/GameplayStatics.h"

UDynamicLensComponent* UDynamicLensLibrary::AddToActor(AActor* Actor, UDynamicLensPreset* Preset)
{
	if (!Actor) return nullptr;
	UDynamicLensComponent* Comp = Actor->FindComponentByClass<UDynamicLensComponent>();
	if (!Comp)
	{
		Actor->Modify();
		Comp = NewObject<UDynamicLensComponent>(Actor, UDynamicLensComponent::StaticClass(), TEXT("DynamicLens"), RF_Transactional);
		Actor->AddInstanceComponent(Comp);
		Comp->RegisterComponent();
	}
	if (Preset)
	{
		Comp->Modify();
		Comp->Preset = Preset;
	}
	return Comp;
}

int32 UDynamicLensLibrary::AddToAllCineCameras(UObject* WorldContextObject, UDynamicLensPreset* Preset, bool bOverwriteExisting)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World) return 0;
	int32 Count = 0;
	for (TActorIterator<ACineCameraActor> It(World); It; ++It)
	{
		ACineCameraActor* Cam = *It;
		if (!Cam || Cam->IsTemplate()) continue;
		UDynamicLensComponent* Existing = Cam->FindComponentByClass<UDynamicLensComponent>();
		if (Existing && !bOverwriteExisting) continue;
		AddToActor(Cam, Preset);
		++Count;
	}
	return Count;
}

int32 UDynamicLensLibrary::RemoveFromAllCineCameras(UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World) return 0;
	int32 Count = 0;
	for (TActorIterator<ACineCameraActor> It(World); It; ++It)
	{
		TArray<UDynamicLensComponent*> Comps;
		It->GetComponents<UDynamicLensComponent>(Comps);
		for (UDynamicLensComponent* C : Comps)
		{
			It->Modify();
			C->ClearEffect();
			C->DestroyComponent();
			++Count;
		}
	}
	return Count;
}

bool UDynamicLensLibrary::FillProfile(UDynamicLensProfile* Profile, const FString& Label, const FString& Source, const TArray<float>& FocusCm, const TArray<float>& FocalsMm, const TArray<float>& FlatParams)
{
	if (!Profile || FocusCm.Num() == 0 || FocalsMm.Num() == 0) return false;
	if (FlatParams.Num() != FocalsMm.Num() * FocusCm.Num() * 5) return false;
	Profile->Modify();
	Profile->Type = EDynamicLensProfileType::Parametric;
	Profile->Label = Label;
	Profile->Source = Source;
	Profile->FocusCm = FocusCm;
	Profile->Rows.Reset(FocalsMm.Num());
	int32 I = 0;
	for (float Focal : FocalsMm)
	{
		FDynamicLensProfileRow Row;
		Row.FocalMm = Focal;
		Row.ByFocus.Reserve(FocusCm.Num());
		for (int32 D = 0; D < FocusCm.Num(); ++D)
		{
			FDynamicLensParams P;
			P.K1 = FlatParams[I++]; P.K2 = FlatParams[I++]; P.K3 = FlatParams[I++]; P.P1 = FlatParams[I++]; P.P2 = FlatParams[I++];
			Row.ByFocus.Add(P);
		}
		Profile->Rows.Add(MoveTemp(Row));
	}
	Profile->RefreshCoverage();
	return Profile->IsValidProfile();
}

bool UDynamicLensLibrary::AddSTMapFromLensFile(UDynamicLensProfile* Profile, ULensFile* LensFile, float FocalMm, UTexture* MapTexture, float Squeeze)
{
	if (!Profile || !LensFile) return false;
	const TArray<FSTMapPointInfo> Points = LensFile->GetSTMapPoints();
	if (Points.Num() == 0) return false;
	const FSTMapPointInfo& Pt = Points[0];
	UTexture* Tex = MapTexture ? MapTexture : Pt.STMapInfo.DistortionMap.Get();
	if (!Tex) return false;

	Profile->Modify();
	Profile->Type = EDynamicLensProfileType::STMap;
	Profile->NativeSensorMm = LensFile->LensInfo.SensorDimensions;
	Profile->Squeeze = FMath::Max(Squeeze, 1.f);
	FDynamicLensSTMapEntry Entry;
	Entry.FocalMm = FocalMm;
	Entry.FocusCm = Pt.Focus;
	Entry.Map = Tex;
	Entry.MapFormat = Pt.STMapInfo.MapFormat;
	// replace an existing entry at the same focal length
	bool bReplaced = false;
	for (FDynamicLensSTMapEntry& E : Profile->STMaps)
	{
		if (FMath::IsNearlyEqual(E.FocalMm, FocalMm, 0.01f)) { E = Entry; bReplaced = true; break; }
	}
	if (!bReplaced) Profile->STMaps.Add(Entry);
	Profile->STMaps.Sort([](const FDynamicLensSTMapEntry& A, const FDynamicLensSTMapEntry& B) { return A.FocalMm < B.FocalMm; });
	Profile->RefreshCoverage();
	return true;
}

bool UDynamicLensLibrary::ReadSTMapSamples(UTexture2D* Map, int32 Cols, int32 Rows, TArray<float>& OutUV)
{
	OutUV.Reset();
#if WITH_EDITORONLY_DATA
	if (!Map || Cols < 2 || Rows < 2 || !Map->Source.IsValid()) return false;
	const int32 W = Map->Source.GetSizeX(), H = Map->Source.GetSizeY();
	const ETextureSourceFormat Fmt = Map->Source.GetFormat();
	const uint8* Data = Map->Source.LockMipReadOnly(0);
	if (!Data) return false;
	const int32 Bpp = Map->Source.GetBytesPerPixel();
	auto ReadRG = [&](int32 X, int32 Y, float& R, float& G)
	{
		const uint8* Px = Data + (Y * W + X) * Bpp;
		switch (Fmt)
		{
		case TSF_RGBA32F: R = reinterpret_cast<const float*>(Px)[0]; G = reinterpret_cast<const float*>(Px)[1]; break;
		case TSF_RGBA16F: R = reinterpret_cast<const FFloat16*>(Px)[0].GetFloat(); G = reinterpret_cast<const FFloat16*>(Px)[1].GetFloat(); break;
		case TSF_BGRA8: R = Px[2] / 255.f; G = Px[1] / 255.f; break;
		default: R = G = 0.f; break;
		}
	};
	OutUV.Reserve(Cols * Rows * 2);
	for (int32 J = 0; J < Rows; ++J)
	{
		const float V = (J + 0.5f) / Rows;
		const int32 Y = FMath::Clamp(FMath::FloorToInt(V * H), 0, H - 1);
		for (int32 I = 0; I < Cols; ++I)
		{
			const float U = (I + 0.5f) / Cols;
			const int32 X = FMath::Clamp(FMath::FloorToInt(U * W), 0, W - 1);
			float R, G; ReadRG(X, Y, R, G);
			OutUV.Add(R); OutUV.Add(G);
		}
	}
	Map->Source.UnlockMip(0);
	return true;
#else
	return false;
#endif
}

void UDynamicLensLibrary::RefreshProfile(UDynamicLensProfile* Profile)
{
	if (Profile) Profile->RefreshCoverage();
}
