// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#include "SDynamicLensPresetBrowser.h"

#include "DynamicLensComponent.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/ConfigCacheIni.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "DynamicLensPresetBrowser"

const FName SDynamicLensPresetBrowser::TabId(TEXT("DynamicLensPresetBrowser"));

namespace
{
	/** Shared with the hidden set, so everything the browser remembers sits in one ini section. */
	const TCHAR* GConfigSection = DynamicLensHiddenPresets::Section;

	/**
	 * Curvature -> 0..1 bar. Full scale is 0.30, which is the widest real lens in the shipped
	 * catalogue (the Angenieux Optimo zoom); fisheyes sit around 0.6 and simply peg, which is the
	 * honest reading for them. Measured across all 53 profiles, so the bar uses its whole width
	 * instead of bunching every prime near zero.
	 */
	float DistortionBarFraction(float Distortion)
	{
		return FMath::Clamp(Distortion / 0.30f, 0.f, 1.f);
	}

	FLinearColor DistortionBarColour(float Distortion)
	{
		// calm -> hot, so a glance separates a clean prime from a fisheye
		return FMath::Lerp(FLinearColor(0.25f, 0.55f, 0.85f), FLinearColor(0.95f, 0.45f, 0.15f),
		                   DistortionBarFraction(Distortion));
	}

	FSlateFontInfo BoldFont(int32 Size)  { return FCoreStyle::GetDefaultFontStyle("Bold", Size); }
	FSlateFontInfo LightFont(int32 Size) { return FCoreStyle::GetDefaultFontStyle("Regular", Size); }

	/** A small pill of text, for the badges on a row. */
	TSharedRef<SWidget> Badge(const FString& Text, const FLinearColor& Colour)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(Colour)
			.Padding(FMargin(4.f, 1.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Text))
				.Font(BoldFont(7))
				.ColorAndOpacity(FLinearColor::White)
			];
	}
}

// ---------------------------------------------------------------------------------- construction

void SDynamicLensPresetBrowser::Construct(const FArguments& InArgs)
{
	Catalog = FDynamicLensPresetCatalog::Get();
	CatalogChangedHandle = Catalog->OnChanged.AddSP(this, &SDynamicLensPresetBrowser::HandleCatalogChanged);

	LoadConfig();

	if (InArgs._InitialTarget.IsValid())
	{
		Targets.Add(InArgs._InitialTarget);
		bPinTarget = true;   // opened from a specific camera: start on it and stay there
	}
	else
	{
		HandleEditorSelectionChanged(nullptr);
	}

	SelectionChangedHandle = USelection::SelectionChangedEvent.AddSP(
		this, &SDynamicLensPresetBrowser::HandleEditorSelectionChanged);

	RebuildRows();

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildToolbar()
		]

		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot().Value(0.22f)
			[
				BuildFilterRail()
			]

			+ SSplitter::Slot().Value(0.48f)
			[
				BuildList()
			]

			+ SSplitter::Slot().Value(0.30f)
			[
				BuildDetailPane()
			]
		]
	];
}

SDynamicLensPresetBrowser::~SDynamicLensPresetBrowser()
{
	if (Catalog.IsValid()) Catalog->OnChanged.Remove(CatalogChangedHandle);
	USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
	SaveConfig();
}

// -------------------------------------------------------------------------------------- toolbar

