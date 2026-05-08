// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::ContentBrowserExtensions
{
	/**
	 * Default sanitizer — produces a UE package-name-safe path.
	 *
	 * Per-segment rules (segments delimited by '/'):
	 *  - Strips TM (U+2122), (R) (U+00AE), (C) (U+00A9), parentheses, and other non-alphanumerics
	 *    (underscore, hyphen, dot are preserved).
	 *  - Words within a segment are joined (space-separated input becomes PascalCase output).
	 *  - Mixed-case words are preserved verbatim — the sanitizer is idempotent on already-cased
	 *    identifiers, so "ThirdParty" stays "ThirdParty" (not "Thirdparty"). Pass mixed-case
	 *    intentionally if you want internal capitals retained.
	 *  - Short all-uppercase tokens (<=3 chars, e.g. "II", "IV") are preserved.
	 *  - All-lower / all-upper-long words are title-cased (first alpha to upper, rest to lower).
	 *
	 * Forward slashes between segments are preserved.
	 *
	 * Idempotence note: SanitizeForPackagePath(SanitizeForPackagePath(X)) == SanitizeForPackagePath(X)
	 * for any X, because mixed-case outputs of the first call are preserved by the second.
	 *
	 * Consumers may bypass sanitization entirely by passing an already-sanitized MountRoot
	 * to UContentBrowserExtensionDataSource::InitializeWithAssets — or by using the
	 * FContentBrowserExtensionFolderSegment-based overload to state both PathName and
	 * DisplayName explicitly.
	 *
	 * @param InPath Input path or path segment (e.g. "/My Folder/Star Wars II").
	 * @return Sanitized path (e.g. "/MyFolder/StarWarsII").
	 */
	CONTENTBROWSEREXTENSIONS_API FString SanitizeForPackagePath(const FString& InPath);
}
