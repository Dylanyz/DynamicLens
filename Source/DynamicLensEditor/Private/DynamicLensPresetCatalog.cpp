// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#include "DynamicLensPresetCatalog.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"

// --- entry helpers ----------------------------------------------------------------------------

FString FDynamicLensPresetEntry::FocalText() const
{
	if (FocalMax <= 0.f) return TEXT("any focal");
	if (IsPrime()) return FString::Printf(TEXT("%g mm"), FocalMin);
	return FString::Printf(TEXT("%g-%g mm"), FocalMin, FocalMax);
}

bool FDynamicLensPresetEntry::MatchesSearch(const FString& Lower) const
{
	if (Lower.IsEmpty()) return true;
	return AssetName.ToLower().Contains(Lower)
		|| DisplayName.ToLower().Contains(Lower)
		|| Label.ToLower().Contains(Lower)
		|| Description.ToLower().Contains(Lower)
		|| FamilyDisplay.ToLower().Contains(Lower)
		|| Source.ToLower().Contains(Lower);
}

// --- filter -----------------------------------------------------------------------------------

bool FDynamicLensPresetFilter::IsDefault() const
{
	return Search.IsEmpty()
		&& bFamily[0] && bFamily[1] && bFamily[2] && bFamily[3]
		&& bSpherical && bAnamorphic && bParametric && bSTMap && bProjection
		&& !bBreathes.IsSet() && !bImageCircle.IsSet() && !bPrime.IsSet()
		&& FocalMin == 0.f && FocalMax == 0.f && ApertureMax == 0.f && DistortionMin == 0.f
		&& !bFavouritesOnly;
}

bool FDynamicLensPresetFilter::Passes(const FDynamicLensPresetEntry& E, const TSet<FName>& Favourites) const
{
	if (bFavouritesOnly && !Favourites.Contains(E.Asset.PackageName)) return false;
	if (!E.MatchesSearch(Search.ToLower())) return false;

	const EDynamicLensFamilyFilter Fam = FDynamicLensPresetCatalog::FamilyFromKey(E.Family);
	if (Fam != EDynamicLensFamilyFilter::Count && !bFamily[(uint8)Fam]) return false;

	// optics
	if (E.IsAnamorphic() ? !bAnamorphic : !bSpherical) return false;

	// how the distortion is stored
	switch (E.Type)
	{
	case EDynamicLensProfileType::Parametric: if (!bParametric) return false; break;
	case EDynamicLensProfileType::STMap:      if (!bSTMap)      return false; break;
	case EDynamicLensProfileType::Projection: if (!bProjection) return false; break;
	default: break;
	}

	if (bBreathes.IsSet()    && E.bBreathes        != bBreathes.GetValue())    return false;
	if (bImageCircle.IsSet() && E.HasImageCircle() != bImageCircle.GetValue()) return false;
	if (bPrime.IsSet()       && E.IsPrime()        != bPrime.GetValue())       return false;

	// focal: keep a lens whose measured coverage overlaps the requested window
	if (FocalMin > 0.f && E.FocalMax > 0.f && E.FocalMax < FocalMin) return false;
	if (FocalMax > 0.f && E.FocalMin > 0.f && E.FocalMin > FocalMax) return false;

	// "at least this fast": a lens passes when it opens to ApertureMax or wider
	if (ApertureMax > 0.f && E.MaxAperture > 0.f && E.MaxAperture > ApertureMax) return false;
	if (DistortionMin > 0.f && E.Distortion < 1.f + DistortionMin) return false;

	return true;
}

// --- catalogue --------------------------------------------------------------------------------

FString FDynamicLensPresetCatalog::FamilyKey(EDynamicLensFamilyFilter F)
{
	switch (F)
	{
	case EDynamicLensFamilyFilter::AndyDavis: return TEXT("AndyDavis");
	case EDynamicLensFamilyFilter::Tiedtke:   return TEXT("Tiedtke");
	case EDynamicLensFamilyFilter::Lanthimos: return TEXT("Lanthimos");
	default:                                  return TEXT("Custom");
	}
}

FString FDynamicLensPresetCatalog::FamilyDisplayName(const FString& Key)
{
	if (Key == TEXT("AndyDavis")) return TEXT("Andy Davis");
	if (Key == TEXT("Tiedtke"))   return TEXT("tiedtke");
	if (Key == TEXT("Lanthimos")) return TEXT("Lanthimos films");
	return TEXT("Custom / Mad Rice");
}

EDynamicLensFamilyFilter FDynamicLensPresetCatalog::FamilyFromKey(const FString& Key)
{
	for (uint8 I = 0; I < (uint8)EDynamicLensFamilyFilter::Count; ++I)
	{
		if (FamilyKey((EDynamicLensFamilyFilter)I) == Key) return (EDynamicLensFamilyFilter)I;
	}
	return EDynamicLensFamilyFilter::Custom;
}

