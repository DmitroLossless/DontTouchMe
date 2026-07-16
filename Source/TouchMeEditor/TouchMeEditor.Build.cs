// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TouchMeEditor : ModuleRules
{
	public TouchMeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"ContentBrowser",
			"RenderCore",
			"Slate",
			"SlateCore",
			"UnrealEd"
		});
	}
}