TSharedRef<SWidget> SDynamicLensPresetBrowser::BuildToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(4.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SAssignNew(SearchBox, SSearchBox)
					.HintText(LOCTEXT("SearchHint", "Search lenses, makers, notes..."))
					.OnTextChanged(this, &SDynamicLensPresetBrowser::HandleSearchChanged)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.ContentPadding(FMargin(6.f, 2.f))
					.OnGetMenuContent(this, &SDynamicLensPresetBrowser::BuildSortMenu)
					.ButtonContent()
					[
						SNew(STextBlock).Font(LightFont(9))
						.Text_Lambda([this]()
						{
							FText Name;
							switch (Sort)
							{
							case EDynamicLensSort::Label:       Name = LOCTEXT("SortLabel", "Lens name"); break;
							case EDynamicLensSort::Family:      Name = LOCTEXT("SortFamily", "Maker"); break;
							case EDynamicLensSort::FocalLength: Name = LOCTEXT("SortFocal", "Focal length"); break;
							case EDynamicLensSort::Distortion:  Name = LOCTEXT("SortDistortion", "Distortion"); break;
							case EDynamicLensSort::Aperture:    Name = LOCTEXT("SortAperture", "Max aperture"); break;
							case EDynamicLensSort::Recent:      Name = LOCTEXT("SortRecent", "Recently used"); break;
							default:                            Name = LOCTEXT("SortName", "Asset name"); break;
							}
							return FText::Format(LOCTEXT("SortFmt", "Sort: {0} {1}"), Name,
								FText::FromString(bSortAscending ? TEXT("▲") : TEXT("▼")));
						})
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.ContentPadding(FMargin(6.f, 2.f))
					.OnGetMenuContent(this, &SDynamicLensPresetBrowser::BuildGroupMenu)
					.ButtonContent()
					[
						SNew(STextBlock).Font(LightFont(9))
						.Text_Lambda([this]()
						{
							FText Name;
							switch (Group)
							{
							case EDynamicLensGroup::Family:   Name = LOCTEXT("GroupFamily", "Maker"); break;
							case EDynamicLensGroup::Optics:   Name = LOCTEXT("GroupOptics", "Optics"); break;
							case EDynamicLensGroup::DataType: Name = LOCTEXT("GroupData", "Data type"); break;
							default:                          Name = LOCTEXT("GroupNone", "None"); break;
							}
							return FText::Format(LOCTEXT("GroupFmt", "Group: {0}"), Name);
						})
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.Padding(FMargin(5.f, 1.f))
					.ToolTipText(LOCTEXT("PinTip", "Pin to this camera. Off, the browser follows whatever you select in the level."))
					.IsChecked_Lambda([this]() { return bPinTarget ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
					{
						bPinTarget = (S == ECheckBoxState::Checked);
						if (!bPinTarget) HandleEditorSelectionChanged(nullptr);
					})
					[
						SNew(STextBlock).Font(LightFont(8)).Text(LOCTEXT("Pin", "Pin"))
					]
				]

				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(LightFont(8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(this, &SDynamicLensPresetBrowser::GetTargetText)
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(LightFont(8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]()
					{
						const int32 NumHidden = Catalog->NumHidden();
						if (NumHidden == 0)
						{
							return FText::Format(LOCTEXT("CountFmt", "{0} of {1}"),
								FText::AsNumber(ShownCount), FText::AsNumber(Catalog->GetAll().Num()));
						}
						return FText::Format(LOCTEXT("CountHiddenFmt", "{0} of {1}  ·  {2} hidden"),
							FText::AsNumber(ShownCount), FText::AsNumber(Catalog->GetAll().Num()),
							FText::AsNumber(NumHidden));
					})
				]
			]
		];
}

TSharedRef<SWidget> SDynamicLensPresetBrowser::BuildSortMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	auto Add = [&](EDynamicLensSort InSort, const FText& Label)
	{
		MenuBuilder.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, InSort]()
				{
					if (Sort == InSort) bSortAscending = !bSortAscending;
					else { Sort = InSort; bSortAscending = true; }
					RebuildRows();
					SaveConfig();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, InSort]() { return Sort == InSort; })),
			NAME_None, EUserInterfaceActionType::RadioButton);
	};

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("SortBy", "Sort by"));
	Add(EDynamicLensSort::Name,        LOCTEXT("SortName", "Asset name"));
	Add(EDynamicLensSort::Label,       LOCTEXT("SortLabel", "Lens name"));
	Add(EDynamicLensSort::Family,      LOCTEXT("SortFamily", "Maker"));
	Add(EDynamicLensSort::FocalLength, LOCTEXT("SortFocal", "Focal length"));
	Add(EDynamicLensSort::Aperture,    LOCTEXT("SortAperture", "Max aperture"));
	Add(EDynamicLensSort::Distortion,  LOCTEXT("SortDistortion", "Distortion"));
	Add(EDynamicLensSort::Recent,      LOCTEXT("SortRecent", "Recently used"));
	MenuBuilder.EndSection();

	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(
		LOCTEXT("SortReverse", "Reverse order"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			bSortAscending = !bSortAscending;
			RebuildRows();
			SaveConfig();
		})));

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SDynamicLensPresetBrowser::BuildGroupMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	auto Add = [&](EDynamicLensGroup InGroup, const FText& Label, const FText& Tip)
	{
		MenuBuilder.AddMenuEntry(Label, Tip, FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, InGroup]()
				{
					Group = InGroup;
					RebuildRows();
					SaveConfig();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, InGroup]() { return Group == InGroup; })),
			NAME_None, EUserInterfaceActionType::RadioButton);
	};

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("GroupBy", "Group by"));
	Add(EDynamicLensGroup::None,   LOCTEXT("GroupNone", "Nothing (flat list)"), FText::GetEmpty());
	Add(EDynamicLensGroup::Family, LOCTEXT("GroupFamily", "Maker"),
		LOCTEXT("GroupFamilyTip", "Andy Davis, tiedtke, Lanthimos films, custom."));
	Add(EDynamicLensGroup::Optics, LOCTEXT("GroupOptics", "Spherical / anamorphic"),
		LOCTEXT("GroupOpticsTip", "Puts every anamorphic together, whoever measured it."));
	Add(EDynamicLensGroup::DataType, LOCTEXT("GroupData", "Data type"),
		LOCTEXT("GroupDataTip", "Parametric (zoomable), ST map (measured primes), projection."));
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

// ---------------------------------------------------------------------------------- filter rail

TSharedRef<SWidget> SDynamicLensPresetBrowser::FilterCheck(
	const FText& Label, TFunction<bool()> Get, TFunction<void(bool)> Set, const FText& Tooltip)
{
	return SNew(SCheckBox)
		.ToolTipText(Tooltip)
		.IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Set](ECheckBoxState S)
		{
			Set(S == ECheckBoxState::Checked);
			RebuildRows();
			SaveConfig();
		})
		[
			SNew(STextBlock).Font(LightFont(9)).Text(Label).Margin(FMargin(3.f, 1.f, 0.f, 1.f))
		];
}

