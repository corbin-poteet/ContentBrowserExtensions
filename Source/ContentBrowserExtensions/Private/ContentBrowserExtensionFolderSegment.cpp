// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionFolderSegment.h"
#include "ContentBrowserExtensionsStatics.h"

FContentBrowserExtensionFolderSegment::FContentBrowserExtensionFolderSegment(const FString& InPathName, const bool bSanitizePathName)
{
	PathName = bSanitizePathName
		? UContentBrowserExtensionsStatics::SanitizeSegment(InPathName)
		: InPathName;
	DisplayName = PathName;
}

FContentBrowserExtensionFolderSegment::FContentBrowserExtensionFolderSegment(const FString& InPathName, const FString& InDisplayName, const bool bSanitizePathName)
{
	PathName = bSanitizePathName
		? UContentBrowserExtensionsStatics::SanitizeSegment(InPathName)
		: InPathName;
	DisplayName = InDisplayName;
}
