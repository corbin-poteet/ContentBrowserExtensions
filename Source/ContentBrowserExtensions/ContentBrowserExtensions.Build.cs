// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ContentBrowserExtensions : ModuleRules
{
	public ContentBrowserExtensions(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ContentBrowserData",   // base UContentBrowserDataSource lives here (per D-01 + RESEARCH Q1)
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"EditorFramework",
				"UnrealEd",
				"EditorSubsystem",      // UEditorSubsystem base class (per RESEARCH Q2)
				"ContentBrowser",       // IContentBrowserDataModule for subsystem ActivateDataSource
				"AssetRegistry",        // FAssetData / IAssetRegistry (used by base class default impls)
				"ToolMenus",
				"Projects",
			}
		);
	}
}
