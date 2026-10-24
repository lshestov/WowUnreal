using UnrealBuildTool;
using System.IO;

public class WowData : ModuleRules
{
    public WowData(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject" });
        string ThirdParty = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty"));
        PrivateIncludePaths.Add(Path.Combine(ThirdParty, "StormLib", "src"));
        PrivateDefinitions.Add("__STORMLIB_NO_STATIC_LINK__=1");
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string Library = Path.Combine(ThirdParty, "Windows", "x64", "StormLib.lib");
            if (!File.Exists(Library)) throw new BuildException("Build StormLib for Win64; see BuildSupport/Windows/README.md.");
            PublicAdditionalLibraries.Add(Library);
            PublicSystemLibraries.Add("wininet.lib");
        }
        else
        {
            PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "StormLib", "build", "libstorm.a"));
            PublicAdditionalLibraries.AddRange(new string[] { "bz2", "z" });
        }
    }
}
