using System.IO;
using EpicGames.Core;
using UnrealBuildTool;

public class ArdaExternal : ModuleRules
{
	public ArdaExternal(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		PublicDependencyModuleNames.Add("ArdaHeaders");
		if (Target.Platform != UnrealTargetPlatform.Win64 || Target.Architecture != UnrealArch.X64)
		{
			throw new BuildException("ArdashirUnreal currently supports Win64 x64 only.");
		}
		if (Target.bDebugBuildsActuallyUseDebugCRT)
		{
			throw new BuildException("ArdashirUnreal uses the release MSVC CRT (/MD), as do standard Unreal targets.");
		}
		string Root = Path.GetFullPath(Path.Combine(ModuleDirectory, "../.."));
		string Build = Path.Combine(Root, "Intermediate", "Ardashir", "Win64");
		foreach (string Library in new string[] { "ArdaUnrealCore" })
		{
			string FileName = Path.Combine(Build, "lib", "RelWithDebInfo", Library + ".lib");
			if (!File.Exists(FileName))
			{
				throw new BuildException("Missing {0}. Run: python \"{1}\"", FileName,
					Path.Combine(Root, "Scripts", "ArdaBuild.py"));
			}
			PublicAdditionalLibraries.Add(FileName);
			ExternalDependencies.Add(FileName);
		}
		string ManifestFile = Path.Combine(Build, "ArdaBuildManifest.json");
		string LockFile = Path.Combine(Root, "ArdaRevision.json");
		if (!File.Exists(ManifestFile))
		{
			throw new BuildException("Ardashir library build is incomplete. Run Scripts/ArdaBuild.py again.");
		}
		JsonObject Manifest = JsonObject.Read(new FileReference(ManifestFile));
		JsonObject Revision = JsonObject.Read(new FileReference(LockFile));
		string ExpectedRevision = Revision.GetStringField("revision");
		string CheckoutRevision = ReadCheckoutRevision(Path.Combine(Root, "ThirdParty", "Ardashir", ".git"));
		if (Manifest.GetStringField("revision") != ExpectedRevision || CheckoutRevision != ExpectedRevision)
		{
			throw new BuildException("Ardashir binaries/headers do not match ArdaRevision.json. Run Scripts/ArdaBuild.py again.");
		}
		RuntimeDependencies.Add("$(TargetOutputDir)/ArdaUnrealCore.dll",
			Path.Combine(Build, "bin", "RelWithDebInfo", "ArdaUnrealCore.dll"));
		ExternalDependencies.Add(ManifestFile);
		ExternalDependencies.Add(LockFile);
	}

	private static string ReadCheckoutRevision(string GitDirectory)
	{
		string Head = File.ReadAllText(Path.Combine(GitDirectory, "HEAD")).Trim();
		if (!Head.StartsWith("ref: "))
		{
			return Head;
		}
		string Reference = Head.Substring(5);
		string LooseReference = Path.Combine(GitDirectory, Reference.Replace('/', Path.DirectorySeparatorChar));
		if (File.Exists(LooseReference))
		{
			return File.ReadAllText(LooseReference).Trim();
		}
		string PackedReferences = Path.Combine(GitDirectory, "packed-refs");
		if (File.Exists(PackedReferences))
		{
			foreach (string Line in File.ReadLines(PackedReferences))
			{
				if (Line.EndsWith(" " + Reference))
				{
					return Line.Split(' ')[0];
				}
			}
		}
		throw new BuildException("Cannot resolve the Ardashir dependency's Git revision.");
	}
}