TSharedRef<SWidget> SDynamicLensPresetBrowser::TriStateRow(const FText& Label, TOptional<bool>* Target, const FText& Tooltip)
{
	auto MakeButton = [this, Target](const FText& Text, TOptional<bool> Value)
	{
		return SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.Padding(FMargin(5.f, 1.f))
			.IsChecked_Lambda([Target, Value]()
			{
				const bool bMatch = (!Target->IsSet() && !Value.IsSet())
					|| (Target->IsSet() && Value.IsSet() && Target->GetValue() == Value.GetValue());
				return bMatch ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Target, Value](ECheckBoxState)
			{
				*Target = Value;
				RebuildRows();
				SaveConfig();
			})
			[
				SNew(STextBlock).Font(LightFont(8)).Text(Text)
			];
	};

	return SNew(SHorizontalBox)
		.ToolTipText(Tooltip)

		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(LightFont(9)).Text(Label)
		]
		+ SHorizontalBox::Slot().AutoWidth()[ MakeButton(LOCTEXT("Any", "Any"), TOptional<bool>()) ]
		+ SHorizontalBox::Slot().AutoWidth()[ MakeButton(LOCTEXT("Yes", "Yes"), TOptional<bool>(true)) ]
		+ SHorizontalBox::Slot().AutoWidth()[ MakeButton(LOCTEXT("No",  "No"),  TOptional<bool>(false)) ];
}

TSharedRef<SWidget> SDynamicLensPresetBrowser::BuildFilterRail()
{
	int32 Counts[(uint8)EDynamicLensFamilyFilter::Count] = { 0, 0, 0, 0 };
	Catalog->CountByFamily(Counts);

	auto SectionHeading = [](const FText& Text)
	{
		return SNew(STextBlock)
			.Font(BoldFont(8))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(Text)
			.Margin(FMargin(0.f, 8.f, 0.f, 2.f));
	};

	TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox);

	// --- favourites + clear
	Rail->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			FilterCheck(LOCTEXT("FavOnly", "★ Favourites only"),
				[this]() { return Filter.bFavouritesOnly; },
				[this](bool b) { Filter.bFavouritesOnly = b; })
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("ClearTip", "Clear every filter."))
			.Visibility_Lambda([this]() { return Filter.IsDefault() ? EVisibility::Collapsed : EVisibility::Visible; })
			.OnClicked_Lambda([this]()
			{
				Filter.Reset();
				if (SearchBox.IsValid()) SearchBox->SetText(FText::GetEmpty());
				RebuildRows();
				SaveConfig();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Font(LightFont(8)).Text(LOCTEXT("Clear", "Clear"))
			]
		]
	];

	Rail->AddSlot().AutoHeight()
	[
		FilterCheck(LOCTEXT("ShowHidden", "Show hidden inline"),
			[this]() { return Filter.bShowHidden; },
			[this](bool b) { Filter.bShowHidden = b; },
			LOCTEXT("ShowHiddenTip",
				"Put hidden lenses back in the list, dimmed, instead of folding them into the Hidden "
				"section at the bottom. Hiding is yours alone: it never touches the preset asset."))
	];

	// --- maker
	Rail->AddSlot().AutoHeight()[ SectionHeading(LOCTEXT("Maker", "MAKER")) ];
	for (uint8 I = 0; I < (uint8)EDynamicLensFamilyFilter::Count; ++I)
	{
		const FString Key = FDynamicLensPresetCatalog::FamilyKey((EDynamicLensFamilyFilter)I);
		const FText Label = FText::Format(LOCTEXT("FamilyFmt", "{0}  ({1})"),
			FText::FromString(FDynamicLensPresetCatalog::FamilyDisplayName(Key)),
			FText::AsNumber(Counts[I]));
		Rail->AddSlot().AutoHeight()
		[
			FilterCheck(Label,
				[this, I]() { return Filter.bFamily[I]; },
				[this, I](bool b) { Filter.bFamily[I] = b; })
		];
	}

	// --- optics
	Rail->AddSlot().AutoHeight()[ SectionHeading(LOCTEXT("Optics", "OPTICS")) ];
	Rail->AddSlot().AutoHeight()
	[
		FilterCheck(LOCTEXT("Spherical", "Spherical"),
			[this]() { return Filter.bSpherical; },
			[this](bool b) { Filter.bSpherical = b; })
	];
	Rail->AddSlot().AutoHeight()
	[
		FilterCheck(LOCTEXT("Anamorphic", "Anamorphic"),
			[this]() { return Filter.bAnamorphic; },
			[this](bool b) { Filter.bAnamorphic = b; },
			LOCTEXT("AnamorphicTip", "Squeeze above 1, read from the profile's measured data - not from the name."))
	];

	// --- data type
	Rail->AddSlot().AutoHeight()[ SectionHeading(LOCTEXT("DataType", "DISTORTION DATA")) ];
	Rail->AddSlot().AutoHeight()
	[
		FilterCheck(LOCTEXT("Parametric", "Parametric (zoomable)"),
			[this]() { return Filter.bParametric; },
			[this](bool b) { Filter.bParametric = b; },
			LOCTEXT("ParametricTip", "Measured K coefficients on a focal x focus grid. Any focal length, and these breathe."))
	];
	Rail->AddSlot().AutoHeight()
	[
		FilterCheck(LOCTEXT("STMap", "ST map (measured primes)"),
			[this]() { return Filter.bSTMap; },
			[this](bool b) { Filter.bSTMap = b; },
			LOCTEXT("STMapTip", "One measured map per prime. Exact at those focal lengths, single focus, so no breathing."))
	];
	Rail->AddSlot().AutoHeight()
	[
		FilterCheck(LOCTEXT("Projection", "Projection (fisheye maths)"),
			[this]() { return Filter.bProjection; },
			[this](bool b) { Filter.bProjection = b; })
	];

	// --- behaviour
	Rail->AddSlot().AutoHeight()[ SectionHeading(LOCTEXT("Behaviour", "BEHAVIOUR")) ];
	Rail->AddSlot().AutoHeight().Padding(0.f, 1.f)
	[
		TriStateRow(LOCTEXT("Breathes", "Breathes"), &Filter.bBreathes,
			LOCTEXT("BreathesTip", "Distortion changes with focus. Only parametric profiles with a focus stack do."))
	];
	Rail->AddSlot().AutoHeight().Padding(0.f, 1.f)
	[
		TriStateRow(LOCTEXT("ImageCircle", "Image circle"), &Filter.bImageCircle,
			LOCTEXT("ImageCircleTip", "The lens does not cover the whole sensor, so the frame has a black edge."))
	];
	Rail->AddSlot().AutoHeight().Padding(0.f, 1.f)
	[
		TriStateRow(LOCTEXT("Prime", "Prime"), &Filter.bPrime,
			LOCTEXT("PrimeTip", "A single focal length. No means the profile covers a range."))
	];

	// --- ranges
	Rail->AddSlot().AutoHeight()[ SectionHeading(LOCTEXT("Ranges", "RANGES")) ];

	auto RangeSpin = [this](const FText& Label, float* Target, float Min, float Max, const FText& Tip)
	{
		return SNew(SHorizontalBox)
			.ToolTipText(Tip)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(LightFont(9)).Text(Label)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(64.f)
				[
					SNew(SSpinBox<float>)
					.Font(LightFont(8))
					.MinValue(Min).MaxValue(Max)
					.MinSliderValue(Min).MaxSliderValue(Max)
					.Value_Lambda([Target]() { return *Target; })
					.OnValueChanged_Lambda([this, Target](float V) { *Target = V; RebuildRows(); })
					.OnValueCommitted_Lambda([this](float, ETextCommit::Type) { SaveConfig(); })
				]
			];
	};

	Rail->AddSlot().AutoHeight().Padding(0.f, 1.f)
	[
		RangeSpin(LOCTEXT("FocalFrom", "Focal from"), &Filter.FocalMin, 0.f, 500.f,
			LOCTEXT("FocalFromTip", "Keep lenses whose coverage reaches this focal length. 0 = no limit."))
	];
	Rail->AddSlot().AutoHeight().Padding(0.f, 1.f)
	[
		RangeSpin(LOCTEXT("FocalTo", "Focal to"), &Filter.FocalMax, 0.f, 500.f,
			LOCTEXT("FocalToTip", "Keep lenses that reach down to this focal length. 0 = no limit."))
	];
	Rail->AddSlot().AutoHeight().Padding(0.f, 1.f)
	[
		RangeSpin(LOCTEXT("FasterThan", "Opens to T"), &Filter.ApertureMax, 0.f, 22.f,
			LOCTEXT("FasterThanTip", "Keep lenses at least this fast wide open. 0 = no limit."))
	];
	Rail->AddSlot().AutoHeight().Padding(0.f, 1.f)
	[
		RangeSpin(LOCTEXT("MinDistortion", "Min distortion"), &Filter.DistortionMin, 0.f, 0.5f,
			LOCTEXT("MinDistortionTip", "Keep lenses that bend at least this much. 0 = no limit; 0.1 is already a strong look."))
	];

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Rail
			]
		];
}

