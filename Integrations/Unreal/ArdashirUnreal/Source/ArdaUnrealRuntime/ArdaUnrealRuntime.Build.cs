using UnrealBuildTool;

public class ArdaUnrealRuntime : ModuleRules
{
	public ArdaUnrealRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = true;
		bUseRTTI = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "RHI", "ArdaHeaders", "ArdaExternal" });
		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject", "Engine", "RenderCore", "Projects"
		});
	}
}
