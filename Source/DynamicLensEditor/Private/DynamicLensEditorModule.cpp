// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#include "DynamicLensEditorModule.h"

#include "DynamicLensComponent.h"
#include "DynamicLensComponentDetails.h"
#include "SDynamicLensPresetBrowser.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "DynamicLensEditor"

IMPLEMENT_MODULE(FDynamicLensEditorModule, DynamicLensEditor)

namespace
{
	/**
	 * The live browser widget, so a second Browse click re-aims the open tab instead of doing
	 * nothing. Weak: the tab owns it, and closing the tab must free it.
	 */
	TWeakPtr<SDynamicLensPresetBrowser> GOpenBrowser;

	/** Target handed over by OpenPresetBrowser, consumed when the tab spawns. */
	TWeakObjectPtr<UDynamicLensComponent> GPendingTarget;
}

void FDynamicLensEditorModule::StartupModule()
{
	RegisterTabSpawner();

	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		UDynamicLensComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDynamicLensComponentDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}

void FDynamicLensEditorModule::ShutdownModule()
{
	UnregisterTabSpawner();

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UDynamicLensComponent::StaticClass()->GetFName());
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

void FDynamicLensEditorModule::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(
			SDynamicLensPresetBrowser::TabId,
			FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
			{
				TSharedRef<SDynamicLensPresetBrowser> Browser =
					SNew(SDynamicLensPresetBrowser).InitialTarget(GPendingTarget);
				GPendingTarget = nullptr;
				GOpenBrowser = Browser;

				return SNew(SDockTab)
					.TabRole(ETabRole::NomadTab)
					.Label(LOCTEXT("TabLabel", "Lens Presets"))
					[
						Browser
					];
			}))
		.SetDisplayName(LOCTEXT("TabTitle", "Dynamic Lens Preset Browser"))
		.SetTooltipText(LOCTEXT("TabTooltip",
			"Browse every Dynamic Lens preset with filters and sorting, and apply one to the "
			"selected camera with a single click."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent"));
}

void FDynamicLensEditorModule::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SDynamicLensPresetBrowser::TabId);
}

void FDynamicLensEditorModule::OpenPresetBrowser(TWeakObjectPtr<UDynamicLensComponent> Target)
{
	// re-aim an open tab rather than spawning a second one
	if (TSharedPtr<SDynamicLensPresetBrowser> Existing = GOpenBrowser.Pin())
	{
		Existing->SetTarget(Target);
	}
	else
	{
		GPendingTarget = Target;
	}

	FGlobalTabmanager::Get()->TryInvokeTab(FTabId(SDynamicLensPresetBrowser::TabId));
}

#undef LOCTEXT_NAMESPACE
