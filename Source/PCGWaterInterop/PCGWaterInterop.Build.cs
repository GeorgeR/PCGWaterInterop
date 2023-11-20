// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PCGWaterInterop : ModuleRules
{
	public PCGWaterInterop(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", 
				"PCG",
				"Water",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
			});
	}
}
