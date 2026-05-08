// Copyright Epic Games, Inc. All Rights Reserved.
// Wave 0 prerequisite probe: assert CB module subsystem is available under NullRHI.
// If unavailable, downstream subsystem specs would silently pass for the wrong reason.
// Per project memory feedback_no_skipped_probes: surface to user, do not silently proceed.

#include "Misc/AutomationTest.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"

DEFINE_SPEC(FContentBrowserExtensionsCBSubsystemProbeSpec, "ContentBrowserExtensions.CBSubsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FContentBrowserExtensionsCBSubsystemProbeSpec::Define()
{
	Describe("CBSubsystem", [this]()
	{
		It("ProbeAvailability — IContentBrowserDataModule::GetPtr() and GetSubsystem() are non-null under NullRHI", [this]()
		{
			IContentBrowserDataModule* Module = IContentBrowserDataModule::GetPtr();
			if (!Module)
			{
				AddError(TEXT("IContentBrowserDataModule::GetPtr() returned null under NullRHI. Downstream subsystem specs would pass for the wrong reason. Phase 40 blocker — surface to user per project memory feedback_no_skipped_probes."));
				return;
			}
			TestNotNull(TEXT("IContentBrowserDataModule (GetPtr)"), Module);

			UContentBrowserDataSubsystem* CBSub = Module->GetSubsystem();
			if (!CBSub)
			{
				AddError(TEXT("IContentBrowserDataModule::Get().GetSubsystem() returned null under NullRHI. Downstream subsystem specs that depend on ActivateDataSource would silently pass without observable activation. Phase 40 blocker — surface to user per project memory feedback_no_skipped_probes."));
				return;
			}
			TestNotNull(TEXT("UContentBrowserDataSubsystem (GetSubsystem)"), CBSub);
		});
	});
}
