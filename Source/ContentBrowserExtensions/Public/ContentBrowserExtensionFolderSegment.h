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
 * already-valid PathNames pass through unchanged). The safety pass can be opted out
 * of via the bSanitizePathName parameter if the caller has already sanitized the
 * string — useful for hot paths or when round-tripping persisted PathNames.
 * DisplayName is never sanitized — it is consumer-controlled and may contain
 * spaces, TM marks, or any printable characters.
 */
struct FContentBrowserExtensionFolderSegment
{
	/** Path-form: the segment as it appears in the virtual path / breadcrumbs.
	 *  Sanitized at construction (unless bSanitizePathName=false was passed). */
	FString PathName;

	/** Display-form: the segment as shown in the Content Browser tree view.
	 *  Stored verbatim — never sanitized. May contain spaces and any printable
	 *  characters. If empty or equal to PathName, no display override is
	 *  registered (PathName renders as-is). */
	FString DisplayName;

	FContentBrowserExtensionFolderSegment() = default;

	/**
	 * Single-input constructor: sanitizes InPathName via SanitizeForPackagePath
	 * and uses the result for BOTH PathName and DisplayName. Use this when you
	 * don't need a separate display label — the segment renders the same in the
	 * path bar and the tree view.
	 *
	 * To opt out of sanitization, or to supply a separate display label, use the
	 * two-argument constructor: `Segment(name, name, false)` skips the safety pass.
	 * (A bool parameter on this ctor would be ambiguous with the two-string ctor
	 * when called with TEXT() literals — wchar_t* implicitly converts to bool.)
	 *
	 * @param InPathName  Path string to use as both PathName and DisplayName.
	 */
	CONTENTBROWSEREXTENSIONS_API explicit FContentBrowserExtensionFolderSegment(const FString& InPathName);

	/**
	 * Explicit (PathName, DisplayName) constructor. PathName is sanitized via
	 * SanitizeForPackagePath (unless bSanitizePathName=false); DisplayName is
	 * stored verbatim.
	 *
	 * Useful patterns:
	 *   { TEXT("ThirdParty"), TEXT("Third Party") }                // explicit both forms
	 *   { GameDisplayName,    GameDisplayName }                    // sanitize for path, keep verbatim for display
	 *   { AlreadyCleanName,   FriendlyLabel, false }              // skip safety pass (bSanitizePathName=false)
	 *
	 * @param InPathName          Path-form segment (alphanumeric + ._-, no spaces).
	 * @param InDisplayName       Display-form segment shown in the CB tree.
	 * @param bSanitizePathName   When true (default), runs InPathName through
	 *                            SanitizeForPackagePath before storing. Pass false to
	 *                            skip sanitization when the caller knows the input is
	 *                            already a valid path-form segment.
	 */
	CONTENTBROWSEREXTENSIONS_API FContentBrowserExtensionFolderSegment(const FString& InPathName, const FString& InDisplayName, bool bSanitizePathName = true);
};
