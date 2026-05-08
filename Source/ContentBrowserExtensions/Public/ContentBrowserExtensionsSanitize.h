// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::ContentBrowserExtensions
{
	/**
	 * Default sanitizer — PascalCase, strips TM (U+2122), (R) (U+00AE), (C) (U+00A9), parentheses;
	 * preserves _, -, . inside segments; preserves forward slashes between segments;
	 * short all-uppercase tokens (<=3 chars, e.g. "II", "IV") kept as-is.
	 *
	 * Consumers may override sanitization simply by passing an already-sanitized MountRoot
	 * to UContentBrowserExtensionDataSource::InitializeWithAssets — the path index does NOT
	 * re-sanitize a string that was already sanitized.
	 *
	 * @param InPath Input path or path segment (e.g. "/My Folder/Star Wars II").
	 * @return Sanitized path (e.g. "/MyFolder/StarWarsII").
	 */
	CONTENTBROWSEREXTENSIONS_API FString SanitizeForPackagePath(const FString& InPath);
}
