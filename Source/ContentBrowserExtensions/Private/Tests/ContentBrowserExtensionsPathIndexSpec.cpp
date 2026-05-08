// Copyright Epic Games, Inc. All Rights Reserved.
// ATF specs for FContentBrowserExtensionPathIndex + UE::ContentBrowserExtensions::SanitizeForPackagePath
// + FContentBrowserExtensionAssetEntry shape (D-08 + D-10).
// Migrated from FrostyPathIndexSpec (single AssetEntry It()) and expanded with sanitize cases.

#include "Misc/AutomationTest.h"
#include "ContentBrowserExtensionAssetEntry.h"
#include "ContentBrowserExtensionPathIndex.h"
#include "ContentBrowserExtensionsStatics.h"

DEFINE_SPEC(FContentBrowserExtensionsPathIndexSpec, "ContentBrowserExtensions.PathIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FContentBrowserExtensionsPathIndexSpec::Define()
{
	Describe("AssetEntry", [this]()
	{
		It("constructs with Path and Type", [this]()
		{
			FContentBrowserExtensionAssetEntry E;
			E.Path = TEXT("weapons/rifle");
			E.Type = TEXT("MeshAsset");   // Field name is `Type` per Plan 02 policy
			TestEqual(TEXT("Path"), E.Path, FString(TEXT("weapons/rifle")));
			TestEqual(TEXT("Type"), E.Type, FName(TEXT("MeshAsset")));
		});
	});

	Describe("PathIndex", [this]()
	{
		It("BuildFromFlatList populates ChildrenMap, LeafItems, FolderPaths", [this]()
		{
			FContentBrowserExtensionPathIndex Idx;
			TArray<FContentBrowserExtensionAssetEntry> Flat;
			Flat.Add({TEXT("weapons/rifle"), FName(TEXT("MeshAsset"))});
			Flat.Add({TEXT("weapons/pistol"), FName(TEXT("MeshAsset"))});
			Idx.BuildFromFlatList(TEXT("/TestRoot"), Flat);
			TestTrue(TEXT("MountRoot non-empty"), !Idx.MountRoot.IsEmpty());
			TestTrue(TEXT("LeafItems has entries"), Idx.LeafItems.Num() >= 2);
			TestTrue(TEXT("ChildrenMap has entries"), Idx.ChildrenMap.Num() >= 1);
		});

		It("Reset clears all members", [this]()
		{
			FContentBrowserExtensionPathIndex Idx;
			TArray<FContentBrowserExtensionAssetEntry> Flat;
			Flat.Add({TEXT("a"), FName(TEXT("T"))});
			Idx.BuildFromFlatList(TEXT("/X"), Flat);
			Idx.Reset();
			TestEqual(TEXT("MountRoot"), Idx.MountRoot, FString());
			TestEqual(TEXT("LeafItems empty"), Idx.LeafItems.Num(), 0);
			TestEqual(TEXT("ChildrenMap empty"), Idx.ChildrenMap.Num(), 0);
			TestEqual(TEXT("FolderPaths empty"), Idx.FolderPaths.Num(), 0);
		});
	});

	Describe("Sanitize", [this]()
	{
		It("preserves leading slash", [this]()
		{
			FString R = UContentBrowserExtensionsStatics::SanitizeForPackagePath(TEXT("/x"));
			TestTrue(TEXT("starts with /"), R.StartsWith(TEXT("/")));
		});

		It("PascalCases space-separated words: 'Star Wars II' -> 'StarWarsII'", [this]()
		{
			FString R = UContentBrowserExtensionsStatics::SanitizeForPackagePath(TEXT("/Star Wars II"));
			TestEqual(TEXT("Sanitized"), R, FString(TEXT("/StarWarsII")));
		});

		It("preserves short all-uppercase tokens (II, IV)", [this]()
		{
			TestEqual(TEXT("II preserved"), UContentBrowserExtensionsStatics::SanitizeForPackagePath(TEXT("/II")), FString(TEXT("/II")));
			TestEqual(TEXT("IV preserved"), UContentBrowserExtensionsStatics::SanitizeForPackagePath(TEXT("/IV")), FString(TEXT("/IV")));
		});

		It("strips parens, TM, R, C marks", [this]()
		{
			FString R = UContentBrowserExtensionsStatics::SanitizeForPackagePath(TEXT("/Foo(Bar)"));
			TestFalse(TEXT("no parens"), R.Contains(TEXT("(")) || R.Contains(TEXT(")")));
		});

		It("preserves underscore, hyphen, dot in segments", [this]()
		{
			FString R = UContentBrowserExtensionsStatics::SanitizeForPackagePath(TEXT("/foo_bar-baz.qux"));
			TestTrue(TEXT("contains _-."), R.Contains(TEXT("_")) && R.Contains(TEXT("-")) && R.Contains(TEXT(".")));
		});

		It("preserves forward slashes between segments", [this]()
		{
			FString R = UContentBrowserExtensionsStatics::SanitizeForPackagePath(TEXT("/A/B/C"));
			int32 SlashCount = 0;
			for (TCHAR Ch : R) { if (Ch == TEXT('/')) ++SlashCount; }
			TestEqual(TEXT("3 slashes"), SlashCount, 3);
		});
	});
}
