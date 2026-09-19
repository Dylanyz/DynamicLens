// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

// The Dynamic Lens Preset Browser: a dockable window that filters the lens catalogue and applies
// a preset to the selected camera on a single click.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "DynamicLensPresetCatalog.h"

class UDynamicLensComponent;
class SSearchBox;

/**
 * One row of the list. Either a group header or a preset, so grouping needs no second view type.
 */
struct FDynamicLensBrowserRow
{
	/** Non-empty means this row is a group header and Entry is unset. */
	FString Header;
	int32 HeaderCount = 0;
	FDynamicLensPresetEntryPtr Entry;

	bool IsHeader() const { return !Header.IsEmpty(); }

	static TSharedRef<FDynamicLensBrowserRow> MakeHeader(const FString& In, int32 Count)
	{
		TSharedRef<FDynamicLensBrowserRow> R = MakeShared<FDynamicLensBrowserRow>();
		R->Header = In;
		R->HeaderCount = Count;
		return R;
	}
	static TSharedRef<FDynamicLensBrowserRow> MakeEntry(FDynamicLensPresetEntryPtr In)
	{
		TSharedRef<FDynamicLensBrowserRow> R = MakeShared<FDynamicLensBrowserRow>();
		R->Entry = In;
		return R;
	}
};

using FDynamicLensBrowserRowPtr = TSharedPtr<FDynamicLensBrowserRow>;

/**
 * The browser.
 *
 * Targets: it follows the editor selection, applying to every selected actor that carries a
 * Dynamic Lens component, so it can stay docked while you click through cameras. The padlock
 * pins it to whatever it is on now and stops it following.
 *
 * Applying always goes through UDynamicLensComponent::ApplyPreset, which is the same path the
 * A1/A2 buttons use - so the Match Camera checkboxes on the component mean exactly the same
 * thing here as they do there.
 */
class SDynamicLensPresetBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDynamicLensPresetBrowser) {}
		/** Seeded when opened from a component's details panel, so it starts on that camera. */
		SLATE_ARGUMENT(TWeakObjectPtr<UDynamicLensComponent>, InitialTarget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDynamicLensPresetBrowser() override;

	/** Tab id for the nomad spawner. */
	static const FName TabId;

	/** Aim an already-open browser at a camera and pin it there (the Browse button on a component). */
	void SetTarget(TWeakObjectPtr<UDynamicLensComponent> InTarget);

private:
	// --- build -------------------------------------------------------------------------------
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildFilterRail();
	TSharedRef<SWidget> BuildList();
	TSharedRef<SWidget> BuildDetailPane();
	TSharedRef<SWidget> BuildSortMenu();
	TSharedRef<SWidget> BuildGroupMenu();

	TSharedRef<ITableRow> GenerateRow(FDynamicLensBrowserRowPtr Item, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<SWidget> BuildPresetRowContent(FDynamicLensPresetEntryPtr Entry);

	/** A labelled checkbox that drives one bool on the filter. */
	TSharedRef<SWidget> FilterCheck(const FText& Label, TFunction<bool()> Get, TFunction<void(bool)> Set, const FText& Tooltip = FText::GetEmpty());
	/** A three-state filter: Any / Yes / No. */
	TSharedRef<SWidget> TriStateRow(const FText& Label, TOptional<bool>* Target, const FText& Tooltip);

	// --- data --------------------------------------------------------------------------------
	void RebuildRows();
	void HandleCatalogChanged();
	void HandleSearchChanged(const FText& Text);
	void HandleSelectionChanged(FDynamicLensBrowserRowPtr Item, ESelectInfo::Type SelectInfo);

	/** Loads the asset (the one place anything is loaded) and applies it to every target. */
	void ApplyEntry(FDynamicLensPresetEntryPtr Entry);

	// --- targets -----------------------------------------------------------------------------
	void HandleEditorSelectionChanged(UObject* Object);
	TArray<UDynamicLensComponent*> ResolveTargets() const;
	FText GetTargetText() const;

	// --- favourites / recents / persistence ---------------------------------------------------
	bool IsFavourite(const FDynamicLensPresetEntry& E) const;
	void ToggleFavourite(FDynamicLensPresetEntryPtr Entry);
	void PushRecent(FName PackageName);
	void SaveConfig() const;
	void LoadConfig();

	// --- state -------------------------------------------------------------------------------
	TSharedPtr<FDynamicLensPresetCatalog> Catalog;
	FDynamicLensPresetFilter Filter;
	EDynamicLensSort Sort = EDynamicLensSort::Name;
	EDynamicLensGroup Group = EDynamicLensGroup::Family;
	bool bSortAscending = true;

	TArray<FDynamicLensBrowserRowPtr> Rows;
	TSharedPtr<SListView<FDynamicLensBrowserRowPtr>> ListView;
	TSharedPtr<SSearchBox> SearchBox;

	FDynamicLensPresetEntryPtr Selected;

	TSet<FName> Favourites;
	TArray<FName> Recents;

	/** Cameras the browser writes to. Refreshed from the editor selection unless pinned. */
	TArray<TWeakObjectPtr<UDynamicLensComponent>> Targets;
	bool bPinTarget = false;

	FDelegateHandle CatalogChangedHandle;
	FDelegateHandle SelectionChangedHandle;
};
