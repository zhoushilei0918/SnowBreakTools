using UnrealBuildTool;

public class FModelAnimRestore : ModuleRules
{
    public FModelAnimRestore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "AnimGraph", "AnimGraphRuntime", "BlueprintGraph", "Kismet", "KismetCompiler",
            "AnimationDataController", "AssetRegistry", "AssetTools", "Json", "JsonUtilities",
            "KawaiiPhysics", "KawaiiPhysicsEd", "Slate", "SlateCore", "ToolMenus",
            "PropertyEditor", "InputCore", "ContentBrowser", "Projects", "DesktopWidgets", "DesktopPlatform",
            "AnimationBlueprintEditor", "Persona", "UnrealPSKPSA"
        });
    }
}
