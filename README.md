# ContentBrowserExtensions

An Unreal Engine 5.7 editor plugin that lets any other plugin register a
**virtual content directory** in the Content Browser by subclassing one base
`UCLASS` and calling one subsystem method.

Use it when your plugin produces logical "assets" that should appear in the
Content Browser tree (and respond to selection / drag-drop / right-click) but
don't live as on-disk `.uasset` files. Examples: importer previews, remote
asset listings, generated procedural content, tooling-side metadata browsers.

---

## Requirements

- **Unreal Engine 5.7** (uses `UContentBrowserDataSource`, `UEditorSubsystem`,
  `IContentBrowserDataModule`).
- **Editor only** — single editor module (`Type: Editor` in `.uplugin`). No
  Runtime / no separate test target.

---

## Installation

1. Drop this directory into your project's `Plugins/` folder (sibling to other
   plugins like `Plugins/MyConsumerPlugin/`).
2. Enable **ContentBrowserExtensions** in your `.uproject` (or it will be
   auto-discovered if you `Plugins[]`-list it from a dependent plugin's
   `.uplugin`).
3. From the consumer module's `*.Build.cs`, add the dependency:

   ```cs
   PublicDependencyModuleNames.AddRange(new[] { "ContentBrowserExtensions" });
   // or PrivateDependencyModuleNames if no public types reference it
   ```

4. From the consumer's `.uplugin`, declare the plugin dependency:

   ```json
   "Plugins": [ { "Name": "ContentBrowserExtensions", "Enabled": true } ]
   ```

---

## API

The plugin exposes five consumer-facing types (everything else is internal).
A typical consumer wires them up in this order:

```
1. Build a flat list of FContentBrowserExtensionAssetEntry
2. Build the mount-root path as FContentBrowserExtensionFolderSegment[]
3. NewObject a subclass of UContentBrowserExtensionDataSource
4. DataSource->InitializeWithAssets(segments, MoveTemp(entries))
5. Subsystem->RegisterDataSource(DataSource, ...)
```

### `FContentBrowserExtensionAssetEntry`

Your flat asset record. Each entry becomes one leaf item in the Content Browser
tree under the mount root.

```cpp
#include "ContentBrowserExtensionAssetEntry.h"

FContentBrowserExtensionAssetEntry Entry;
Entry.Path = TEXT("weapons/rifle/AK74");   // path under the mount root, '/'-separated
Entry.Type = FName(TEXT("MeshAsset"));     // type name shown in the CB type column
```

### `FContentBrowserExtensionFolderSegment`

A `(PathName, DisplayName)` pair for one segment of the mount-root hierarchy.
Use these when you want the on-disk path identifier to differ from the human-
readable label shown in the tree view.

```cpp
#include "ContentBrowserExtensionFolderSegment.h"

// Default — sanitize PathName as safety; DisplayName stored verbatim
FContentBrowserExtensionFolderSegment Root(TEXT("ThirdParty"), TEXT("Third Party"));
//   PathName    = "ThirdParty"            (idempotent under the sanitizer)
//   DisplayName = "Third Party"           (with space, shown in tree)

// Same string for both — common when you don't need a separate label
FContentBrowserExtensionFolderSegment Game(GameDisplayName, GameDisplayName);
//   PathName    = sanitized GameDisplayName  (e.g. "STAR WARS™ Battlefront™ II"
//                                              -> "StarWarsBattlefrontII")
//   DisplayName = GameDisplayName            (verbatim)

// Skip the safety sanitization — caller guarantees the PathName is already valid
FContentBrowserExtensionFolderSegment Cached(SerializedPath, FriendlyLabel, /*bSanitizePathName=*/ false);

// Single-arg shorthand — sanitize and use the result for both fields
FContentBrowserExtensionFolderSegment Simple(TEXT("MyData"));
//   PathName    = "MyData"
//   DisplayName = "MyData"
```

The default `SanitizeForPackagePath` strips spaces / TM marks / parens /
non-alphanumerics, joins space-separated words as PascalCase, preserves short
all-uppercase tokens (`II`, `IV`), and is **idempotent on mixed-case
identifiers** — already-cased PathNames pass through unchanged.

