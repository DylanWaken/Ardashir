using System.IO;
using UnrealBuildTool;

// Shared ABI contract for the Ardashir DLL and its Unreal consumers.
public class ArdaHeaders : ModuleRules
{
	public ArdaHeaders(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		string Root = Path.GetFullPath(Path.Combine(ModuleDirectory, "../.."));
		string Ardashir = Path.Combine(Root, "ThirdParty", "Ardashir");
		string Build = Path.Combine(Root, "Intermediate", "Ardashir", "Win64");
		PublicSystemIncludePaths.AddRange(new string[] {
			Path.Combine(Ardashir, "Source", "ArdaInfra", "ArdaBackend", "Public"),
			Path.Combine(Ardashir, "Source", "ArdaInfra", "ArdaMath", "Public"),
			Path.Combine(Ardashir, "Source", "ArdaInfra", "ArdaGraph", "Public"),
			Path.Combine(Ardashir, "Source", "ArdaInfra", "ArdaDependencyGraph", "Public"),
			Path.Combine(Ardashir, "Source", "ArdaTrace", "Public"),
			Path.Combine(Ardashir, "ThirdParty", "EASTL", "include"),
			Path.Combine(Ardashir, "ThirdParty", "Eigen"),
			Path.Combine(Ardashir, "Cmake")
		});
		string NativeCompute = Path.Combine(Ardashir, "Source", "ArdaInfra", "ArdaNativeCompute", "Public");
		if (Directory.Exists(NativeCompute))
		{
			PublicSystemIncludePaths.Add(NativeCompute);
		}
		string EABaseIncludeFile = Path.Combine(Build, "ArdaEABaseInclude.txt");
		if (File.Exists(EABaseIncludeFile))
		{
			PublicSystemIncludePaths.Add(File.ReadAllText(EABaseIncludeFile).Trim());
		}
		else
		{
			throw new BuildException("Missing Ardashir headers manifest. Run python \"{0}\" before UnrealBuildTool.",
				Path.Combine(Root, "Scripts", "ArdaBuild.py"));
		}
		ExternalDependencies.Add(EABaseIncludeFile);
		PublicDefinitions.AddRange(new string[] {
			"EASTL_USER_CONFIG_HEADER=\"ArdaEASTLConfig.h\"",
			"EASTL_STD_ITERATOR_CATEGORY_ENABLED=1",
			"EASTL_DEBUG=0",
			"_ITERATOR_DEBUG_LEVEL=0",
			"EASTL_OPENSOURCE=1",
			"EASTL_DLL=1",
			"EIGEN_DEFAULT_TO_ROW_MAJOR",
			"EIGEN_MAX_ALIGN_BYTES=16",
			"EIGEN_MAX_STATIC_ALIGN_BYTES=16",
			"ARDASHIR_ENABLE_TRACE=0"
		});
	}
}
