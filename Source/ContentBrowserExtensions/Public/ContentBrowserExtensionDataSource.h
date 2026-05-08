// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserDataSource.h"
#include "ContentBrowserExtensionAssetEntry.h"
#include "ContentBrowserExtensionPathIndex.h"
#include "ContentBrowserExtensionItemPayload.h"

#include "ContentBrowserExtensionDataSource.generated.h"

/**
 * Compiled filter for Content Browser Extension items.
 * Stored inside FContentBrowserDataCompiledFilter via FindOrAddFilter<T>().
 */
USTRUCT()
struct FContentBrowserExtensionCompiledFilter
{
	GENERATED_BODY()

	/** Lowercased search text (empty = no search filter). */
	FString SearchText;

	/** Asset type filters — empty set means all types pass. */
	TSet<FName> TypeFilters;

	/** Whether folder items should be included. */
	bool bIncludeFolders = true;

	/** Whether file items should be included. */
	bool bIncludeFiles = true;

	/** The internal path root that was passed to CompileFilter. */
	FName PathRoot;

	/** Whether to return items recursively under PathRoot, or only direct children. */
	bool bRecursive = false;
};

/**
 * Base data source that plugs consumer-supplied virtual content into UE's native Content Browser.
 *
 * Lifecycle:
 *  - Created via NewObject<UContentBrowserExtensionDataSource>(GetTransientPackage(), NAME_None, RF_Transient)
 *  - InitializeWithAssets() populates the path index (data-population only).
 *  - The lifecycle (Initialize(true) + ActivateDataSource) is owned by
 *    UContentBrowserExtensionsSubsystem::RegisterDataSource (Plan 03 / D-02).
 *  - Shutdown() resets the path index. CB-framework deactivation is owned by the subsystem
 *    during Unregister (with IsEngineExitRequested guard).
 *
 * Mount root: consumer-supplied (e.g. "/MyTool/MyData"). Plugin hardcodes nothing per D-04.
 * Visible in CB as /All/{MountRoot} once activated.
 *
 * Customization (D-09): the four protected hooks (UpdateThumbnail, GetItemAttribute, EditItem,
 * AppendItemReference) are virtual; subclasses override with `Super::Foo(...)` chaining
 * available where appropriate. Default implementations cover the common case (item-type
 * attribute, no custom thumbnail, no edit, append asset path as reference).
 */
UCLASS(Transient, MinimalAPI)
class UContentBrowserExtensionDataSource : public UContentBrowserDataSource
{
	GENERATED_BODY()

public:
	/**
	 * Populate the path index from a flat list of asset entries.
	 *
	 * Data-population only. The lifecycle (base-class init + CB-framework activation) is
	 * owned by UContentBrowserExtensionsSubsystem::RegisterDataSource (Plan 03 / D-02). The
	 * subsystem renames this data source to its assigned FName, calls the base-class
	 * Initialize, then activates the data source by assigned name. Callers MUST hand the
	 * data source to the subsystem after this call (or perform the base-class init +
	 * activation themselves if not using the subsystem path).
	 *
	 * @param InMountRootPath  Consumer-supplied virtual root path (e.g. "/MyTool/MyData").
	 *                         Plugin hardcodes nothing — consumers supply any prefix they
	 *                         like from their own call site (D-04).
	 * @param InAssets         Flat list of {Path, Type} entries.
	 */
	CONTENTBROWSEREXTENSIONS_API void InitializeWithAssets(const FString& InMountRootPath, TArray<FContentBrowserExtensionAssetEntry>&& InAssets);

	/**
	 * Collect all leaf asset entries under a folder path, optionally filtered by type.
	 * Used by "Import All"-style flows to recursively gather importable assets.
	 *
	 * @param InternalFolderPath  The internal (non-virtual, non-/All) folder path.
	 * @param AllowedTypes        If non-empty, only include assets with Type in this set.
	 *                            If empty, include all types.
	 * @param OutEntries          Receives matching asset entries.
	 */
	CONTENTBROWSEREXTENSIONS_API void GetLeafAssetsUnderFolder(
		FName InternalFolderPath,
		const TSet<FName>& AllowedTypes,
		TArray<FContentBrowserExtensionAssetEntry>& OutEntries) const;

	/** Returns the sanitized mount root path (e.g. "/MyTool/MyData"). */
	const FString& GetMountRoot() const { return PathIndex.MountRoot; }

	// ~UContentBrowserDataSource interface
	// CONTENTBROWSEREXTENSIONS_API per-method tags required so consumer subclasses
	// (e.g. UFrostyContentBrowserDataSource) can link against the vtable entries
	// from another module. Plan 04 Rule 3 fix: without these tags, MSVC emits
	// LNK2001 unresolved external symbol for every override the subclass DOESN'T
	// itself override (it falls through to the base impl, which must be exported).
	CONTENTBROWSEREXTENSIONS_API virtual void Shutdown() override;
	CONTENTBROWSEREXTENSIONS_API virtual void BuildRootPathVirtualTree() override;
	CONTENTBROWSEREXTENSIONS_API virtual void CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter) override;
	CONTENTBROWSEREXTENSIONS_API virtual void EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) override;
	CONTENTBROWSEREXTENSIONS_API virtual void EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) override;
	CONTENTBROWSEREXTENSIONS_API virtual bool IsDiscoveringItems(FText* OutStatus = nullptr) override;
	CONTENTBROWSEREXTENSIONS_API virtual bool DoesItemPassFilter(const FContentBrowserItemData& InItem, const FContentBrowserDataCompiledFilter& InFilter) override;
	CONTENTBROWSEREXTENSIONS_API virtual bool GetItemAttribute(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue) override;
	CONTENTBROWSEREXTENSIONS_API virtual bool UpdateThumbnail(const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail) override;
	CONTENTBROWSEREXTENSIONS_API virtual bool AppendItemReference(const FContentBrowserItemData& InItem, FString& InOutStr) override;
	CONTENTBROWSEREXTENSIONS_API virtual bool EditItem(const FContentBrowserItemData& InItem) override;
	// ~End UContentBrowserDataSource interface

protected:
	/** TMap-based path index built from the flat asset list. */
	FContentBrowserExtensionPathIndex PathIndex;

	/** True once InitializeWithAssets has completed. */
	bool bDiscoveryComplete = false;

	/**
	 * Construct a folder item data for the given internal path.
	 *
	 * Non-const because the underlying CB API contract requires mutating access:
	 *   - TryConvertInternalPathToVirtual is non-const (may cache results in the future).
	 *   - FContentBrowserItemData stores a non-const data-source pointer and the CB framework
	 *     will invoke non-const operations via that pointer later.
	 * Removing the const qualifier eliminates the const_cast<this> code smell at the call site.
	 */
	CONTENTBROWSEREXTENSIONS_API FContentBrowserItemData MakeFolderItem(FName InternalPath);

	/** Construct a file item data for the given internal path and asset entry. Non-const for the same reasons as MakeFolderItem. */
	CONTENTBROWSEREXTENSIONS_API FContentBrowserItemData MakeFileItem(FName InternalPath, const FContentBrowserExtensionAssetEntry& Entry);

	/**
	 * Cast the item's payload to FContentBrowserExtensionFileItemPayload.
	 * Returns nullptr if the item is not a file item or payload is missing.
	 */
	CONTENTBROWSEREXTENSIONS_API const FContentBrowserExtensionFileItemPayload* GetFilePayload(const FContentBrowserItemData& InItem) const;
};
