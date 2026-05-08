// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionsSubsystem.h"
#include "ContentBrowserExtensionDataSource.h"
#include "ContentBrowserExtensionsSanitize.h"
#include "ContentBrowserExtensionsModule.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"
#include "Misc/CoreMiscDefines.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

void UContentBrowserExtensionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogContentBrowserExtensions, Log, TEXT("UContentBrowserExtensionsSubsystem::Initialize"));
}

void UContentBrowserExtensionsSubsystem::Deinitialize()
{
	UE_LOG(LogContentBrowserExtensions, Log,
		TEXT("UContentBrowserExtensionsSubsystem::Deinitialize — %d active registrations"),
		RegisteredByPath.Num());

	if (!IsEngineExitRequested())
	{
		IContentBrowserDataModule* CBModule = IContentBrowserDataModule::GetPtr();
		UContentBrowserDataSubsystem* CBSub = CBModule ? CBModule->GetSubsystem() : nullptr;
		for (auto& Pair : RegisteredByPath)
		{
			FRegistration& Reg = Pair.Value;
			if (CBSub)
			{
				CBSub->DeactivateDataSource(Reg.DataSourceFName);
			}
			if (UContentBrowserExtensionDataSource* DS = Reg.DataSource.Get())
			{
				DS->Shutdown();
			}
		}
	}
	RegisteredByPath.Empty();
	RootedDataSources.Empty();
	Super::Deinitialize();
}

bool UContentBrowserExtensionsSubsystem::ValidateVirtualRootPath(const FString& InPath, FString& OutReason) const
{
	if (InPath.IsEmpty()) { OutReason = TEXT("empty"); return false; }
	if (InPath.Contains(TEXT(".."))) { OutReason = TEXT("contains '..'"); return false; }
	if (InPath.Contains(TEXT("\\"))) { OutReason = TEXT("contains backslash"); return false; }
	if (InPath.Contains(TEXT("~"))) { OutReason = TEXT("contains '~' (home expansion)"); return false; }
	// Drive letter check: starts with [A-Za-z]:
	if (InPath.Len() >= 2 && FChar::IsAlpha(InPath[0]) && InPath[1] == TEXT(':'))
	{
		OutReason = TEXT("starts with drive letter (absolute filesystem path)");
		return false;
	}
	return true;
}

