// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A single segment of a Content Browser virtual folder path with explicit
 * (PathName, DisplayName) decoupling.
 *
 * Use case: building a mount-root folder hierarchy where the on-disk path
 * identifier and the human-readable label differ. Example:
 *   { TEXT("ThirdParty"),           TEXT("Third Party")                  }
 *   { TEXT("StarWarsBattlefrontII"), TEXT("STAR WARS™ Battlefront™ II") }
 *
 * Pass an array of these to UContentBrowserExtensionDataSource::InitializeWithAssets
 * (segment overload) instead of a single composite string. The struct lets consumers
 * state the path-form and display-form independently, which avoids the
 * sanitizer-roundtrip ambiguity of single composite strings.
 *
 * Both constructors run PathName through the default SanitizeForPackagePath as a
 * safety measure (since the sanitizer is idempotent on mixed-case identifiers,
 * already-valid PathNames pass through unchanged). DisplayName is never sanitized
 * — it is consumer-controlled and may contain spaces, TM marks, or any printable
 * characters.
 */
struct FContentBrowserExtensionFolderSegment
{
	/** Path-form: the segment as it appears in the virtual path / breadcrumbs.
	 *  Sanitized at construction; safe to pass any string here. */
	FString PathName;

	/** Display-form: the segment as shown in the Content Browser tree view.
	 *  Stored verbatim — never sanitized. May contain spaces and any printable
	 *  characters. If empty or equal to PathName, no display override is
	 *  registered (PathName renders as-is). */
	FString DisplayName;

	FContentBrowserExtensionFolderSegment() = default;

	/**
	 * Single-input constructor: sanitizes InPathName via SanitizeForPackagePath and
	 * uses the sanitized result for BOTH PathName and DisplayName. Use this when
	 * you don't need a separate display label — the segment renders the same in
	 * the path bar and the tree view.
	 *
	 * To get a different display form, use the two-argument constructor instead.
	 */
	CONTENTBROWSEREXTENSIONS_API explicit FContentBrowserExtensionFolderSegment(const FString& InPathName);

	/**
	 * Explicit (PathName, DisplayName) constructor. PathName is sanitized via
	 * SanitizeForPackagePath as a safety measure; DisplayName is stored verbatim.
	 *
	 * Useful patterns:
	 *   { TEXT("ThirdParty"), TEXT("Third Party") }   // explicit both forms
	 *   { GameDisplayName, GameDisplayName }          // sanitize for path, keep verbatim for display
	 */
	CONTENTBROWSEREXTENSIONS_API FContentBrowserExtensionFolderSegment(const FString& InPathName, const FString& InDisplayName);
};
