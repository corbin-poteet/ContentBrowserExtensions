// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UObject/ObjectPtr.h"
#include "ContentBrowserExtensionsSubsystem.generated.h"

class UContentBrowserExtensionDataSource;

/**
 * Result of UContentBrowserExtensionsSubsystem::RegisterDataSource.
 *
 * Per checker issue #5+#10: when the CB module subsystem is unavailable, RegisterDataSource
 * returns CBSubsystemUnavailable (NOT Success) — surfacing the lifecycle contract gap through
 * the result type rather than silently logging a Warning and returning Success.
 */
UENUM()
enum class ERegisterResult : uint8
{
	Success,
	AlreadyRegistered_LeafCollision,
	InvalidArguments,
	/**
	 * IContentBrowserDataModule::Get().GetSubsystem() returned null at registration time.
	 * The data source was NOT added to the registry; consumer must retry once the CB
	 * subsystem is available (or surface as a phase blocker per project memory
	 * feedback_no_skipped_probes — surface to user, do not silently proceed).
	 */
	CBSubsystemUnavailable,
};

/**
 * Opaque handle returned by RegisterDataSource and consumed by UnregisterDataSource.
 * Carries enough state for the subsystem to find the registry entry on tear-down.
 */
USTRUCT(BlueprintType)
struct CONTENTBROWSEREXTENSIONS_API FContentBrowserExtensionRegistrationHandle
{
	GENERATED_BODY()

	/** The sanitized virtual root path used as the registry map key. */
	UPROPERTY()
	FString SanitizedVirtualRoot;

	/** The FName assigned by the subsystem; matches the data source's GetFName() and the CB subsystem's ActiveDataSources key. */
	UPROPERTY()
	FName DataSourceFName;

	/** True when filled by a successful RegisterDataSource call. */
	UPROPERTY()
	bool bValid = false;
};

/**
 * UEditorSubsystem owning the registry of registered UContentBrowserExtensionDataSource instances.
 *
 * Lifecycle (per RESEARCH Q2 + checker issue #2 fix):
 *  - Editor starts -> ContentBrowserExtensions module's StartupModule runs -> UEditorSubsystem
 *    collection auto-instantiates -> Initialize() runs.
 *  - Consumer plugin (e.g. Frosty) calls
 *      DS->InitializeWithAssets(VirtualRoot, Entries);  // data-population only (Plan 02 contract)
 *      GEditor->GetEditorSubsystem<UContentBrowserExtensionsSubsystem>()
 *        ->RegisterDataSource(DS, ConsumerId, VirtualRoot, DisplayName, OutHandle);
 *      // RegisterDataSource owns: Rename(AssignedName) + Initialize(true) + CBSub->ActivateDataSource(AssignedName)
 *  - Editor exit -> Deinitialize() unregisters remaining entries.
 *
 * Per D-04, VirtualRootPath is consumer-supplied — subsystem does NOT prepend any prefix.
 * Per D-07, leaf collision (two consumers registering the EXACT same VirtualRootPath after sanitization)
 * is rejected with ERegisterResult::AlreadyRegistered_LeafCollision and a logged error. Sharing a
 * parent path is intentional (D-05) and supported via UE's native bHandledVirtualFolder dedup gate
 * at ContentBrowserDataSubsystem.cpp:479-500.
 */
UCLASS(MinimalAPI)
class UContentBrowserExtensionsSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// ~UEditorSubsystem
	CONTENTBROWSEREXTENSIONS_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	CONTENTBROWSEREXTENSIONS_API virtual void Deinitialize() override;
	// ~End UEditorSubsystem

	/**
	 * Register a data source instance under a consumer-chosen virtual root path.
	 *
	 * Caller MUST call InDataSource->InitializeWithAssets(InVirtualRootPath, Entries) BEFORE this call
	 * (per D-03 explicit init; per Plan 02 contract InitializeWithAssets is data-population only).
	 *
	 * This method (the canonical lifecycle owner per D-02) calls in sequence:
	 *   1. Source->Rename(AssignedName, ...)
	 *   2. Source->Initialize(true)   (the UContentBrowserDataSource::Initialize call — moved here
	 *                                   from Plan 02's InitializeWithAssets per the lifecycle split)
	 *   3. CBSub->ActivateDataSource(AssignedName)
	 *
	 * @param InDataSource     Data source instance owned by the consumer; subsystem stores a TObjectPtr
	 *                         (no AddToRoot needed — subsystem itself is rooted).
	 * @param InConsumerId     Bookkeeping ID; appears nowhere in the virtual path (D-07).
	 * @param InVirtualRootPath  Consumer-supplied virtual root, e.g. "/ThirdParty/SWBF2" or
	 *                           "/MyTool/MyData" (D-04).
	 * @param InDisplayName    Leaf-folder display label in CB.
	 * @param OutHandle        Filled on Success; pass to UnregisterDataSource later.
	 * @return Success / AlreadyRegistered_LeafCollision / InvalidArguments / CBSubsystemUnavailable.
	 */
	CONTENTBROWSEREXTENSIONS_API ERegisterResult RegisterDataSource(
		UContentBrowserExtensionDataSource* InDataSource,
		FName InConsumerId,
		const FString& InVirtualRootPath,
		const FText& InDisplayName,
		FContentBrowserExtensionRegistrationHandle& OutHandle);

	/** Unregister a previously-registered data source. Idempotent on invalid handles. */
	CONTENTBROWSEREXTENSIONS_API void UnregisterDataSource(const FContentBrowserExtensionRegistrationHandle& InHandle);

	/** Number of currently-registered data sources. Test convenience. */
	CONTENTBROWSEREXTENSIONS_API int32 GetRegisteredCount() const;

private:
	/** Per-registration record. */
	struct FRegistration
	{
		TObjectPtr<UContentBrowserExtensionDataSource> DataSource;
		FName ConsumerId;
		FName DataSourceFName;
		FText DisplayName;
	};

	/** Registry keyed by sanitized virtual root path. */
	TMap<FString, FRegistration> RegisteredByPath;

	/**
	 * GC anchor — keeps registered data sources alive between Register and Unregister.
	 *
	 * Plan 04 Rule 1 fix: TObjectPtr inside FRegistration (a plain struct in a
	 * non-UPROPERTY TMap) does NOT participate in the reference graph, so registered
	 * data sources could be GC'd before Unregister was called. Frosty's prior code
	 * worked around this with NewDataSource->AddToRoot() at the consumer site, but
	 * Plan 04's "subsystem owns lifecycle" contract (D-02) requires the subsystem
	 * itself to root the objects.
	 *
	 * UPROPERTY()-decorated array makes UE's reference collector traverse and keep
	 * each entry alive for as long as the subsystem is alive (the subsystem itself
	 * is rooted by the UEditorSubsystem collection).
	 *
	 * Mirrored to RegisteredByPath: when Register adds, append here; when Unregister
	 * removes, RemoveSwap here too. Lookup is still via RegisteredByPath; this array
	 * exists purely to anchor the GC root.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UContentBrowserExtensionDataSource>> RootedDataSources;

	/** Validate the user-supplied virtual root path. Returns false (and fills OutReason) on `..`, drive letters, empty, etc. */
	bool ValidateVirtualRootPath(const FString& InPath, FString& OutReason) const;
};
