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
