using UnrealBuildTool;
using System.IO;

public class WowUI : ModuleRules
{
    public WowUI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UMG", "Slate", "SlateCore", "WowData", "WowAssets", "WowNetwork", "XmlParser", "InputCore" });
        PrivateDependencyModuleNames.Add("WowWorld");
        PrivateDependencyModuleNames.Add("ApplicationCore");
        string ThirdParty = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty"));
        PublicIncludePaths.Add(Path.Combine(ThirdParty, "pugixml"));
        PrivateDefinitions.Add("HAS_PUGIXML=1");
        PublicIncludePaths.Add(Path.Combine(ThirdParty, "lua"));
        string Library = Target.Platform == UnrealTargetPlatform.Win64
            ? Path.Combine(ThirdParty, "Windows", "x64", "lua51.lib")
            : Path.Combine(ThirdParty, "lua", "liblua.a");
        if (!File.Exists(Library)) throw new BuildException("Build Lua 5.1 for the target platform; see README.md and BuildSupport/Windows/README.md.");
        PublicAdditionalLibraries.Add(Library);
    }
}
