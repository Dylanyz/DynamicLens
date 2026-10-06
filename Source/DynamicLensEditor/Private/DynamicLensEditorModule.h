// Copyright (c) 2026 Dylan Gitalis. Source-available under CPAL-1.0 with the Commons Clause; see LICENSE.
// SPDX-License-Identifier: CPAL-1.0 AND LicenseRef-Commons-Clause-1.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

// Editor-only half of DynamicLens: the Preset Browser tab and the component's details panel.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class UDynamicLensComponent;

class FDynamicLensEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Show the Preset Browser, optionally aimed at one camera.
	 *
	 * Passing a target pins the browser to it, so opening from a camera's details panel does not
	 * then wander off with the next thing selected. With no target it follows the selection.
	 */
	static void OpenPresetBrowser(TWeakObjectPtr<UDynamicLensComponent> Target = nullptr);

private:
	void RegisterTabSpawner();
	void UnregisterTabSpawner();
};