namespace
{
	/** "DL_AD_ARRI_Signature" -> "ARRI Signature". Unprefixed names are left alone. */
	FString DynamicLensPrettyName(const FString& AssetName)
	{
		FString Out = AssetName;
		const TCHAR* Prefixes[] = { TEXT("DL_AD_"), TEXT("DL_T_"), TEXT("DL_L_"), TEXT("DL_C_"), TEXT("DL_") };
		for (const TCHAR* Prefix : Prefixes)
		{
			if (Out.StartsWith(Prefix))
			{
				Out.RightChopInline(FCString::Strlen(Prefix));
				break;
			}
		}
		Out.ReplaceInline(TEXT("_"), TEXT(" "));
		return Out;
	}

	/** The family a preset belongs to, from the documented DL_<x>_ prefix. */
	FString DynamicLensFamilyFromAssetName(const FString& AssetName)
	{
		if (AssetName.StartsWith(TEXT("DL_AD_"))) return TEXT("AndyDavis");
		if (AssetName.StartsWith(TEXT("DL_T_")))  return TEXT("Tiedtke");
		if (AssetName.StartsWith(TEXT("DL_L_")))  return TEXT("Lanthimos");
		return TEXT("Custom");
	}

	float DynamicLensTagFloat(const FAssetData& Data, FName Tag, float Fallback)
	{
		FString S;
		return (Data.GetTagValue(Tag, S) && !S.IsEmpty()) ? FCString::Atof(*S) : Fallback;
	}
}

TSharedRef<FDynamicLensPresetCatalog> FDynamicLensPresetCatalog::Get()
{
	static TSharedRef<FDynamicLensPresetCatalog> Instance = MakeShared<FDynamicLensPresetCatalog>();
	return Instance;
}

FDynamicLensPresetCatalog::FDynamicLensPresetCatalog()
{
	BindRegistry();
	Refresh();
}

FDynamicLensPresetCatalog::~FDynamicLensPresetCatalog()
{
	UnbindRegistry();
}

void FDynamicLensPresetCatalog::BindRegistry()
{
	if (bRegistryBound) return;
	if (IAssetRegistry* AR = IAssetRegistry::Get())
	{
		AR->OnAssetAdded().AddRaw(this, &FDynamicLensPresetCatalog::HandleAssetChanged);
		AR->OnAssetRemoved().AddRaw(this, &FDynamicLensPresetCatalog::HandleAssetChanged);
		AR->OnAssetUpdated().AddRaw(this, &FDynamicLensPresetCatalog::HandleAssetChanged);
		AR->OnAssetRenamed().AddRaw(this, &FDynamicLensPresetCatalog::HandleAssetRenamed);
		bRegistryBound = true;
	}
}

void FDynamicLensPresetCatalog::UnbindRegistry()
{
	if (!bRegistryBound) return;
	if (IAssetRegistry* AR = IAssetRegistry::Get())
	{
		AR->OnAssetAdded().RemoveAll(this);
		AR->OnAssetRemoved().RemoveAll(this);
		AR->OnAssetUpdated().RemoveAll(this);
		AR->OnAssetRenamed().RemoveAll(this);
	}
	bRegistryBound = false;
}

void FDynamicLensPresetCatalog::HandleAssetChanged(const FAssetData& Data)
{
	// the registry fires for every asset in the project; only ours should cost a rescan
	if (Data.AssetClassPath == UDynamicLensPreset::StaticClass()->GetClassPathName())
	{
		Refresh();
	}
}

void FDynamicLensPresetCatalog::HandleAssetRenamed(const FAssetData& Data, const FString&)
{
	HandleAssetChanged(Data);
}

