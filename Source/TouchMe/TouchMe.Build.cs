// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TouchMe : ModuleRules
{
	public TouchMe(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "PhysicsCore", "UMG" });

		PrivateDependencyModuleNames.AddRange(new string[] { "AudioMixer", "AudioSynesthesia", "AudioSynesthesiaCore", "Niagara", "PakFile", "Slate", "SlateCore" });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"AnimGraph",
				"AnimGraphRuntime",
				"AssetRegistry",
				"BlueprintGraph",
				"ImageWrapper",
				"Kismet",
				"UMGEditor",
				"UnrealEd"
			});
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
