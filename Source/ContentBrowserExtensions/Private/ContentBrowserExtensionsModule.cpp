// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionsModule.h"

DEFINE_LOG_CATEGORY(LogContentBrowserExtensions);

void FContentBrowserExtensionsModule::StartupModule()
{
	UE_LOG(LogContentBrowserExtensions, Log, TEXT("ContentBrowserExtensions module starting up"));
}

void FContentBrowserExtensionsModule::ShutdownModule()
{
	UE_LOG(LogContentBrowserExtensions, Log, TEXT("ContentBrowserExtensions module shutting down"));
}

IMPLEMENT_MODULE(FContentBrowserExtensionsModule, ContentBrowserExtensions)