// ----------------------------------------------------------------------------------------- list

TSharedRef<SWidget> SDynamicLensPresetBrowser::BuildList()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(2.f)
		[
			SAssignNew(ListView, SListView<FDynamicLensBrowserRowPtr>)
			.ListItemsSource(&Rows)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SDynamicLensPresetBrowser::GenerateRow)
			.OnSelectionChanged(this, &SDynamicLensPresetBrowser::HandleSelectionChanged)
		];
}

TSharedRef<ITableRow> SDynamicLensPresetBrowser::GenerateRow(
	FDynamicLensBrowserRowPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	if (!Item.IsValid())
	{
		return SNew(STableRow<FDynamicLensBrowserRowPtr>, Owner)[ SNullWidget::NullWidget ];
	}

	if (Item->IsHeader() && Item->bHiddenSection)
	{
		// the Hidden section folds, so lenses set aside stay out of the way until asked for
		return SNew(STableRow<FDynamicLensBrowserRowPtr>, Owner)
			.Padding(FMargin(0.f, 10.f, 0.f, 2.f))
			.ShowSelection(false)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(0.f))
				.ToolTipText(LOCTEXT("HiddenSectionTip",
					"Lenses you have hidden. They are left out of the list, the component's Preset "
					"dropdown and A1/A2 stepping. Click to show or fold them; the eye on a row unhides it."))
				.OnClicked_Lambda([this]()
				{
					bHiddenExpanded = !bHiddenExpanded;
					RebuildRows();
					SaveConfig();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(STextBlock)
						.Font(LightFont(8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text_Lambda([this]() { return FText::FromString(bHiddenExpanded ? TEXT("▼") : TEXT("▶")); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(BoldFont(9))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(FText::FromString(Item->Header.ToUpper()))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 6.f, 0.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(LightFont(8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(FText::AsNumber(Item->HeaderCount))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SSeparator).Thickness(1.f)
					]
				]
			];
	}

	if (Item->IsHeader())
	{
		return SNew(STableRow<FDynamicLensBrowserRowPtr>, Owner)
			.Padding(FMargin(0.f, 6.f, 0.f, 2.f))
			.ShowSelection(false)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(BoldFont(9))
					.Text(FText::FromString(Item->Header.ToUpper()))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 6.f, 0.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(LightFont(8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::AsNumber(Item->HeaderCount))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SSeparator).Thickness(1.f)
				]
			];
	}

	return SNew(STableRow<FDynamicLensBrowserRowPtr>, Owner)
		.Padding(FMargin(2.f, 3.f))
		.ToolTipText(FText::FromString(
			Item->Entry->Description.IsEmpty() ? Item->Entry->Label : Item->Entry->Description))
		[
			BuildPresetRowContent(Item->Entry)
		];
}

TSharedRef<SWidget> SDynamicLensPresetBrowser::BuildPresetRowContent(FDynamicLensPresetEntryPtr Entry)
{
	TSharedRef<SHorizontalBox> Badges = SNew(SHorizontalBox);

	if (Entry->IsAnamorphic())
	{
		Badges->AddSlot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)
		[
			Badge(FString::Printf(TEXT("%gx ANA"), Entry->Squeeze), FLinearColor(0.35f, 0.3f, 0.75f))
		];
	}
	Badges->AddSlot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)
	[
		Badge(Entry->TypeBadge, FLinearColor(0.28f, 0.28f, 0.32f))
	];
	if (Entry->bBreathes)
	{
		Badges->AddSlot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)
		[
			Badge(TEXT("BREATHES"), FLinearColor(0.2f, 0.5f, 0.35f))
		];
	}
	if (Entry->HasImageCircle())
	{
		Badges->AddSlot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)
		[
			Badge(FString::Printf(TEXT("Ø %.3g"), Entry->ImageCircleMm), FLinearColor(0.45f, 0.35f, 0.2f))
		];
	}

	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox)

		// favourite star
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(1.f))
			.ToolTipText(LOCTEXT("FavTip", "Pin this lens to the top of the list."))
			.OnClicked_Lambda([this, Entry]()
			{
				ToggleFavourite(Entry);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(LightFont(10))
				.ColorAndOpacity_Lambda([this, Entry]()
				{
					return IsFavourite(*Entry)
						? FSlateColor(FLinearColor(1.f, 0.78f, 0.25f))
						: FSlateColor::UseSubduedForeground();
				})
				.Text_Lambda([this, Entry]()
				{
					return FText::FromString(IsFavourite(*Entry) ? TEXT("★") : TEXT("☆"));
				})
			]
		]

		// name + label + badges
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(BoldFont(10))
				.Text(FText::FromString(Entry->DisplayName))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(LightFont(8))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(FString::Printf(TEXT("%s  ·  %s"),
					*Entry->FocalText(), *Entry->FamilyDisplay)))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				Badges
			]
		]

		// distortion bar
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 4.f, 0.f)
		[
			SNew(SBox).WidthOverride(58.f)
			.ToolTipText(FText::Format(
				LOCTEXT("DistortionTip",
					"Curvature {0}: how far this lens departs from straight lines, as a fraction of "
					"half the frame. A clean modern prime is near 0.05, a characterful anamorphic "
					"near 0.13, a big zoom near 0.30. Fisheyes peg the bar."),
				FText::AsNumber(Entry->Distortion)))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(5.f)
					[
						SNew(SProgressBar)
						.Percent(DistortionBarFraction(Entry->Distortion))
						.FillColorAndOpacity(DistortionBarColour(Entry->Distortion))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Font(LightFont(7))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(FString::Printf(TEXT("%.3g"), Entry->Distortion)))
				]
			]
		]

		// hide / unhide
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 2.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.f))
			.ToolTipText_Lambda([this, Entry]()
			{
				return IsHidden(*Entry)
					? LOCTEXT("UnhideTip", "Unhide: put this lens back in the list, the Preset dropdown and A1/A2 stepping.")
					: LOCTEXT("HideTip", "Hide this lens from the list, the Preset dropdown and A1/A2 stepping. "
					                     "Nothing is deleted and only you see the change; it moves to the Hidden section.");
			})
			.OnClicked_Lambda([this, Entry]()
			{
				ToggleHidden(Entry);
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Image_Lambda([this, Entry]()
				{
					return FAppStyle::Get().GetBrush(IsHidden(*Entry) ? "Icons.Hidden" : "Icons.Visible");
				})
			]
		];

	// a hidden lens reads as set aside wherever it shows up
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
		.Padding(0.f)
		.ColorAndOpacity_Lambda([this, Entry]()
		{
			return IsHidden(*Entry) ? FLinearColor(1.f, 1.f, 1.f, 0.45f) : FLinearColor::White;
		})
		[
			Content
		];
}

