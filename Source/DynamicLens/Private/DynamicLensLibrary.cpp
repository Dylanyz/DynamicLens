#include "DynamicLensLibrary.h"

#include "CineCameraActor.h"
#include "DynamicLensComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
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
	// overscan from the map itself: how far outside the frame the border pixels source from (undistortion convention)
	if (UTexture2D* Tex2D = Cast<UTexture2D>(Tex))
	{
		TArray<float> UV;
		const int32 N = 64;
		if (ReadSTMapSamples(Tex2D, N, N, UV))
		{
			float Over = 1.f;
			for (int32 J = 0; J < N; ++J)
			{
				for (int32 I = 0; I < N; ++I)
				{
					if (I != 0 && I != N - 1 && J != 0 && J != N - 1) continue;
					const float U = (I + 0.5f) / N, V = (J + 0.5f) / N;
					const float SU = UV[2 * (J * N + I)], SV = UV[2 * (J * N + I) + 1];
					if (FMath::Abs(U - 0.5f) > 0.01f) Over = FMath::Max(Over, FMath::Abs(SU - 0.5f) / FMath::Abs(U - 0.5f));
					if (FMath::Abs(V - 0.5f) > 0.01f) Over = FMath::Max(Over, FMath::Abs(SV - 0.5f) / FMath::Abs(V - 0.5f));
				}
			}
			Entry.NeededOverscan = FMath::Clamp(Over, 1.f, 2.f);
		}
	}
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


