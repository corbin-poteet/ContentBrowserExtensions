// Copyright Epic Games, Inc. All Rights Reserved.
// ATF specs for UContentBrowserExtensionsSubsystem (Phase 40, Plan 03 Wave 0 RED scaffold).
// No mock tests — real subsystem instance via GEditor->GetEditorSubsystem<>().
// CB-subsystem availability is asserted by ContentBrowserExtensionsCBSubsystemProbeSpec.cpp BEFORE this spec runs.

#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "UObject/Package.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserDataFilter.h"
#include "ContentBrowserItemData.h"
#include "ContentBrowserExtensionsSubsystem.h"
#include "ContentBrowserExtensionDataSource.h"
#include "ContentBrowserExtensionAssetEntry.h"

DEFINE_SPEC(FContentBrowserExtensionsSubsystemSpec, "ContentBrowserExtensions.Subsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FContentBrowserExtensionsSubsystemSpec::Define()
{
	auto GetSub = [this]() -> UContentBrowserExtensionsSubsystem*
	{
		if (!GEditor) { AddError(TEXT("GEditor is null")); return nullptr; }
		UContentBrowserExtensionsSubsystem* Sub = GEditor->GetEditorSubsystem<UContentBrowserExtensionsSubsystem>();
		if (!Sub) { AddError(TEXT("UContentBrowserExtensionsSubsystem not found via GEditor")); }
		return Sub;
	};

	auto MakeDS = [this]() -> UContentBrowserExtensionDataSource*
	{
		return NewObject<UContentBrowserExtensionDataSource>(GetTransientPackage(), NAME_None, RF_Transient);
	};

	Describe("Subsystem", [this, GetSub, MakeDS]()
	{
		It("should be accessible via GEditor->GetEditorSubsystem<>()", [this, GetSub]()
		{
			UContentBrowserExtensionsSubsystem* Sub = GetSub();
			TestNotNull(TEXT("Subsystem"), Sub);
		});

		It("RegisterDataSource at a fresh path returns Success AND activates with the CB framework", [this, GetSub, MakeDS]()
		{
			UContentBrowserExtensionsSubsystem* Sub = GetSub(); if (!Sub) { return; }
			UContentBrowserExtensionDataSource* DS = MakeDS();
			DS->InitializeWithAssets(TEXT("/TestRoot/Fresh1"), {});
			FContentBrowserExtensionRegistrationHandle Handle;
			ERegisterResult R = Sub->RegisterDataSource(DS, FName("UnitTest"), TEXT("/TestRoot/Fresh1"), FText::FromString(TEXT("Fresh1")), Handle);
			TestEqual(TEXT("Result"), (uint8)R, (uint8)ERegisterResult::Success);
			TestTrue(TEXT("Handle valid"), Handle.bValid);

			// Observability check: assigned FName must be observable via the CB framework's
			// active-sources accessor. Use GetActiveDataSources() — a direct membership check
			// that proves activation actually happened (not just that the API returned Success).
			UContentBrowserDataSubsystem* CBSub = IContentBrowserDataModule::Get().GetSubsystem();
			TestNotNull(TEXT("CB subsystem"), CBSub);
			if (CBSub)
			{
				TArray<FName> Active = CBSub->GetActiveDataSources();
				const bool bContainsAssigned = Active.Contains(Handle.DataSourceFName);
				TestTrue(TEXT("CB framework lists assigned FName among active sources (proves activation)"), bContainsAssigned);
			}

			Sub->UnregisterDataSource(Handle); // cleanup
		});

		It("RegisterDataSource at an already-used path returns AlreadyRegistered_LeafCollision", [this, GetSub, MakeDS]()
		{
			UContentBrowserExtensionsSubsystem* Sub = GetSub(); if (!Sub) { return; }
			UContentBrowserExtensionDataSource* DS1 = MakeDS();
			DS1->InitializeWithAssets(TEXT("/TestRoot/Collide"), {});
			FContentBrowserExtensionRegistrationHandle H1;
			ERegisterResult R1 = Sub->RegisterDataSource(DS1, FName("UnitTest"), TEXT("/TestRoot/Collide"), FText::FromString(TEXT("Collide")), H1);
			TestEqual(TEXT("First Success"), (uint8)R1, (uint8)ERegisterResult::Success);

			UContentBrowserExtensionDataSource* DS2 = MakeDS();
			DS2->InitializeWithAssets(TEXT("/TestRoot/Collide"), {});
			FContentBrowserExtensionRegistrationHandle H2;
			ERegisterResult R2 = Sub->RegisterDataSource(DS2, FName("UnitTest2"), TEXT("/TestRoot/Collide"), FText::FromString(TEXT("Collide2")), H2);
			TestEqual(TEXT("Second LeafCollision"), (uint8)R2, (uint8)ERegisterResult::AlreadyRegistered_LeafCollision);

			Sub->UnregisterDataSource(H1);
		});

		It("Two consumers under SHARED parent both Succeed (D-05 — registration API only; visible CB merge is engine-tested per RESEARCH Q1, manually verified per VALIDATION.md)", [this, GetSub, MakeDS]()
		{
			UContentBrowserExtensionsSubsystem* Sub = GetSub(); if (!Sub) { return; }
			UContentBrowserExtensionDataSource* A = MakeDS();
			A->InitializeWithAssets(TEXT("/Shared/A"), {});
			UContentBrowserExtensionDataSource* B = MakeDS();
			B->InitializeWithAssets(TEXT("/Shared/B"), {});
			FContentBrowserExtensionRegistrationHandle Ha, Hb;
			ERegisterResult Ra = Sub->RegisterDataSource(A, FName("ConsA"), TEXT("/Shared/A"), FText::FromString(TEXT("A")), Ha);
			ERegisterResult Rb = Sub->RegisterDataSource(B, FName("ConsB"), TEXT("/Shared/B"), FText::FromString(TEXT("B")), Hb);
			TestEqual(TEXT("A Success"), (uint8)Ra, (uint8)ERegisterResult::Success);
			TestEqual(TEXT("B Success"), (uint8)Rb, (uint8)ERegisterResult::Success);
			Sub->UnregisterDataSource(Ha);
			Sub->UnregisterDataSource(Hb);
		});

		It("Single consumer registers N data sources at distinct paths (D-06)", [this, GetSub, MakeDS]()
		{
			UContentBrowserExtensionsSubsystem* Sub = GetSub(); if (!Sub) { return; }
			FContentBrowserExtensionRegistrationHandle Hs[3];
			const int32 BaselineCount = Sub->GetRegisteredCount();
			for (int i = 0; i < 3; ++i)
			{
				UContentBrowserExtensionDataSource* DS = MakeDS();
				FString Path = FString::Printf(TEXT("/Multi/Game%d"), i);
				DS->InitializeWithAssets(Path, {});
				ERegisterResult R = Sub->RegisterDataSource(DS, FName("MultiCons"), Path, FText::FromString(Path), Hs[i]);
				TestEqual(TEXT("Each Success"), (uint8)R, (uint8)ERegisterResult::Success);
			}
			TestEqual(TEXT("RegisteredCount = baseline + 3"), Sub->GetRegisteredCount(), BaselineCount + 3);
			for (int i = 0; i < 3; ++i) { Sub->UnregisterDataSource(Hs[i]); }
		});

		It("UnregisterDataSource frees the path; subsequent Register at same path Succeeds", [this, GetSub, MakeDS]()
		{
			UContentBrowserExtensionsSubsystem* Sub = GetSub(); if (!Sub) { return; }
			UContentBrowserExtensionDataSource* DS1 = MakeDS();
			DS1->InitializeWithAssets(TEXT("/Recycle/X"), {});
			FContentBrowserExtensionRegistrationHandle H1;
			Sub->RegisterDataSource(DS1, FName("Cons"), TEXT("/Recycle/X"), FText::FromString(TEXT("X")), H1);
			Sub->UnregisterDataSource(H1);

			UContentBrowserExtensionDataSource* DS2 = MakeDS();
			DS2->InitializeWithAssets(TEXT("/Recycle/X"), {});
			FContentBrowserExtensionRegistrationHandle H2;
			ERegisterResult R = Sub->RegisterDataSource(DS2, FName("Cons"), TEXT("/Recycle/X"), FText::FromString(TEXT("X")), H2);
			TestEqual(TEXT("Re-register Success"), (uint8)R, (uint8)ERegisterResult::Success);
			Sub->UnregisterDataSource(H2);
		});

		It("Path traversal `..` is rejected as InvalidArguments (T-40-01)", [this, GetSub, MakeDS]()
		{
			UContentBrowserExtensionsSubsystem* Sub = GetSub(); if (!Sub) { return; }

			// Use a fresh DS + handle for each rejection call so we don't depend on
			// RegisterDataSource's defensive OutHandle reset and don't reuse a DS
			// pointer across two failed calls. Each scope is independent.
			{
				UContentBrowserExtensionDataSource* DS = MakeDS();
				FContentBrowserExtensionRegistrationHandle H;
				ERegisterResult R = Sub->RegisterDataSource(DS, FName("Cons"), TEXT("/x/../y"), FText::FromString(TEXT("X")), H);
				TestEqual(TEXT("Invalid (..)"), (uint8)R, (uint8)ERegisterResult::InvalidArguments);
			}

			// Drive letter — fresh DS + handle.
			{
				UContentBrowserExtensionDataSource* DSDrive = MakeDS();
				FContentBrowserExtensionRegistrationHandle HDrive;
				ERegisterResult RDrive = Sub->RegisterDataSource(DSDrive, FName("Cons"), TEXT("C:/Foo"), FText::FromString(TEXT("Drive")), HDrive);
				TestEqual(TEXT("Invalid (drive letter)"), (uint8)RDrive, (uint8)ERegisterResult::InvalidArguments);
			}
		});

		It("Empty VirtualRootPath is rejected as InvalidArguments", [this, GetSub, MakeDS]()
		{
			UContentBrowserExtensionsSubsystem* Sub = GetSub(); if (!Sub) { return; }
			UContentBrowserExtensionDataSource* DS = MakeDS();
			FContentBrowserExtensionRegistrationHandle H;
			ERegisterResult R = Sub->RegisterDataSource(DS, FName("Cons"), TEXT(""), FText::FromString(TEXT("Empty")), H);
			TestEqual(TEXT("Invalid"), (uint8)R, (uint8)ERegisterResult::InvalidArguments);
		});
	});
}
