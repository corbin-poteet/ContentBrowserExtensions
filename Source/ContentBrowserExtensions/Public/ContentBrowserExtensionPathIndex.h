// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserExtensionAssetEntry.h"
#include "ContentBrowserExtensionFolderSegment.h"

/**
 * Sanitize a string (or full virtual path) so it only contains characters valid in a UE
 * package name: alphanumerics, underscore, hyphen, dot, and forward-slash.
 *
 * Uses PascalCase: each space-separated word becomes title-cased and joined.
 * Short all-uppercase words (<=3 chars, e.g. "II", "IV") are preserved as-is.
 * TM (U+2122), registered (U+00AE), copyright (U+00A9), parentheses, and other
 * non-alphanumeric characters are stripped.
 *
 * Forward slashes are preserved, so full paths like "/My Folder/My Game" become
 * "/MyFolder/MyGame".
 *
 * Default sanitizer; consumers may override by pre-sanitizing the mount root before
 * passing it to UContentBrowserExtensionDataSource::InitializeWithAssets.
 *
 * Defined in ContentBrowserExtensionsSanitize.h. Forward-declared at use sites; this
 * header pulls in the declaration via the BuildFromFlatList implementation in the .cpp.
 */

/**
 * TMap-based path index for O(1) parent-to-children lookups in the Content Browser.
 *
 * Paths are stored with original case. FName lookups are case-insensitive,
 * so queries from the CB framework (which may use different casing) still match.
 *
 * Mount root and all internal paths are sanitized via SanitizeForPackagePath() before
 * storage to avoid LogPackageName warnings from UE's DoesPackageExist checks.
 *
 * DisplayNameMap stores the original (unsanitized) display name for each path segment
 * that was changed by sanitization so the Content Browser can show user-friendly names.
 *
 * Mount root is consumer-supplied (per D-04 — plugin hardcodes nothing). Behavior is
 * preserved from the Frosty-side reference implementation: same TMap/TSet/TArray usage,
 * same FName case-insensitive lookup semantics, same DisplayNameMap approach. Forward
 * slashes are preserved across segments, so a full path like "/My Folder/My Game"
 * becomes "/MyFolder/MyGame" after sanitization.
 *
 * Usage:
 *   FContentBrowserExtensionPathIndex Index;
 *   Index.BuildFromFlatList(TEXT("/MyTool/MyData"), AssetsFromConsumer);
 *   // then query ChildrenMap / LeafItems / FolderPaths / DisplayNameMap
 */
struct CONTENTBROWSEREXTENSIONS_API FContentBrowserExtensionPathIndex
{
	/** Parent path -> array of direct child paths. */
	TMap<FName, TArray<FName>> ChildrenMap;

	/** Leaf path -> original FContentBrowserExtensionAssetEntry (path and type). */
	TMap<FName, FContentBrowserExtensionAssetEntry> LeafItems;

	/** Set of all folder paths (not leaves). */
	TSet<FName> FolderPaths;

	/**
	 * Sanitized full path (as FName) -> original unsanitized display name for that segment.
	 * Only populated for segments where original != sanitized.
	 * Key example: FName("/MyFolder")
	 * Value example: "My Folder"
	 */
	TMap<FName, FString> DisplayNameMap;

	/** The sanitized mount root path (e.g. "/MyTool/MyData" — fully consumer-supplied; plugin hardcodes nothing). */
	FString MountRoot;

	/**
	 * Build the index from a flat list of FContentBrowserExtensionAssetEntry objects,
	 * using a string mount root that is sanitized internally.
	 *
	 * Convenience overload — derives mount-root segments by sanitizing InMountRoot and
	 * recovering each segment's display form from the original input. Equivalent to:
	 *   BuildFromFolderSegments( {SegmentsFromString(InMountRoot)}, FlatList )
	 *
	 * Prefer the BuildFromFolderSegments overload when you want explicit control over
	 * the (PathName, DisplayName) pair for each mount-root segment — particularly when
	 * the path-form is not the sanitized round-trip of the display-form (e.g. PathName
	 * "ThirdParty" + DisplayName "Third Party": the sanitizer would collapse "ThirdParty"
	 * to "Thirdparty" because it lacks a word boundary).
	 *
	 * @param InMountRoot  The virtual mount root (e.g. "/My Folder/My Game" or "/MyTool/MyData").
	 *                     Consumer-supplied; plugin hardcodes nothing.
	 * @param FlatList     Flat array of {Path, Type} entries.
	 */
	void BuildFromFlatList(const FString& InMountRoot, const TArray<FContentBrowserExtensionAssetEntry>& FlatList);

	/**
	 * Build the index from a structured array of mount-root segments — preferred form.
	 *
	 * Each segment carries an explicit (PathName, DisplayName) pair. The mount root is
	 * constructed by joining the PathName values with '/'; DisplayNameMap is seeded
	 * directly from any segment whose DisplayName differs from its PathName. No sanitizer
	 * is applied to the segments — consumers are trusted to supply path-valid PathNames.
	 *
	 * @param InMountRootSegments  Ordered list of (PathName, DisplayName) segments forming
	 *                             the mount-root hierarchy. The full mount root is
	 *                             "/" + Join(.PathName, "/").
	 * @param FlatList             Flat array of {Path, Type} entries under the mount root.
	 */
	void BuildFromFolderSegments(const TArray<FContentBrowserExtensionFolderSegment>& InMountRootSegments,
	                             const TArray<FContentBrowserExtensionAssetEntry>& FlatList);

	/** Clear all state. Call when the consumer's data set changes or the panel is reset. */
	void Reset();

private:
	/**
	 * Walk the asset entries to populate ChildrenMap / LeafItems / FolderPaths and the
	 * best-cased DisplayNameMap entries for ancestor segments.
	 *
	 * Caller must have set MountRoot and seeded any mount-root DisplayNameMap entries
	 * before invoking this.
	 */
	void IndexAssetEntries(const TArray<FContentBrowserExtensionAssetEntry>& FlatList);
};
