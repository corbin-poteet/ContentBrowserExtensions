// Copyright Epic Games, Inc. All Rights Reserved.
// ATF specs for UContentBrowserExtensionDataSource UClass via UE reflection.
// Migrated verbatim from FrostyContentBrowserDataSourceSpec.cpp (Phase 40 Plan 03 Wave 0).
// Reflection literal retargeted to /Script/ContentBrowserExtensions.ContentBrowserExtensionDataSource.

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "ContentBrowserDataSource.h"
#include "ContentBrowserDataFilter.h"
#include "ContentBrowserItemData.h"
#include "ContentBrowserExtensionDataSource.h"
#include "ContentBrowserExtensionAssetEntry.h"

DEFINE_SPEC(FContentBrowserExtensionsDataSourceSpec, "ContentBrowserExtensions.DataSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FContentBrowserExtensionsDataSourceSpec::Define()
{
	// Helper: find the UContentBrowserExtensionDataSource UClass via reflection
	// and construct an instance. Returns nullptr if the class is not found.
	auto CreateDataSource = [this]() -> UContentBrowserDataSource*
	{
		UClass* DSClass = FindObject<UClass>(
			nullptr, TEXT("/Script/ContentBrowserExtensions.ContentBrowserExtensionDataSource"));
		if (!DSClass)
		{
			AddError(TEXT("UContentBrowserExtensionDataSource UClass not found via reflection"));
			return nullptr;
		}
		return Cast<UContentBrowserDataSource>(
			NewObject<UObject>(GetTransientPackage(), DSClass, NAME_None, RF_Transient));
	};

	// -----------------------------------------------------------------------
	// Data source construction via reflection
	// -----------------------------------------------------------------------
	Describe("Construction", [this, CreateDataSource]()
	{
		It("should find UContentBrowserExtensionDataSource UClass via reflection", [this]()
		{
			UClass* DSClass = FindObject<UClass>(
				nullptr, TEXT("/Script/ContentBrowserExtensions.ContentBrowserExtensionDataSource"));
			TestNotNull("UClass found", DSClass);
		});

		It("should construct instance via NewObject", [this, CreateDataSource]()
		{
			UContentBrowserDataSource* DS = CreateDataSource();
			TestNotNull("Instance created", DS);
			if (DS)
			{
				DS->MarkAsGarbage();
			}
		});

		It("should report IsDiscoveringItems true before initialization", [this, CreateDataSource]()
		{
			UContentBrowserDataSource* DS = CreateDataSource();
			if (!DS) { return; }
			TestTrue("IsDiscoveringItems before init", DS->IsDiscoveringItems());
			DS->MarkAsGarbage();
		});
	});

	// -----------------------------------------------------------------------
	// Shutdown
	// -----------------------------------------------------------------------
	Describe("Shutdown", [this, CreateDataSource]()
	{
		It("should not crash on Shutdown of uninitialized instance", [this, CreateDataSource]()
		{
			UContentBrowserDataSource* DS = CreateDataSource();
			if (!DS) { return; }
			DS->Shutdown();
			TestTrue("Shutdown completed without crash", true);
			DS->MarkAsGarbage();
		});
	});

	// -----------------------------------------------------------------------
	// Recursive enumerate boundary check
	// -----------------------------------------------------------------------
	// Regression coverage for the latent boundary-collision bug at
	// ContentBrowserExtensionDataSource.cpp:338/352 (pre-fix). The unprotected
	// raw `StartsWith` matched sibling-prefix paths (e.g. PathRoot="/X/Foo"
	// would incorrectly match folder "/X/FooBar/Sub") in the recursive
	// EnumerateItemsMatchingFilter case. Switching to FPathViews::IsParentPathOf
	// is boundary-aware and rejects sibling-prefix collisions.
	Describe("Recursive enumerate boundary", [this]()
	{
		It("does not match a non-boundary prefix in recursive enumerate (e.g. /X/Foo vs sibling /X/FooBar)", [this]()
		{
			// Construct a concrete UContentBrowserExtensionDataSource (not via reflection —
			// we need access to InitializeWithAssets / CompileFilter / EnumerateItemsMatchingFilter).
			UContentBrowserExtensionDataSource* DS = NewObject<UContentBrowserExtensionDataSource>(
				GetTransientPackage(), NAME_None, RF_Transient);
			if (!DS) { AddError(TEXT("Failed to construct UContentBrowserExtensionDataSource")); return; }

			// Build a flat list with two sibling-prefix branches under the same mount root.
			//   /X/Foo/Inner/Asset1     (legit descendant of /X/Foo)
			//   /X/FooBar/Inner/Asset2  (sibling-prefix — must NOT match /X/Foo as ancestor)
			TArray<FContentBrowserExtensionAssetEntry> Flat;
			Flat.Add({TEXT("Foo/Inner/Asset1"),    FName(TEXT("TestAsset"))});
			Flat.Add({TEXT("FooBar/Inner/Asset2"), FName(TEXT("TestAsset"))});
			DS->InitializeWithAssets(FString(TEXT("/X")), MoveTemp(Flat));

			// Manually populate the compiled filter with PathRoot="/X/Foo" recursive=true.
			// We bypass CompileFilter so the test pins behavior of EnumerateItemsMatchingFilter
			// directly (CompileFilter has its own boundary check at lines 172-175 — already
			// boundary-aware via the post-fix swap to IsParentPathOf, so it correctly accepts
			// "/X/Foo" as a descendant of mount "/X").
			FContentBrowserDataCompiledFilter CompiledFilter;
			FContentBrowserExtensionCompiledFilter& CBEFilter =
				CompiledFilter.CompiledFilters.FindOrAdd(DS).FindOrAddFilter<FContentBrowserExtensionCompiledFilter>();
			CBEFilter.PathRoot       = FName(TEXT("/X/Foo"));
			CBEFilter.bRecursive     = true;
			CBEFilter.bIncludeFolders = true;
			CBEFilter.bIncludeFiles   = true;

			// Collect every emitted item path and assert that NONE start with "/X/FooBar".
			// (Items legitimately under /X/Foo are fine — we only care about the cross-boundary
			// leak.)
			TArray<FString> Emitted;
			DS->EnumerateItemsMatchingFilter(CompiledFilter,
				[&Emitted](FContentBrowserItemData&& Item) -> bool
				{
					Emitted.Add(Item.GetInternalPath().ToString());
					return true;
				});

			int32 LeakedCount = 0;
			for (const FString& Path : Emitted)
			{
				if (Path.StartsWith(TEXT("/X/FooBar"), ESearchCase::IgnoreCase))
				{
					AddError(FString::Printf(TEXT("Leaked sibling-prefix item across boundary: %s"), *Path));
					++LeakedCount;
				}
			}
			TestEqual(TEXT("/X/FooBar items leaked into /X/Foo recursive enumerate"), LeakedCount, 0);

			DS->Shutdown();
			DS->MarkAsGarbage();
		});
	});
}
