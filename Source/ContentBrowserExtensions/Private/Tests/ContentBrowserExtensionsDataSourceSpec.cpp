// Copyright Epic Games, Inc. All Rights Reserved.
// ATF specs for UContentBrowserExtensionDataSource UClass via UE reflection.
// Migrated verbatim from FrostyContentBrowserDataSourceSpec.cpp (Phase 40 Plan 03 Wave 0).
// Reflection literal retargeted to /Script/ContentBrowserExtensions.ContentBrowserExtensionDataSource.

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "ContentBrowserDataSource.h"

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
}
