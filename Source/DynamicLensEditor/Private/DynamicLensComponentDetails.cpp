// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#include "DynamicLensComponentDetails.h"

#include "DynamicLensComponent.h"
#include "DynamicLensEditorModule.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
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
	TSharedPtr<SWidget> ValueWidget;
	Row->GetDefaultWidgets(NameWidget, ValueWidget, /*bAddWidgetDecoration*/ true);
	if (!NameWidget.IsValid() || !ValueWidget.IsValid()) return;

	TWeakObjectPtr<UDynamicLensComponent> WeakTarget = Target;

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
				ValueWidget.ToSharedRef()
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
