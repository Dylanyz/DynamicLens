// Copyright (c) 2026 Dylan Gitalis. Source-available under CPAL-1.0 with the Commons Clause; see LICENSE.
// SPDX-License-Identifier: CPAL-1.0 AND LicenseRef-Commons-Clause-1.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

using UnrealBuildTool;

public class DynamicLensEditor : ModuleRules
{
	public DynamicLensEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"EditorFramework",
			"EditorStyle",
			"PropertyEditor",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"AssetRegistry",
			"CinematicCamera",
			"DynamicLens",
		});
	}
}