// --------------------------------------------------------------------------------- detail pane

TSharedRef<SWidget> SDynamicLensPresetBrowser::BuildDetailPane()
{
	auto Field = [this](const FText& Label, TFunction<FString()> Value)
	{
		return SNew(SHorizontalBox)
			.Visibility_Lambda([this, Value]()
			{
				return (Selected.IsValid() && !Value().IsEmpty()) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(84.f)
				[
					SNew(STextBlock)
					.Font(LightFont(8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(Label)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock)
				.Font(LightFont(8))
				.AutoWrapText(true)
				.Text_Lambda([Value]() { return FText::FromString(Value()); })
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Font(BoldFont(12))
					.AutoWrapText(true)
					.Text_Lambda([this]()
					{
						return Selected.IsValid()
							? FText::FromString(Selected->DisplayName)
							: LOCTEXT("NoSelection", "Pick a lens");
					})
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(LightFont(9))
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]()
					{
						return Selected.IsValid()
							? FText::FromString(Selected->Label)
							: LOCTEXT("NoSelectionHint", "Click any lens to put it on the selected camera. The window can stay open.");
					})
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f)
				[
					SNew(SSeparator)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FMaker", "Maker"), [this]() { return Selected.IsValid() ? Selected->FamilyDisplay : FString(); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FCoverage", "Coverage"), [this]() { return Selected.IsValid() ? Selected->FocalText() : FString(); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FMeasured", "Measured"), [this]()
					{
						if (!Selected.IsValid() || Selected->MapCount <= 0) return FString();
						return FString::Printf(TEXT("%d focal length%s"), Selected->MapCount,
							Selected->MapCount == 1 ? TEXT("") : TEXT("s"));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FSensor", "Sensor"), [this]()
					{
						return (Selected.IsValid() && !Selected->SensorMm.IsEmpty())
							? Selected->SensorMm + TEXT(" mm") : FString();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FSqueeze", "Squeeze"), [this]()
					{
						return (Selected.IsValid() && Selected->IsAnamorphic())
							? FString::Printf(TEXT("%gx anamorphic"), Selected->Squeeze) : FString();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FCircle", "Image circle"), [this]()
					{
						return (Selected.IsValid() && Selected->HasImageCircle())
							? FString::Printf(TEXT("%.4g mm"), Selected->ImageCircleMm) : FString();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FAperture", "Wide open"), [this]()
					{
						return (Selected.IsValid() && Selected->MaxAperture > 0.f)
							? FString::Printf(TEXT("T%.3g"), Selected->MaxAperture) : FString();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FBreathing", "Breathing"), [this]()
					{
						if (!Selected.IsValid()) return FString();
						return Selected->bBreathes
							? FString(TEXT("Distortion follows focus (measured focus stack)"))
							: FString(TEXT("Single focus - does not breathe"));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FDistortion", "Distortion"), [this]()
					{
						return Selected.IsValid()
							? FString::Printf(TEXT("%.3g away from straight lines"), Selected->Distortion) : FString();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
				[
					Field(LOCTEXT("FAsset", "Asset"), [this]() { return Selected.IsValid() ? Selected->AssetName : FString(); })
				]

				// description
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(LightFont(9))
					.AutoWrapText(true)
					.Visibility_Lambda([this]()
					{
						return (Selected.IsValid() && !Selected->Description.IsEmpty())
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text_Lambda([this]()
					{
						return Selected.IsValid() ? FText::FromString(Selected->Description) : FText::GetEmpty();
					})
				]

				// Attribution, verbatim. Third-party lens data is not ours to relicense, so who
				// measured it travels with the UI rather than living only in NOTICE.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 2.f)
				[
					SNew(STextBlock)
					.Font(BoldFont(8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(LOCTEXT("SourceHeading", "SOURCE"))
					.Visibility_Lambda([this]()
					{
						return (Selected.IsValid() && !Selected->Source.IsEmpty())
							? EVisibility::Visible : EVisibility::Collapsed;
					})
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Font(LightFont(8))
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]()
					{
						return Selected.IsValid() ? FText::FromString(Selected->Source) : FText::GetEmpty();
					})
				]
			]
		];
}

// ----------------------------------------------------------------------------------------- data

void SDynamicLensPresetBrowser::RebuildRows()
{
	Rows.Reset();

	TArray<FDynamicLensPresetEntryPtr> HiddenView;
	const TArray<FDynamicLensPresetEntryPtr> View =
		Catalog->BuildView(Filter, Sort, bSortAscending, Favourites, Recents, &HiddenView);
	ShownCount = View.Num();

	auto GroupKeyOf = [this](const FDynamicLensPresetEntry& E) -> FString
	{
		switch (Group)
		{
		case EDynamicLensGroup::Family:   return E.FamilyDisplay;
		case EDynamicLensGroup::Optics:   return E.IsAnamorphic() ? TEXT("Anamorphic") : TEXT("Spherical");
		case EDynamicLensGroup::DataType:
			return E.Type == EDynamicLensProfileType::STMap      ? TEXT("ST map - measured primes")
			     : E.Type == EDynamicLensProfileType::Projection ? TEXT("Projection - fisheye maths")
			                                                     : TEXT("Parametric - zoomable");
		default: return FString();
		}
	};

	if (Group == EDynamicLensGroup::None)
	{
		for (const FDynamicLensPresetEntryPtr& E : View) Rows.Add(FDynamicLensBrowserRow::MakeEntry(E));
	}
	else
	{
		// stable group order: first appearance in the already-sorted view, so the sort still leads
		TArray<FString> Order;
		TMap<FString, TArray<FDynamicLensPresetEntryPtr>> Buckets;
		for (const FDynamicLensPresetEntryPtr& E : View)
		{
			const FString Key = GroupKeyOf(*E);
			if (!Buckets.Contains(Key)) Order.Add(Key);
			Buckets.FindOrAdd(Key).Add(E);
		}
		for (const FString& Key : Order)
		{
			const TArray<FDynamicLensPresetEntryPtr>& Bucket = Buckets[Key];
			Rows.Add(FDynamicLensBrowserRow::MakeHeader(Key, Bucket.Num()));
			for (const FDynamicLensPresetEntryPtr& E : Bucket) Rows.Add(FDynamicLensBrowserRow::MakeEntry(E));
		}
	}

	// hidden lenses that pass every other filter, folded into one section at the foot of the list
	if (HiddenView.Num() > 0)
	{
		TSharedRef<FDynamicLensBrowserRow> Header =
			FDynamicLensBrowserRow::MakeHeader(TEXT("Hidden"), HiddenView.Num());
		Header->bHiddenSection = true;
		Rows.Add(Header);
		if (bHiddenExpanded)
		{
			for (const FDynamicLensPresetEntryPtr& E : HiddenView) Rows.Add(FDynamicLensBrowserRow::MakeEntry(E));
		}
	}

	if (ListView.IsValid()) ListView->RequestListRefresh();
}

void SDynamicLensPresetBrowser::HandleCatalogChanged()
{
	RebuildRows();
}

void SDynamicLensPresetBrowser::HandleSearchChanged(const FText& Text)
{
	Filter.Search = Text.ToString();
	RebuildRows();
}

void SDynamicLensPresetBrowser::HandleSelectionChanged(FDynamicLensBrowserRowPtr Item, ESelectInfo::Type SelectInfo)
{
	// Direct means we set the selection ourselves; applying then would be a loop
	if (SelectInfo == ESelectInfo::Direct) return;
	if (!Item.IsValid() || Item->IsHeader()) return;

	Selected = Item->Entry;
	ApplyEntry(Item->Entry);
}

void SDynamicLensPresetBrowser::ApplyEntry(FDynamicLensPresetEntryPtr Entry)
{
	if (!Entry.IsValid()) return;

	const TArray<UDynamicLensComponent*> Found = ResolveTargets();
	if (Found.Num() == 0) return;

	// The one place anything is loaded. Browsing stays free; putting a lens on a camera costs
	// what that lens costs (an ST-map series pulls in its textures here, not before).
	UDynamicLensPreset* Preset = Cast<UDynamicLensPreset>(Entry->Asset.GetAsset());
	if (!Preset) return;

	FScopedTransaction Transaction(LOCTEXT("ApplyPresetTransaction", "Apply Dynamic Lens Preset"));
	bool bAnyApplied = false;
	for (UDynamicLensComponent* Component : Found)
	{
		if (Component->ApplyPreset(Preset)) bAnyApplied = true;
	}

	if (bAnyApplied)
	{
		PushRecent(Entry->Asset.PackageName);
		SaveConfig();
	}
	else
	{
		// nothing changed (already on this lens): don't leave an empty undo step behind
		Transaction.Cancel();
	}
}

// -------------------------------------------------------------------------------------- targets

void SDynamicLensPresetBrowser::SetTarget(TWeakObjectPtr<UDynamicLensComponent> InTarget)
{
	if (!InTarget.IsValid()) return;
	Targets.Reset();
	Targets.Add(InTarget);
	bPinTarget = true;
}

void SDynamicLensPresetBrowser::HandleEditorSelectionChanged(UObject*)
{
	if (bPinTarget) return;
	if (!GEditor) return;

	TArray<TWeakObjectPtr<UDynamicLensComponent>> Found;
	if (USelection* SelectedActors = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				if (UDynamicLensComponent* Component = Actor->FindComponentByClass<UDynamicLensComponent>())
				{
					Found.Add(Component);
				}
			}
		}
	}

	// A selection with no Dynamic Lens on it is usually someone clicking a light on the way back
	// to their camera. Keep the last good target rather than going dead.
	if (Found.Num() > 0) Targets = Found;
}

TArray<UDynamicLensComponent*> SDynamicLensPresetBrowser::ResolveTargets() const
{
	TArray<UDynamicLensComponent*> Out;
	for (const TWeakObjectPtr<UDynamicLensComponent>& Weak : Targets)
	{
		if (UDynamicLensComponent* Component = Weak.Get()) Out.Add(Component);
	}
	return Out;
}

FText SDynamicLensPresetBrowser::GetTargetText() const
{
	const TArray<UDynamicLensComponent*> Found = ResolveTargets();
	if (Found.Num() == 0)
	{
		return LOCTEXT("NoTarget", "No camera selected - select one with a Dynamic Lens component");
	}
	if (Found.Num() == 1)
	{
		const AActor* Owner = Found[0]->GetOwner();
		return FText::Format(LOCTEXT("OneTarget", "→ {0}"),
			FText::FromString(Owner ? Owner->GetActorNameOrLabel() : TEXT("(component)")));
	}
	return FText::Format(LOCTEXT("ManyTargets", "→ {0} cameras"), FText::AsNumber(Found.Num()));
}

// ------------------------------------------------------------------- favourites, recents, config

bool SDynamicLensPresetBrowser::IsFavourite(const FDynamicLensPresetEntry& E) const
{
	return Favourites.Contains(E.Asset.PackageName);
}

void SDynamicLensPresetBrowser::ToggleFavourite(FDynamicLensPresetEntryPtr Entry)
{
	if (!Entry.IsValid()) return;
	const FName Key = Entry->Asset.PackageName;
	if (Favourites.Contains(Key)) Favourites.Remove(Key);
	else Favourites.Add(Key);
	SaveConfig();
	if (Filter.bFavouritesOnly) RebuildRows();
}

bool SDynamicLensPresetBrowser::IsHidden(const FDynamicLensPresetEntry& E) const
{
	return Catalog.IsValid() && Catalog->IsHidden(E);
}

void SDynamicLensPresetBrowser::ToggleHidden(FDynamicLensPresetEntryPtr Entry)
{
	if (!Entry.IsValid() || !Catalog.IsValid()) return;
	// the catalogue saves the set and broadcasts, so every open tab rebuilds, this one included
	Catalog->SetHidden(Entry->Asset.PackageName, !Catalog->IsHidden(*Entry));
}

void SDynamicLensPresetBrowser::PushRecent(FName PackageName)
{
	Recents.Remove(PackageName);
	Recents.Insert(PackageName, 0);
	const int32 MaxRecents = 24;
	if (Recents.Num() > MaxRecents) Recents.SetNum(MaxRecents);
	if (Sort == EDynamicLensSort::Recent) RebuildRows();
}

void SDynamicLensPresetBrowser::SaveConfig() const
{
	if (!GConfig) return;
	const FString& Ini = GEditorPerProjectIni;

	TArray<FString> FavStrings;
	for (const FName& F : Favourites) FavStrings.Add(F.ToString());
	GConfig->SetArray(GConfigSection, TEXT("Favourites"), FavStrings, Ini);

	TArray<FString> RecentStrings;
	for (const FName& R : Recents) RecentStrings.Add(R.ToString());
	GConfig->SetArray(GConfigSection, TEXT("Recents"), RecentStrings, Ini);

	GConfig->SetInt(GConfigSection, TEXT("Sort"), (int32)Sort, Ini);
	GConfig->SetInt(GConfigSection, TEXT("Group"), (int32)Group, Ini);
	GConfig->SetBool(GConfigSection, TEXT("SortAscending"), bSortAscending, Ini);

	for (uint8 I = 0; I < (uint8)EDynamicLensFamilyFilter::Count; ++I)
	{
		GConfig->SetBool(GConfigSection, *FString::Printf(TEXT("Family%d"), I), Filter.bFamily[I], Ini);
	}
	GConfig->SetBool(GConfigSection, TEXT("Spherical"), Filter.bSpherical, Ini);
	GConfig->SetBool(GConfigSection, TEXT("Anamorphic"), Filter.bAnamorphic, Ini);
	GConfig->SetBool(GConfigSection, TEXT("Parametric"), Filter.bParametric, Ini);
	GConfig->SetBool(GConfigSection, TEXT("STMap"), Filter.bSTMap, Ini);
	GConfig->SetBool(GConfigSection, TEXT("Projection"), Filter.bProjection, Ini);
	GConfig->SetBool(GConfigSection, TEXT("FavouritesOnly"), Filter.bFavouritesOnly, Ini);
	GConfig->SetBool(GConfigSection, TEXT("ShowHidden"), Filter.bShowHidden, Ini);
	GConfig->SetBool(GConfigSection, TEXT("HiddenExpanded"), bHiddenExpanded, Ini);
	GConfig->SetFloat(GConfigSection, TEXT("FocalMin"), Filter.FocalMin, Ini);
	GConfig->SetFloat(GConfigSection, TEXT("FocalMax"), Filter.FocalMax, Ini);
	GConfig->SetFloat(GConfigSection, TEXT("ApertureMax"), Filter.ApertureMax, Ini);
	GConfig->SetFloat(GConfigSection, TEXT("DistortionMin"), Filter.DistortionMin, Ini);
}

void SDynamicLensPresetBrowser::LoadConfig()
{
	if (!GConfig) return;
	const FString& Ini = GEditorPerProjectIni;

	TArray<FString> FavStrings;
	GConfig->GetArray(GConfigSection, TEXT("Favourites"), FavStrings, Ini);
	for (const FString& S : FavStrings) Favourites.Add(FName(*S));

	TArray<FString> RecentStrings;
	GConfig->GetArray(GConfigSection, TEXT("Recents"), RecentStrings, Ini);
	for (const FString& S : RecentStrings) Recents.Add(FName(*S));

	int32 IntValue = 0;
	if (GConfig->GetInt(GConfigSection, TEXT("Sort"), IntValue, Ini)
		&& IntValue >= 0 && IntValue < (int32)EDynamicLensSort::Count)
	{
		Sort = (EDynamicLensSort)IntValue;
	}
	if (GConfig->GetInt(GConfigSection, TEXT("Group"), IntValue, Ini)
		&& IntValue >= 0 && IntValue < (int32)EDynamicLensGroup::Count)
	{
		Group = (EDynamicLensGroup)IntValue;
	}
	GConfig->GetBool(GConfigSection, TEXT("SortAscending"), bSortAscending, Ini);

	for (uint8 I = 0; I < (uint8)EDynamicLensFamilyFilter::Count; ++I)
	{
		GConfig->GetBool(GConfigSection, *FString::Printf(TEXT("Family%d"), I), Filter.bFamily[I], Ini);
	}
	GConfig->GetBool(GConfigSection, TEXT("Spherical"), Filter.bSpherical, Ini);
	GConfig->GetBool(GConfigSection, TEXT("Anamorphic"), Filter.bAnamorphic, Ini);
	GConfig->GetBool(GConfigSection, TEXT("Parametric"), Filter.bParametric, Ini);
	GConfig->GetBool(GConfigSection, TEXT("STMap"), Filter.bSTMap, Ini);
	GConfig->GetBool(GConfigSection, TEXT("Projection"), Filter.bProjection, Ini);
	GConfig->GetBool(GConfigSection, TEXT("FavouritesOnly"), Filter.bFavouritesOnly, Ini);
	GConfig->GetBool(GConfigSection, TEXT("ShowHidden"), Filter.bShowHidden, Ini);
	GConfig->GetBool(GConfigSection, TEXT("HiddenExpanded"), bHiddenExpanded, Ini);
	GConfig->GetFloat(GConfigSection, TEXT("FocalMin"), Filter.FocalMin, Ini);
	GConfig->GetFloat(GConfigSection, TEXT("FocalMax"), Filter.FocalMax, Ini);
	GConfig->GetFloat(GConfigSection, TEXT("ApertureMax"), Filter.ApertureMax, Ini);
	GConfig->GetFloat(GConfigSection, TEXT("DistortionMin"), Filter.DistortionMin, Ini);
}

#undef LOCTEXT_NAMESPACE
