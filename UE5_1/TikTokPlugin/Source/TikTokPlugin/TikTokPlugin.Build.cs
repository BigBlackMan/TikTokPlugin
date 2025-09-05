//TikTokPlugin.Build.cs

using System.IO;
using UnrealBuildTool;

public class TikTokPlugin : ModuleRules
{
    public TikTokPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.Add(Path.Combine(EngineDirectory, "Source/ThirdParty/Boost/boost-1_70_0/include"));
        PublicIncludePaths.Add(Path.Combine(EngineDirectory, "Source/ThirdParty/OpenSSL/1.1.1n/include/Win64/VS2015"));

        bEnableExceptions = true;
        bUseUnity = false; // Force separate translation units to avoid macro/namespace collisions in unity builds

        PublicDefinitions.AddRange(new string[] { "NOMINMAX", "WIN32_LEAN_AND_MEAN" });

        PublicIncludePaths.AddRange(
            new string[]
            {
            
            }
        );

        PrivateIncludePaths.AddRange(
            new string[]
            {
                // ... add other private include paths required here ...
            }
        );

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "Boost",
                "Json",
                "JsonUtilities",
                "OpenSSL",
                "GameplayTags"
                // ... add other public dependencies that you statically link with here ...
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "UMG",
                "Projects"
                
                // ... add private dependencies that you statically link with here ...
            }
        );

        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
                // ... add any modules that your module loads dynamically here ...
            }
        );
    }
}