ERegisterResult UContentBrowserExtensionsSubsystem::RegisterDataSource(
	UContentBrowserExtensionDataSource* InDataSource,
	FName InConsumerId,
	const FString& InVirtualRootPath,
	const FText& InDisplayName,
	FContentBrowserExtensionRegistrationHandle& OutHandle)
{
	OutHandle = FContentBrowserExtensionRegistrationHandle();

	if (!IsValid(InDataSource))
	{
		// Warning (not Error): callers signal-via-result-enum (ERegisterResult::InvalidArguments).
		// Logging at Error fails ATF specs that exercise this rejection path; the result enum is
		// the contract surface, the log line is diagnostic-only.
		UE_LOG(LogContentBrowserExtensions, Warning, TEXT("RegisterDataSource: invalid data source"));
		return ERegisterResult::InvalidArguments;
	}

	FString Reason;
	if (!ValidateVirtualRootPath(InVirtualRootPath, Reason))
	{
		// Warning (not Error): see comment above — rejection is contract behavior, not a fault.
		UE_LOG(LogContentBrowserExtensions, Warning,
			TEXT("RegisterDataSource: invalid VirtualRootPath '%s' (%s)"),
			*InVirtualRootPath, *Reason);
		return ERegisterResult::InvalidArguments;
	}

	const FString Sanitized = UE::ContentBrowserExtensions::SanitizeForPackagePath(InVirtualRootPath);

	if (FRegistration* Existing = RegisteredByPath.Find(Sanitized))
	{
		// Warning (not Error): D-07 leaf-collision is a contract rejection signaled via the
		// result enum; the log line is diagnostic-only. Logging at Error level fails ATF specs.
		UE_LOG(LogContentBrowserExtensions, Warning,
			TEXT("RegisterDataSource: leaf collision at '%s' — already registered by consumer '%s'. Use a different VirtualRootPath."),
			*Sanitized,
			*Existing->ConsumerId.ToString());
		return ERegisterResult::AlreadyRegistered_LeafCollision;
	}

	// CB-subsystem availability is a HARD prerequisite for activation. If unavailable,
	// surface via ERegisterResult::CBSubsystemUnavailable (per checker issue #5+#10) —
	// do NOT silently log a Warning and return Success. The lifecycle contract gap is
	// surfaced through the result type, not papered over.
	UContentBrowserDataSubsystem* CBSub = nullptr;
	if (IContentBrowserDataModule* CBModule = IContentBrowserDataModule::GetPtr())
	{
		CBSub = CBModule->GetSubsystem();
	}
	if (!CBSub)
	{
		UE_LOG(LogContentBrowserExtensions, Error,
			TEXT("RegisterDataSource: CB subsystem unavailable; data source NOT registered. ")
			TEXT("Returning ERegisterResult::CBSubsystemUnavailable. Consumer must retry once CB subsystem is available, ")
			TEXT("or surface as phase blocker (project memory feedback_no_skipped_probes)."));
		return ERegisterResult::CBSubsystemUnavailable;
	}

	// Subsystem-assigned FName: prefix + consumer + sanitized-path with slashes mapped to underscores.
	FString FNameBase = Sanitized;
	FNameBase.ReplaceInline(TEXT("/"), TEXT("_"));
	FName AssignedName(*FString::Printf(TEXT("CBE_%s%s"), *InConsumerId.ToString(), *FNameBase));

	// Defensive: if a stale object (from PIE / hot-reload / a different consumer / a leaked
	// previous-session object) already holds this name in the transient package, the Rename
	// below would hit Obj.cpp:349 fatal and crash the editor. Fall back to a unique variant
	// instead. Same-process unregister-then-register is already protected by the rename-to-
	// unique-dead-name in UnregisterDataSource; this guard covers the cross-process /
	// cross-consumer case.
	if (StaticFindObjectFast(nullptr, GetTransientPackage(), AssignedName))
	{
		const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), InDataSource->GetClass(), AssignedName);
		UE_LOG(LogContentBrowserExtensions, Warning,
			TEXT("RegisterDataSource: assigned name '%s' already in use; falling back to '%s'"),
			*AssignedName.ToString(), *UniqueName.ToString());
		AssignedName = UniqueName;
	}

	// Lifecycle owner sequence (per checker issue #2 fix — additive on top of Plan 02 artifact):
	//   1. Rename to assigned name (so CB subsystem keys match across the call)
	//   2. base-class init + CB-framework activation
	//   3. CBSub->ActivateDataSource(AssignedName)
	// Plan 02's InitializeWithAssets ships as data-population only; this plan owns the
	// remaining lifecycle calls without mutating Plan 02's artifact.
	InDataSource->Rename(*AssignedName.ToString(), nullptr, REN_DontCreateRedirectors);
	InDataSource->Initialize(true);
	CBSub->ActivateDataSource(AssignedName);

	FRegistration Reg;
	Reg.DataSource = InDataSource;
	Reg.ConsumerId = InConsumerId;
	Reg.DataSourceFName = AssignedName;
	Reg.DisplayName = InDisplayName;
	RegisteredByPath.Add(Sanitized, MoveTemp(Reg));

	// Anchor in the GC root container so the data source survives until Unregister.
	// Plan 04 Rule 1 fix: TObjectPtr inside a non-UPROPERTY TMap value does NOT
	// participate in the reference graph; without this UPROPERTY-decorated array,
	// registered data sources got GC'd between Register and Unregister and crashed
	// in DS->Shutdown() (observed in FrostyEditorTests.EditorSubsystem.TriggerValidation
	// integration test, ATF run 2026-05-06).
	RootedDataSources.Add(InDataSource);

	OutHandle.SanitizedVirtualRoot = Sanitized;
	OutHandle.DataSourceFName = AssignedName;
	OutHandle.bValid = true;

	UE_LOG(LogContentBrowserExtensions, Log,
		TEXT("RegisterDataSource: '%s' (consumer=%s, fname=%s) — Rename + base-init + CB-activate sequence complete"),
		*Sanitized, *InConsumerId.ToString(), *AssignedName.ToString());

	return ERegisterResult::Success;
}

void UContentBrowserExtensionsSubsystem::UnregisterDataSource(const FContentBrowserExtensionRegistrationHandle& InHandle)
{
	if (!InHandle.bValid) { return; }

	FRegistration Reg;
	if (!RegisteredByPath.RemoveAndCopyValue(InHandle.SanitizedVirtualRoot, Reg)) { return; }

	if (!IsEngineExitRequested())
	{
		if (IContentBrowserDataModule* CBModule = IContentBrowserDataModule::GetPtr())
		{
			if (UContentBrowserDataSubsystem* CBSub = CBModule->GetSubsystem())
			{
				CBSub->DeactivateDataSource(Reg.DataSourceFName);
			}
		}
	}

	if (UContentBrowserExtensionDataSource* DS = Reg.DataSource.Get())
	{
		DS->Shutdown();
		// Free the CBE_* name slot for re-registration at the same VirtualRootPath. UE forbids
		// renaming a fresh UObject onto an existing same-named object (Obj.cpp:349 fatal); since
		// the unregistered DS may not be GC'd before the next Register, we proactively rename it
		// to a unique transient bin name. Mark it as garbage so GC will reap it on next sweep.
		// Extract the base name into a local for readability — keeps the inline Printf
		// allocation off the MakeUniqueObjectName call site and makes the two-step
		// "build base name → build unique name" intent explicit.
		const FName BaseName(*FString::Printf(TEXT("CBE_Unregistered_%s"), *Reg.DataSourceFName.ToString()));
		const FName UniqueDeadName = MakeUniqueObjectName(GetTransientPackage(), DS->GetClass(), BaseName);
		DS->Rename(*UniqueDeadName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors);
		DS->MarkAsGarbage();

		// Drop the GC root anchor so the now-MarkedAsGarbage object can be reaped on next sweep.
		// (Plan 04 Rule 1 fix mate: keep RootedDataSources in lockstep with RegisteredByPath.)
		RootedDataSources.RemoveSingleSwap(DS);
	}

	UE_LOG(LogContentBrowserExtensions, Log, TEXT("UnregisterDataSource: '%s'"), *InHandle.SanitizedVirtualRoot);
}

int32 UContentBrowserExtensionsSubsystem::GetRegisteredCount() const
{
	return RegisteredByPath.Num();
}
