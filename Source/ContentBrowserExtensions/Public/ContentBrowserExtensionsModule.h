// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

CONTENTBROWSEREXTENSIONS_API DECLARE_LOG_CATEGORY_EXTERN(LogContentBrowserExtensions, Log, All);

/**
 * ContentBrowserExtensions plugin module.
 *
 * Library plugin providing a base UContentBrowserDataSource subclass and a UEditorSubsystem
 * registry, intended for other editor plugins to subclass and register virtual content
 * directories in the editor's Content Browser.
 *
 * NOT to be confused with UE's FContentBrowserMenuExtender_* delegate family — this plugin
 * extends the data layer, not the menu/toolbar layer.
 *
 * See:
 *   - UContentBrowserExtensionDataSource (Plan 02)  — base class
 *   - UContentBrowserExtensionsSubsystem (Plan 03)  — registration API
 *   - UE::ContentBrowserExtensions::SanitizeForPackagePath (Plan 02) — default sanitizer
 */
class FContentBrowserExtensionsModule : public IModuleInterface
{
public:
	// ~IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// ~End IModuleInterface
};

/**
 * Reserved namespace for free helper functions in this plugin.
 *
 * UCLASS / USTRUCT types CANNOT live in nested namespaces (UHT limitation), so they remain
 * top-level with the `ContentBrowserExtension` prefix. Free helpers (sanitizers, FName
 * generators, path utilities) go inside this namespace per UE 5.x convention
 * (UE::AssetRegistry::, UE::String::, UE::Path::, etc.).
 *
 * Functions defined here are populated by Plan 02 (sanitize) and later plans.
 */
namespace UE::ContentBrowserExtensions
{
	// Free helpers are declared in their own headers (e.g. ContentBrowserExtensionsSanitize.h
	// for SanitizeForPackagePath, added in Plan 02). This block is reserved for header-only
	// helpers that don't warrant their own translation unit.
}
