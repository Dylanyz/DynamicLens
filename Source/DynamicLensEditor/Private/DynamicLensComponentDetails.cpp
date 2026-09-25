// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#include "DynamicLensComponentDetails.h"

#include "DynamicLensComponent.h"
#include "DynamicLensEditorModule.h"
#include "DynamicLensPresetCatalog.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DynamicLensComponentDetails"

TSharedRef<IDetailCustomization> FDynamicLensComponentDetails::MakeInstance()
{
	return MakeShared<FDynamicLensComponentDetails>();
}

void FDynamicLensComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (UDynamicLensComponent* Component = Cast<UDynamicLensComponent>(Object.Get()))
		{
			Target = Component;
			break;
		}
	}

	const TSharedRef<IPropertyHandle> PresetHandle =
		DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDynamicLensComponent, Preset));
	if (!PresetHandle->IsValidHandle()) return;

	IDetailPropertyRow* Row = DetailBuilder.EditDefaultProperty(PresetHandle);
	if (!Row) return;

	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> DefaultValueWidget;
	Row->GetDefaultWidgets(NameWidget, DefaultValueWidget, /*bAddWidgetDecoration*/ true);
	if (!NameWidget.IsValid() || !DefaultValueWidget.IsValid()) return;

	TWeakObjectPtr<UDynamicLensComponent> WeakTarget = Target;

	// Our own picker in place of the default one, for the one thing the default cannot do: leave
	// out presets hidden in the Preset Browser. Same handle, so undo, multi-edit, reset-to-default
	// and Sequencer keying behave exactly as before. The catalogue holds the hidden set, so hiding
	// in an open browser is reflected the next time this dropdown opens.
	TSharedRef<FDynamicLensPresetCatalog> Catalog = FDynamicLensPresetCatalog::Get();
	const TSharedRef<SWidget> ValueWidget = SNew(SObjectPropertyEntryBox)
		.PropertyHandle(PresetHandle)
		.AllowedClass(UDynamicLensPreset::StaticClass())
		.ThumbnailPool(DetailBuilder.GetThumbnailPool())
		.OnShouldFilterAsset_Lambda([Catalog](const FAssetData& Asset)
		{
			return Catalog->IsHidden(Asset.PackageName);
		});

	Row->CustomWidget(/*bShowChildren*/ true)
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		.MinDesiredWidth(280.f)
		.MaxDesiredWidth(0.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				ValueWidget
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(FMargin(6.f, 1.f))
				.ToolTipText(LOCTEXT("BrowseTip",
					"Open the Dynamic Lens Preset Browser on this camera: every lens, with filters "
					"for anamorphic, maker and data type. Clicking a lens applies it straight away, "
					"honouring the Match Camera options below."))
				.OnClicked_Lambda([WeakTarget]()
				{
					FDynamicLensEditorModule::OpenPresetBrowser(WeakTarget);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("PropertyWindow.NormalFont"))
					.Text(LOCTEXT("Browse", "Browse"))
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
