// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionFolderSegment.h"
#include "ContentBrowserExtensionsSanitize.h"

FContentBrowserExtensionFolderSegment FContentBrowserExtensionFolderSegment::FromDisplayName(const FString& InDisplayName)
{
	FContentBrowserExtensionFolderSegment Out;
	Out.DisplayName = InDisplayName;

	// Sanitize as a single segment by wrapping in a leading slash (so the path-aware
	// sanitizer treats the input as a one-segment path), then strip the slash back.
	FString Sanitized = UE::ContentBrowserExtensions::SanitizeForPackagePath(FString(TEXT("/")) + InDisplayName);
	if (Sanitized.StartsWith(TEXT("/")))
	{
		Sanitized.RemoveAt(0);
	}
	Out.PathName = MoveTemp(Sanitized);

	return Out;
}
