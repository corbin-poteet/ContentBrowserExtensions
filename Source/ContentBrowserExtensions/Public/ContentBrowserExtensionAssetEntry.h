// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Flat asset-entry schema for the Content Browser Extensions plugin.
 *
 * Consumers populate an array of these and pass it to UContentBrowserExtensionDataSource::InitializeWithAssets,
 * which builds a path index from the flat list. Schema is intentionally minimal per Phase 40 D-08;
 * no attribute bag, no payload-subclass pluggability in this phase.
 */
struct FContentBrowserExtensionAssetEntry
{
	/** Internal path under the consumer's virtual mount root (e.g. "weapons/rifle/model"). */
	FString Path;

	/** Asset type name surfaced in the Content Browser (e.g. "TextureAsset", "MeshAsset"). */
	FName Type;
};
