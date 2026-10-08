// Copyright (c) 2026 Dylan Gitalis. Source-available under CPAL-1.0 with the Commons Clause; see LICENSE.
// SPDX-License-Identifier: CPAL-1.0 AND LicenseRef-Commons-Clause-1.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

#include "DynamicLensEditorModule.h"

#include "DynamicLensComponent.h"
#include "DynamicLensComponentDetails.h"
#include "SDynamicLensPresetBrowser.h"

#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "PropertyEditorModule.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "SequencerToolMenuContext.h"
#include "IPythonScriptPlugin.h"

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

	/** `DynamicLens.PresetBrowser` in the console (or from Python) opens the browser without a target. */
	FAutoConsoleCommand GOpenBrowserCommand(
		TEXT("DynamicLens.PresetBrowser"),
		TEXT("Open the Dynamic Lens Preset Browser."),
		FConsoleCommandDelegate::CreateLambda([]() { FDynamicLensEditorModule::OpenPresetBrowser(); }));

	/** Ready every lens the focused edit's shots use (dynamiclens_tools.prewarm), so the first cut to each angle is smooth. */
	void Prewarm()
	{
		if (IPythonScriptPlugin* Py = IPythonScriptPlugin::Get())
		{
			Py->ExecPythonCommand(TEXT("import dynamiclens_tools as dl; dl.prewarm()"));
		}
	}

	FAutoConsoleCommand GPrewarmCommand(
		TEXT("DynamicLens.Prewarm"),
		TEXT("Ready every lens the focused Sequencer edit's shots use, before playback."),
		FConsoleCommandDelegate::CreateLambda([]() { Prewarm(); }));

	const FName MenuOwner(TEXT("DynamicLensEditor"));

	void ExtendSequencerToolbar()
	{
		const FToolMenuOwnerScoped Owner(MenuOwner);
		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(TEXT("Sequencer.MainToolBar"));
		Toolbar->AddDynamicSection(TEXT("DynamicLensPrewarm"), FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
		{
			if (!Menu->FindContext<USequencerToolMenuContext>())
			{
				return;
			}
			FToolMenuSection& Section = Menu->AddSection(TEXT("DynamicLensPrewarm"));
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(TEXT("DynamicLensPrewarm"),
				FUIAction(FExecuteAction::CreateLambda([]() { Prewarm(); })),
				LOCTEXT("PrewarmLabel", "Prewarm Lenses"),
				LOCTEXT("PrewarmTip", "Dynamic Lens: ready every lens this edit's shots use, so even the first cut to each angle "
				                      "plays without a stutter or a pop. Lenses stay ready until the editor closes."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent")));
		}));
	}

	FDelegateHandle ToolMenusStartup;
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

	ToolMenusStartup = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&ExtendSequencerToolbar));
}

void FDynamicLensEditorModule::ShutdownModule()
{
	UnregisterTabSpawner();
	UToolMenus::UnRegisterStartupCallback(ToolMenusStartup);
	UToolMenus::UnregisterOwner(MenuOwner);

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
