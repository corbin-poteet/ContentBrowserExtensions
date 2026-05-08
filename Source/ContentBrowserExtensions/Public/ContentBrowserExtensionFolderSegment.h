// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A single segment of a Content Browser virtual folder path with explicit
 * (PathName, DisplayName) decoupling.
 *
 * Use case: building a mount-root folder hierarchy where the on-disk path
 * identifier and the human-readable label differ. Example:
 *   { PathName: "ThirdParty",          DisplayName: "Third Party"                  }
 *   { PathName: "StarWarsBattlefrontII", DisplayName: "STAR WARS™ Battlefront™ II" }
 *
 * Pass an array of these to UContentBrowserExtensionDataSource::InitializeWithAssets
 * (segment overload) instead of a single composite string. This avoids the
 * sanitizer-roundtrip ambiguity that loses internal capitalization on
 * single-word PascalCased segments (e.g. "ThirdParty" sanitizes to "Thirdparty"
 * because the sanitizer treats the whole segment as one word and lowercases
 * mid-word characters).
 */
struct FContentBrowserExtensionFolderSegment
{
	/** Path-form: the segment as it appears in the virtual path / breadcrumbs.
	 *  Should be a valid UE package-name segment (alphanumeric + ._-, no spaces). */
	FString PathName;

	/** Display-form: the segment as shown in the Content Browser tree view.
	 *  May contain spaces and any printable characters. If empty or equal to
	 *  PathName, no display override is registered (PathName renders as-is). */
	FString DisplayName;

	FContentBrowserExtensionFolderSegment() = default;

	FContentBrowserExtensionFolderSegment(FString InPathName, FString InDisplayName)
		: PathName(MoveTemp(InPathName))
		, DisplayName(MoveTemp(InDisplayName))
	{}

	/**
	 * Construct a segment by sanitizing InDisplayName via the plugin's default
	 * SanitizeForPackagePath to derive PathName.
	 *
	 * Use this when the consumer has only the display string and is happy to
	 * let the plugin's default sanitizer choose the path form. For full
	 * control, construct the segment directly with explicit (PathName, DisplayName).
	 */
	static CONTENTBROWSEREXTENSIONS_API FContentBrowserExtensionFolderSegment FromDisplayName(const FString& InDisplayName);
};
