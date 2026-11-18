using UnrealBuildTool;

public class ArdaUnrealEditor : ModuleRules
{
	public ArdaUnrealEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "ArdaUnrealRuntime", "UnrealEd" });
	}
}
