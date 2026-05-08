// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserItemData.h"
#include "ContentBrowserExtensionAssetEntry.h"

/**
 * Payload for a virtual folder item in the Content Browser.
 * Stores only the internal path; no disk presence.
 *
 * Naming note (IN-04 fix): the field was previously called VirtualPath but
 * MakeFolderItem actually constructs this payload with the InternalPath
 * (pre-/All/ prefix). The accessor name was misleading; consumers reading
 * the value expected the /All/ virtual path. Renamed to InternalPath /
 * GetInternalPath() so the contract matches the stored content.
 */
class FContentBrowserExtensionFolderItemPayload : public IContentBrowserItemDataPayload
{
public:
	explicit FContentBrowserExtensionFolderItemPayload(FName InInternalPath)
		: InternalPath(InInternalPath)
	{
	}

	FName GetInternalPath() const { return InternalPath; }

private:
	FName InternalPath;
};

/**
 * Payload for a virtual file (asset) item in the Content Browser.
 * Stores the original (un-lowercased) asset path and type from the consumer-supplied entry.
 *
 * @note The constructor takes const FContentBrowserExtensionAssetEntry& — stores original
 *       path for display/export purposes.
 */
class FContentBrowserExtensionFileItemPayload : public IContentBrowserItemDataPayload
{
public:
	explicit FContentBrowserExtensionFileItemPayload(const FContentBrowserExtensionAssetEntry& InEntry)
		: AssetPath(InEntry.Path)
		, AssetType(InEntry.Type)
	{
	}

	const FString& GetAssetPath() const { return AssetPath; }
	const FName& GetAssetType() const { return AssetType; }

private:
	/** Original consumer-supplied asset path (not lowercased, for display/copy purposes). */
	FString AssetPath;

	/** Asset type name, e.g. "TextureAsset". */
	FName AssetType;
};
