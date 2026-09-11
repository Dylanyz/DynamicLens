// Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

using UnrealBuildTool;

public class DynamicLens : ModuleRules
{
	public DynamicLens(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"CinematicCamera",
			"CameraCalibrationCore",
			"AssetRegistry",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
		});
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("PythonScriptPlugin");
		}
	}
}
