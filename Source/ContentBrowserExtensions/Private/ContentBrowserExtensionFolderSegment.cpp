// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionFolderSegment.h"
#include "ContentBrowserExtensionsSanitize.h"

namespace UE::ContentBrowserExtensions::Private
{
	/**
	 * Sanitize a single folder-segment string by wrapping in a leading slash (so the
	 * path-aware SanitizeForPackagePath treats the input as a one-segment path),
	 * running the sanitizer, then stripping the slash back off.
	 *
	 * Idempotent: passing an already-sanitized PathName through this returns the
	 * same string unchanged (mixed-case identifiers are preserved by the sanitizer).
	 */
	static FString SanitizeSegment(const FString& InSegment)
	{
		FString Sanitized = SanitizeForPackagePath(FString(TEXT("/")) + InSegment);
		if (Sanitized.StartsWith(TEXT("/")))
		{
			Sanitized.RemoveAt(0);
		}
		return Sanitized;
	}
}

FContentBrowserExtensionFolderSegment::FContentBrowserExtensionFolderSegment(const FString& InPathName)
{
	PathName = UE::ContentBrowserExtensions::Private::SanitizeSegment(InPathName);
	DisplayName = PathName;
}

FContentBrowserExtensionFolderSegment::FContentBrowserExtensionFolderSegment(const FString& InPathName, const FString& InDisplayName, bool bSanitizePathName)
{
	PathName = bSanitizePathName
		? UE::ContentBrowserExtensions::Private::SanitizeSegment(InPathName)
		: InPathName;
	DisplayName = InDisplayName;
}
