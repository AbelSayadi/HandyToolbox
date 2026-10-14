// Copyright NiagaraParamExposer. All Rights Reserved.

using UnrealBuildTool;

public class NiagaraParamExposer : ModuleRules
{
	public NiagaraParamExposer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Niagara",
				"NiagaraCore",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorStyle",
				"ToolMenus",
				"WorkspaceMenuStructure",
				"ContentBrowser",
				"EditorWidgets",
				"AssetTools",
				"PropertyEditor",
				"NiagaraEditor",
				"DataHierarchyEditor",
				"NiagaraShader",
				"GraphEditor",
				"Kismet",
				"Projects",
				"ApplicationCore",
			});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("EditorSubsystem");
		}
	}
}