UTexture2D* UDynamicLensLibrary::BuildExtendedSTMap(UTexture2D* Map, bool bBottomLeftOrigin, FVector2D DisplacementScale, float MaxExtend, int32 OutWidth, float& OutNeededOverscan, float& OutExtend)
{
	OutNeededOverscan = 1.f; OutExtend = 1.f;
#if WITH_EDITORONLY_DATA
	if (!Map || !Map->Source.IsValid()) return nullptr;
	const int32 SW = Map->Source.GetSizeX(), SH = Map->Source.GetSizeY();
	if (SW < 8 || SH < 8) return nullptr;
	const ETextureSourceFormat Fmt = Map->Source.GetFormat();
	const uint8* Data = Map->Source.LockMipReadOnly(0);
	if (!Data) return nullptr;
	const int32 Bpp = Map->Source.GetBytesPerPixel();

	// pull the source into a float grid at up to 1440 wide (the fields are smooth; this is plenty)
	const int32 Skip = FMath::Max(1, SW / 1440);
	const int32 GW = SW / Skip, GH = SH / Skip;
	TArray<FVector2f> Grid; Grid.SetNumUninitialized(GW * GH);
	for (int32 J = 0; J < GH; ++J)
	{
		for (int32 I = 0; I < GW; ++I)
		{
			const uint8* Px = Data + ((J * Skip) * SW + I * Skip) * Bpp;
			float R = 0.f, G = 0.f;
			switch (Fmt)
			{
			case TSF_RGBA32F: R = reinterpret_cast<const float*>(Px)[0]; G = reinterpret_cast<const float*>(Px)[1]; break;
			case TSF_RGBA16F: R = reinterpret_cast<const FFloat16*>(Px)[0].GetFloat(); G = reinterpret_cast<const FFloat16*>(Px)[1].GetFloat(); break;
			case TSF_BGRA8: R = Px[2] / 255.f; G = Px[1] / 255.f; break;
			default: break;
			}
			Grid[J * GW + I] = FVector2f(R, G);
		}
	}
	Map->Source.UnlockMip(0);

	// valid domain: these maps are clamped to [0,1] where the source leaves the frame, so a band of border texels
	// carries no information (Cooke FFi 27 mm: the left ~3% reads exactly 0). Find the inner rectangle that is not clamped.
	auto Clamped = [](float V) { return V < 0.0005f || V > 0.9995f; };
	auto ColValid = [&](int32 I) { int32 Bad = 0, N = 0; for (int32 J = GH / 5; J < GH * 4 / 5; ++J) { ++N; if (Clamped(Grid[J * GW + I].X)) ++Bad; } return Bad * 10 < N; };
	auto RowValid = [&](int32 J) { int32 Bad = 0, N = 0; for (int32 I = GW / 5; I < GW * 4 / 5; ++I) { ++N; if (Clamped(Grid[J * GW + I].Y)) ++Bad; } return Bad * 10 < N; };
	int32 L = 0, R = GW - 1, T = 0, B = GH - 1;
	while (L < GW / 2 && !ColValid(L)) ++L;
	while (R > GW / 2 && !ColValid(R)) --R;
	while (T < GH / 2 && !RowValid(T)) ++T;
	while (B > GH / 2 && !RowValid(B)) --B;
	auto RowToV = [&](int32 J) { const float Rv = (J + 0.5f) / GH; return bBottomLeftOrigin ? (1.f - Rv) : Rv; };
	const FVector2f DomMin((L + 0.5f) / GW, FMath::Min(RowToV(T), RowToV(B)));
	const FVector2f DomMax((R + 0.5f) / GW, FMath::Max(RowToV(T), RowToV(B)));

	// sample the grid at a UV in the map's own convention (v up if bottom-left origin)
	auto Sample = [&](float U, float V) -> FVector2f
	{
		const float Row = bBottomLeftOrigin ? (1.f - V) : V;
		const float X = FMath::Clamp(U * GW - 0.5f, 0.f, GW - 1.f), Y = FMath::Clamp(Row * GH - 0.5f, 0.f, GH - 1.f);
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, GW - 1), Y1 = FMath::Min(Y0 + 1, GH - 1);
		const float Tx = X - X0, Ty = Y - Y0;
		const FVector2f A = FMath::Lerp(Grid[Y0 * GW + X0], Grid[Y0 * GW + X1], Tx);
		const FVector2f Bv = FMath::Lerp(Grid[Y1 * GW + X0], Grid[Y1 * GW + X1], Tx);
		return FMath::Lerp(A, Bv, Ty);
	};
	// displacement D(p) = F(p) - p, extrapolated linearly outside the valid domain from its border value and gradient
	auto Displacement = [&](FVector2f P) -> FVector2f
	{
		const FVector2f Pb(FMath::Clamp(P.X, DomMin.X, DomMax.X), FMath::Clamp(P.Y, DomMin.Y, DomMax.Y));
		FVector2f D = Sample(Pb.X, Pb.Y) - Pb;
		const float Dx = FMath::Min(4.f / GW, 0.25f * (DomMax.X - DomMin.X)), Dy = FMath::Min(4.f / GH, 0.25f * (DomMax.Y - DomMin.Y));
		if (P.X != Pb.X)
		{
			const float Inner = (P.X > Pb.X) ? Pb.X - Dx : Pb.X + Dx;
			const FVector2f Din = Sample(Inner, Pb.Y) - FVector2f(Inner, Pb.Y);
			D += (D - Din) / (Pb.X - Inner) * (P.X - Pb.X);
		}
		if (P.Y != Pb.Y)
		{
			const float Inner = (P.Y > Pb.Y) ? Pb.Y - Dy : Pb.Y + Dy;
			const FVector2f Din = Sample(Pb.X, Inner) - FVector2f(Pb.X, Inner);
			D += (D - Din) / (Pb.Y - Inner) * (P.Y - Pb.Y);
		}
		return D;
	};

	// how far outside the frame the frame border's sources reach (extrapolated across any clamped band), map units
	float Extent = 1.f;
	for (int32 K = 0; K <= 256; ++K)
	{
		const float Tk = K / 256.f;
		for (const FVector2f& Pt : { FVector2f(Tk, 0.f), FVector2f(Tk, 1.f), FVector2f(0.f, Tk), FVector2f(1.f, Tk) })
		{
			const FVector2f F = Pt + Displacement(Pt);
			Extent = FMath::Max3(Extent, FMath::Abs(F.X - 0.5f) * 2.f, FMath::Abs(F.Y - 0.5f) * 2.f);
		}
	}
	OutNeededOverscan = FMath::Clamp(Extent, 1.f, 4.f);
	const float Extend = FMath::Clamp(FMath::Max(OutNeededOverscan * 1.15f, 1.05f), 1.f, FMath::Max(MaxExtend, 1.f));
	OutExtend = Extend;

	// Epic's blend shader crops the map for a smaller filmback but keeps the displacement values as they are, so the
	// values must already be in the CAMERA frame's units: D_cam = D_map * DisplacementScale (= map sensor / camera sensor).
	const FVector2f DS((float)DisplacementScale.X, (float)DisplacementScale.Y);
	const int32 OW = FMath::Clamp(OutWidth, 64, 4096);
	const int32 OH = FMath::Max(8, FMath::RoundToInt(OW * (float)SH / SW));
	UTexture2D* Out = UTexture2D::CreateTransient(OW, OH, PF_G32R32F);
	Out->SRGB = false;
	Out->Filter = TF_Bilinear;
	Out->AddressX = TA_Clamp;
	Out->AddressY = TA_Clamp;
	Out->NeverStream = true;
	FTexture2DMipMap& Mip = Out->GetPlatformData()->Mips[0];
	float* Dst = static_cast<float*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 J = 0; J < OH; ++J)
	{
		const float RowV = (J + 0.5f) / OH;
		const float Ve = bBottomLeftOrigin ? (1.f - RowV) : RowV;      // position of this texel in the extended frame
		for (int32 I = 0; I < OW; ++I)
		{
			const float Ue = (I + 0.5f) / OW;
			const FVector2f Pe(Ue, Ve);
			const FVector2f P(0.5f + (Ue - 0.5f) * Extend, 0.5f + (Ve - 0.5f) * Extend);   // in the original frame
			const FVector2f Fe = Pe + Displacement(P) * DS;
			float* Px = Dst + 2 * (J * OW + I);
			Px[0] = Fe.X; Px[1] = Fe.Y;
		}
	}
	Mip.BulkData.Unlock();
	Out->UpdateResource();
	return Out;
#else
	return nullptr;
#endif
}
