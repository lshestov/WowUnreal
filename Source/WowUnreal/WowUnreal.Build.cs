using UnrealBuildTool;

public class WowUnreal : ModuleRules
{
    public WowUnreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] { "Json", "MeshDescription", "StaticMeshDescription", "RHI", "RenderCore" });
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "WowData", "WowAssets", "WowWorld", "WowNetwork", "WowUI", "WowClient", "MediaAssets", "Media" });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }
    }
}