### `UContentBrowserExtensionDataSource`

Base `UCLASS` to subclass. Holds the path index built from your asset list
and surfaces it to the Content Browser. Most consumers only override the four
optional virtual hooks (see [Customization](#customization)).

```cpp
// MyDataSource.h
#include "ContentBrowserExtensionDataSource.h"
#include "MyDataSource.generated.h"

UCLASS(Transient, MinimalAPI)
class UMyDataSource : public UContentBrowserExtensionDataSource
{
    GENERATED_BODY()

    // Optional: override any of these (D-09 hooks)
    virtual bool UpdateThumbnail(const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail) override;
    virtual bool GetItemAttribute(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue) override;
    virtual bool EditItem(const FContentBrowserItemData& InItem) override;
    virtual bool AppendItemReference(const FContentBrowserItemData& InItem, FString& InOutStr) override;
};
```

Constructor + initialization:

```cpp
UMyDataSource* DS = NewObject<UMyDataSource>(GetTransientPackage(), NAME_None, RF_Transient);

// Build assets and segments
TArray<FContentBrowserExtensionAssetEntry> Entries = BuildMyAssets();
TArray<FContentBrowserExtensionFolderSegment> MountRoot = {
    FContentBrowserExtensionFolderSegment(TEXT("MyTool"),  TEXT("My Tool")),
    FContentBrowserExtensionFolderSegment(TEXT("MyData"),  TEXT("My Data")),
};

// Data-population only — DOES NOT call base-class Initialize or activate the data source
DS->InitializeWithAssets(MountRoot, MoveTemp(Entries));

// Optional: pull the resolved mount-root string back if you need it later
const FString MountRootPath = DS->GetMountRoot();   // e.g. "/MyTool/MyData"
```

There's also a string-based overload of `InitializeWithAssets(FString, ...)`
that derives segments by sanitizing the input string. Prefer the segment
overload when you need explicit control over `(PathName, DisplayName)`.

Other public methods on the base class:

```cpp
// Recursively collect leaf entries under a folder, optionally filtered by type.
TArray<FContentBrowserExtensionAssetEntry> Out;
TSet<FName> AllowedTypes = { FName(TEXT("MeshAsset")) };
DS->GetLeafAssetsUnderFolder(FName(TEXT("/MyTool/MyData/weapons")), AllowedTypes, Out);

// Sanitized mount-root path (post-segment-join).
const FString& Mount = DS->GetMountRoot();
```

### `UContentBrowserExtensionsSubsystem`

`UEditorSubsystem` that owns the registry of all live data sources, their
lifecycle, and the CB-framework activation handshake. Consumers always
register through this — never call `Initialize(true)` /
`ActivateDataSource(...)` directly on the data source.

```cpp
#include "ContentBrowserExtensionsSubsystem.h"
#include "Editor.h"

UContentBrowserExtensionsSubsystem* Sub =
    GEditor->GetEditorSubsystem<UContentBrowserExtensionsSubsystem>();
if (!Sub)
{
    // Editor not available yet — defer or surface as an error.
    return;
}

FContentBrowserExtensionRegistrationHandle Handle;
const ERegisterResult Result = Sub->RegisterDataSource(
    DS,
    FName(TEXT("MyConsumerPlugin")),               // ConsumerId — bookkeeping only
    DS->GetMountRoot(),                            // VirtualRootPath, e.g. "/MyTool/MyData"
    FText::FromString(TEXT("My Data")),            // Leaf-folder display label
    Handle);

switch (Result)
{
case ERegisterResult::Success:
    // Save Handle — pass to UnregisterDataSource on tear-down.
    break;
case ERegisterResult::AlreadyRegistered_LeafCollision:
    // Another consumer (or you) already owns this exact virtual root.
    break;
case ERegisterResult::InvalidArguments:
    // Empty path, contains "..", drive letter, etc.
    break;
case ERegisterResult::CBSubsystemUnavailable:
    // IContentBrowserDataModule subsystem wasn't ready — retry later.
    break;
}

// ...later, on shutdown / hot-reload / mode switch:
Sub->UnregisterDataSource(Handle);
```

`RegisterDataSource` performs in sequence: rename to a subsystem-assigned
`FName`, call the base-class `Initialize(true)`, then activate via the CB
data subsystem. `UnregisterDataSource` reverses this and is **idempotent on
invalid handles** (safe to call twice).

The subsystem also rejects invalid `VirtualRootPath` inputs (`..`, drive
letters, empty) up front to mitigate path-traversal misuse.

### `UE::ContentBrowserExtensions::SanitizeForPackagePath`

The default sanitizer used internally. Exposed because some consumers want to
pre-sanitize their own strings or use it as a one-shot helper.

```cpp
#include "ContentBrowserExtensionsSanitize.h"

FString Clean = UE::ContentBrowserExtensions::SanitizeForPackagePath(TEXT("/My Folder/Star Wars II"));
// "/MyFolder/StarWarsII"

FString Idem = UE::ContentBrowserExtensions::SanitizeForPackagePath(TEXT("/ThirdParty/MyData"));
// "/ThirdParty/MyData"   (mixed-case preserved — sanitizer is idempotent)
```

---

## Customization

`UContentBrowserExtensionDataSource` provides four `virtual` hooks for the
consumer to override (D-09). Defaults handle the common case; override only
the ones you need.

| Hook | Default | Override to... |
|------|---------|---------------|
| `UpdateThumbnail` | returns false | Render a custom thumbnail (class-icon, fetched bitmap, etc.) |
| `GetItemAttribute` | returns `ItemTypeName` / `ItemTypeDisplayName` / `ItemDiskSize=0` | Surface custom CB attributes (file size, modified date, badges) |
| `EditItem` | returns false | Open your editor (importer dialog, external launcher, etc.) on double-click |
| `AppendItemReference` | appends asset path | Format reference strings for drag-drop into other CB widgets |

Call `Super::Foo(...)` first if you want to chain — defaults are well-behaved.

---

## Lifecycle contract

```
Module load (editor startup)
    └── UEditorSubsystem collection auto-instantiates UContentBrowserExtensionsSubsystem
        └── Initialize() runs (no-op until consumers register)

Consumer call site (anytime after editor startup)
    ├── NewObject<UMyDataSource>(...)
    ├── DataSource->InitializeWithAssets(segments, entries)   // data only
    └── Subsystem->RegisterDataSource(DS, ...)                // owns: Rename + Initialize(true) + ActivateDataSource
                                                              // root-anchors DS in subsystem's UPROPERTY array

Consumer tear-down (shutdown / hot-reload / mode switch)
    └── Subsystem->UnregisterDataSource(Handle)               // owns: deactivate + Rename-to-transient + MarkAsGarbage

Module unload (editor exit)
    └── Subsystem->Deinitialize() unregisters anything still in the registry
```

**You should not** call `AddToRoot()` on the data source — the subsystem's
`UPROPERTY()` array roots it for you. Likewise don't call `Initialize(true)`
or `ActivateDataSource(...)` directly; the subsystem owns the handshake.

---

## Notes

- **Shared parents merge.** Multiple consumers can register data sources whose
  virtual root paths share a common parent (e.g. one at `/ThirdParty/SWBF2`,
  another at `/ThirdParty/SkyrimMods`). UE's Content Browser merges sibling
  folder items contributed by different data sources at the same virtual path,
  so they appear together under a single `/ThirdParty/` node.
- **Leaf collisions are rejected.** Two consumers registering the **exact
  same** sanitized `VirtualRootPath` is a programming error — the second call
  returns `AlreadyRegistered_LeafCollision`.
- **Display vs path casing.** Folder labels are derived from
  `DisplayName` when supplied; the path bar / breadcrumb uses the sanitized
  `PathName`. Use the segment struct to control both independently.

---

## License

Copyright Epic Games, Inc. All Rights Reserved.
