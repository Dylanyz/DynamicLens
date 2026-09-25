// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

// The Preset Browser's model: every DynamicLens preset in the project, read from the Asset Registry.
#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "DynamicLensTypes.h"

/**
 * One preset, described entirely by its Asset Registry tags.
 *
 * NOTHING here loads the asset. Presets hard-reference their profile, and ST-map profiles hard-
 * reference their textures (~107 MB across the shipped set), so loading the catalogue to display it
 * would cost more memory than the rest of the plugin put together. The asset is loaded at exactly
 * one moment: when the user clicks a lens to apply it.
 */
struct FDynamicLensPresetEntry
{
	FAssetData Asset;

	/** Asset name as it appears in the Content Browser, e.g. "DL_AD_ARRI_Signature". */
	FString AssetName;
	/** Asset name with the DL_<family>_ prefix stripped and underscores opened out, for display. */
	FString DisplayName;
	/** The lens series, from the profile's Label. Falls back to the asset name. */
	FString Label;
	FString Description;
	/** The profile's full Source text: who measured it, on what, with what caveats. Shown verbatim. */
	FString Source;

	/** Family key ("AndyDavis") and how it reads in the UI ("Andy Davis"). */
	FString Family;
	FString FamilyDisplay;

	EDynamicLensProfileType Type = EDynamicLensProfileType::Parametric;
	FString TypeName;
	/** Short badge for the data type: "PARAMETRIC" / "ST MAP" / "PROJECTION". */
	FString TypeBadge;

	float Squeeze = 1.f;
	float FocalMin = 0.f;
	float FocalMax = 0.f;
	float ImageCircleMm = 0.f;
	float MaxAperture = 0.f;
	FString SensorMm;
	int32 MapCount = 0;
	bool bBreathes = false;
	/** Overscan the lens needs: 1.0 rectilinear, ~1.25 heavy barrel. See DynamicLensTags::Distortion. */
	float Distortion = 1.f;

	/** False when the asset predates the tags; the browser then shows the name only. */
	bool bHasTags = false;

	bool IsAnamorphic() const { return Squeeze > 1.01f; }
	bool IsPrime() const { return FocalMax > 0.f && FMath::IsNearlyEqual(FocalMin, FocalMax, 0.01f); }
	bool HasImageCircle() const { return ImageCircleMm > 0.f; }

	/** Focal coverage as it reads on a lens barrel: "50 mm" or "44-440 mm". */
	FString FocalText() const;
	/** Everything the search box matches against. */
	bool MatchesSearch(const FString& Lower) const;
};

using FDynamicLensPresetEntryPtr = TSharedPtr<FDynamicLensPresetEntry>;

/** Where a preset's data came from. Filter checkboxes, in the order they are shown. */
enum class EDynamicLensFamilyFilter : uint8 { AndyDavis, Tiedtke, Lanthimos, Custom, Count };

/** How the catalogue is ordered. */
enum class EDynamicLensSort : uint8
{
	Name, Label, Family, FocalLength, Distortion, Aperture, Recent, Count
};

/** What the rows are grouped under. None = one flat list. */
enum class EDynamicLensGroup : uint8
{
	None, Family, Optics, DataType, Count
};

/** Everything the filter rail can say. Defaults are "show everything". */
struct FDynamicLensPresetFilter
{
	FString Search;

	/** Per-family switches. All on = no family filtering. */
	bool bFamily[(uint8)EDynamicLensFamilyFilter::Count] = { true, true, true, true };

	bool bSpherical = true;
	bool bAnamorphic = true;

	bool bParametric = true;
	bool bSTMap = true;
	bool bProjection = true;

	/** Tri-state: unset = don't care. */
	TOptional<bool> bBreathes;
	TOptional<bool> bImageCircle;
	TOptional<bool> bPrime;

	/** Inclusive ranges; a lens passes if its coverage overlaps. 0 bounds mean "unbounded". */
	float FocalMin = 0.f;
	float FocalMax = 0.f;
	float ApertureMax = 0.f;
	float DistortionMin = 0.f;

	bool bFavouritesOnly = false;
	/** Hidden presets inline with the rest (dimmed) instead of tucked into the Hidden section. */
	bool bShowHidden = false;

	/** Everything but hiding, which BuildView handles because hidden lenses go to their own section. */
	bool Passes(const FDynamicLensPresetEntry& E, const TSet<FName>& Favourites) const;
	/** True when nothing is narrowed, so the UI can offer "Clear" only when it would do something. */
	bool IsDefault() const;
	void Reset() { *this = FDynamicLensPresetFilter(); }
};

/**
 * Scans the Asset Registry for UDynamicLensPreset assets and keeps the result fresh.
 *
 * One instance is shared by every open browser tab. It re-scans when assets are added, removed,
 * renamed or re-saved, so importing presets from Python shows up without reopening the window.
 */
class FDynamicLensPresetCatalog
{
public:
	FDynamicLensPresetCatalog();
	~FDynamicLensPresetCatalog();

	static TSharedRef<FDynamicLensPresetCatalog> Get();

	const TArray<FDynamicLensPresetEntryPtr>& GetAll() const { return Entries; }

	/** Re-read everything from the registry. Cheap: tags only, no asset loads. */
	void Refresh();

	/** Fires after any refresh, so open windows rebuild their lists. */
	DECLARE_MULTICAST_DELEGATE(FOnCatalogChanged);
	FOnCatalogChanged OnChanged;

	/**
	 * Filtered and sorted, ready for the list view. Hidden presets are left out unless the filter
	 * shows them; when it doesn't and OutHidden is given, the hidden ones that pass every other
	 * filter land there, sorted the same way, for the Hidden section.
	 */
	TArray<FDynamicLensPresetEntryPtr> BuildView(const FDynamicLensPresetFilter& Filter,
	                                             EDynamicLensSort Sort,
	                                             bool bAscending,
	                                             const TSet<FName>& Favourites,
	                                             const TArray<FName>& RecentOrder,
	                                             TArray<FDynamicLensPresetEntryPtr>* OutHidden = nullptr) const;

	/**
	 * Hiding. Lives here rather than on a browser tab so every open tab and the component's Preset
	 * dropdown agree; persisted through DynamicLensHiddenPresets (per-user config, never the asset).
	 */
	bool IsHidden(const FDynamicLensPresetEntry& E) const { return Hidden.Contains(E.Asset.PackageName); }
	bool IsHidden(FName PackageName) const { return Hidden.Contains(PackageName); }
	void SetHidden(FName PackageName, bool bHide);
	int32 NumHidden() const;

	/** How many presets each family has, for the counts beside the filter checkboxes. */
	void CountByFamily(int32 OutCounts[(uint8)EDynamicLensFamilyFilter::Count]) const;

	static FString FamilyKey(EDynamicLensFamilyFilter F);
	static FString FamilyDisplayName(const FString& Key);
	static EDynamicLensFamilyFilter FamilyFromKey(const FString& Key);

private:
	void BindRegistry();
	void UnbindRegistry();
	void HandleAssetChanged(const FAssetData& Data);
	void HandleAssetRenamed(const FAssetData& Data, const FString& OldName);

	TArray<FDynamicLensPresetEntryPtr> Entries;
	TSet<FName> Hidden;
	bool bRegistryBound = false;
};