void FDynamicLensPresetCatalog::Refresh()
{
	Entries.Reset();

	IAssetRegistry* AR = IAssetRegistry::Get();
	if (!AR) return;

	FARFilter Filter;
	Filter.ClassPaths.Add(UDynamicLensPreset::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> Assets;
	AR->GetAssets(Filter, Assets);

	const UEnum* TypeEnum = StaticEnum<EDynamicLensProfileType>();

	for (const FAssetData& Data : Assets)
	{
		FDynamicLensPresetEntryPtr E = MakeShared<FDynamicLensPresetEntry>();
		E->Asset = Data;
		E->AssetName = Data.AssetName.ToString();
		E->DisplayName = DynamicLensPrettyName(E->AssetName);

		FString LabelTag;
		E->bHasTags = Data.GetTagValue(DynamicLensTags::Label, LabelTag) && !LabelTag.IsEmpty();
		E->Label = E->bHasTags ? LabelTag : E->DisplayName;

		Data.GetTagValue(DynamicLensTags::Description, E->Description);
		Data.GetTagValue(DynamicLensTags::Source, E->Source);
		Data.GetTagValue(DynamicLensTags::SensorMm, E->SensorMm);

		// an asset saved before the tags existed still belongs somewhere: the name prefix is the
		// same contract the tag is written from, so the fallback cannot disagree with it
		FString FamilyTag;
		if (!Data.GetTagValue(DynamicLensTags::Family, FamilyTag) || FamilyTag.IsEmpty())
		{
			FamilyTag = DynamicLensFamilyFromAssetName(E->AssetName);
		}
		E->Family = FamilyTag;
		E->FamilyDisplay = FamilyDisplayName(FamilyTag);

		FString TypeStr;
		Data.GetTagValue(DynamicLensTags::Type, TypeStr);
		E->TypeName = TypeStr;
		if (TypeEnum && !TypeStr.IsEmpty())
		{
			const int64 Value = TypeEnum->GetValueByNameString(TypeStr);
			if (Value != INDEX_NONE) E->Type = (EDynamicLensProfileType)Value;
		}
		E->TypeBadge = E->Type == EDynamicLensProfileType::STMap      ? TEXT("ST MAP")
		             : E->Type == EDynamicLensProfileType::Projection ? TEXT("PROJECTION")
		                                                             : TEXT("PARAMETRIC");

		E->Squeeze       = DynamicLensTagFloat(Data, DynamicLensTags::Squeeze, 1.f);
		E->FocalMin      = DynamicLensTagFloat(Data, DynamicLensTags::FocalMin, 0.f);
		E->FocalMax      = DynamicLensTagFloat(Data, DynamicLensTags::FocalMax, 0.f);
		E->ImageCircleMm = DynamicLensTagFloat(Data, DynamicLensTags::ImageCircleMm, 0.f);
		E->MaxAperture   = DynamicLensTagFloat(Data, DynamicLensTags::MaxAperture, 0.f);
		E->Distortion    = DynamicLensTagFloat(Data, DynamicLensTags::Distortion, 1.f);
		E->MapCount      = (int32)DynamicLensTagFloat(Data, DynamicLensTags::MapCount, 0.f);
		E->bBreathes     = DynamicLensTagFloat(Data, DynamicLensTags::Breathes, 0.f) > 0.5f;

		Entries.Add(E);
	}

	Entries.Sort([](const FDynamicLensPresetEntryPtr& A, const FDynamicLensPresetEntryPtr& B)
	{
		return A->AssetName < B->AssetName;
	});

	OnChanged.Broadcast();
}

void FDynamicLensPresetCatalog::CountByFamily(int32 OutCounts[(uint8)EDynamicLensFamilyFilter::Count]) const
{
	for (uint8 I = 0; I < (uint8)EDynamicLensFamilyFilter::Count; ++I) OutCounts[I] = 0;
	for (const FDynamicLensPresetEntryPtr& E : Entries)
	{
		const EDynamicLensFamilyFilter F = FamilyFromKey(E->Family);
		if (F != EDynamicLensFamilyFilter::Count) ++OutCounts[(uint8)F];
	}
}

TArray<FDynamicLensPresetEntryPtr> FDynamicLensPresetCatalog::BuildView(
	const FDynamicLensPresetFilter& Filter, EDynamicLensSort Sort, bool bAscending,
	const TSet<FName>& Favourites, const TArray<FName>& RecentOrder) const
{
	TArray<FDynamicLensPresetEntryPtr> View;
	View.Reserve(Entries.Num());
	for (const FDynamicLensPresetEntryPtr& E : Entries)
	{
		if (Filter.Passes(*E, Favourites)) View.Add(E);
	}

	// Recent orders by a position list rather than by any field on the entry
	TMap<FName, int32> RecentIndex;
	if (Sort == EDynamicLensSort::Recent)
	{
		for (int32 I = 0; I < RecentOrder.Num(); ++I) RecentIndex.Add(RecentOrder[I], I);
	}

	View.Sort([&](const FDynamicLensPresetEntryPtr& A, const FDynamicLensPresetEntryPtr& B)
	{
		int32 Cmp = 0;
		switch (Sort)
		{
		case EDynamicLensSort::Label:
			Cmp = A->Label.Compare(B->Label, ESearchCase::IgnoreCase);
			break;
		case EDynamicLensSort::Family:
			Cmp = A->FamilyDisplay.Compare(B->FamilyDisplay, ESearchCase::IgnoreCase);
			break;
		case EDynamicLensSort::FocalLength:
			Cmp = A->FocalMin < B->FocalMin ? -1 : (A->FocalMin > B->FocalMin ? 1 : 0);
			break;
		case EDynamicLensSort::Distortion:
			Cmp = A->Distortion < B->Distortion ? -1 : (A->Distortion > B->Distortion ? 1 : 0);
			break;
		case EDynamicLensSort::Aperture:
			Cmp = A->MaxAperture < B->MaxAperture ? -1 : (A->MaxAperture > B->MaxAperture ? 1 : 0);
			break;
		case EDynamicLensSort::Recent:
		{
			const int32* PA = RecentIndex.Find(A->Asset.PackageName);
			const int32* PB = RecentIndex.Find(B->Asset.PackageName);
			const int32 IA = PA ? *PA : MAX_int32;
			const int32 IB = PB ? *PB : MAX_int32;
			Cmp = IA < IB ? -1 : (IA > IB ? 1 : 0);
			break;
		}
		default:
			break;
		}
		// ties always fall back to the asset name, so the order never flickers between refreshes
		if (Cmp == 0) Cmp = A->AssetName.Compare(B->AssetName, ESearchCase::IgnoreCase);
		return bAscending ? Cmp < 0 : Cmp > 0;
	});

	return View;
}
