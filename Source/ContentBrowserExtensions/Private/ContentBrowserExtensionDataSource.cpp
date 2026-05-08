// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionDataSource.h"

#include "ContentBrowserExtensionsModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetThumbnail.h"
#include "ContentBrowserDataFilter.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserItemData.h"
#include "Editor.h"
#include "Internationalization/Text.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"

#define LOCTEXT_NAMESPACE "UContentBrowserExtensionDataSource"

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

/**
 * Strip the virtual "/All" prefix that the CB subsystem adds at runtime.
 * FName comparison is case-insensitive, so no lowercasing needed.
 */
static FName StripAllPrefix(const FName InPath)
{
	FString PathStr = InPath.ToString();
	if (PathStr.StartsWith(TEXT("/All/"), ESearchCase::IgnoreCase))
	{
		return FName(*PathStr.RightChop(4)); // strip "/All"
	}
	if (PathStr.Equals(TEXT("/All"), ESearchCase::IgnoreCase))
	{
		return FName(TEXT("/"));
	}
	return InPath;
}

/**
 * Walks the path components of `Mount` strictly under `PathRoot`, emitting each
 * accumulated ancestor path root-to-leaf (the FINAL emission is `Mount` itself).
 *
 * Termination is controlled by the sink's bool return — return false to stop
 * early. The helper does NOT special-case Mount inclusion; callers that want
 * to exclude Mount must filter it in the sink (see BuildRootPathVirtualTree).
 *
 * Three callsites with three shapes:
 *   1. BuildRootPathVirtualTree (~:188): PathRoot="/", emit ancestors strictly
 *      between "/" and Mount; sink returns false on Mount to stop before it.
 *   2. EnumerateItemsMatchingFilter recursive (~:368): PathRoot=filter root,
 *      emit ancestors between PathRoot and Mount; sink returns false on Mount
 *      because the FolderPaths loop emits Mount itself.
 *   3. EnumerateItemsMatchingFilter non-recursive (~:314): PathRoot=ancestor
 *      of Mount, want only the IMMEDIATE child segment; sink returns false
 *      after the first emission.
 *
 * Built on `FPathViews::TryMakeChildPathRelativeTo` (boundary-aware parent
 * check) + `FPathViews::SplitFirstComponent` (per-segment iteration).
 *
 * Examples (with a sink that always returns true):
 *   PathRoot="/",     Mount="/Root/Sub" → emits "/Root", "/Root/Sub"
 *   PathRoot="/Root", Mount="/Root/Sub" → emits "/Root/Sub"
 *   PathRoot="/Root", Mount="/Root/Sub/Inner" → emits "/Root/Sub", "/Root/Sub/Inner"
 *
 * No emission is made if `PathRoot` is not a parent of (or equal to) `Mount`.
 */
static void EmitMountAncestorsBetween(FStringView PathRoot, FStringView Mount,
	TFunctionRef<bool(FStringView Accumulated)> Sink)
{
	FStringView Rel;
	if (!FPathViews::TryMakeChildPathRelativeTo(Mount, PathRoot, Rel))
	{
		return;
	}

	TStringBuilder<256> Acc;
	Acc << PathRoot;
	FStringView Cursor = Rel;
	while (!Cursor.IsEmpty())
	{
		FStringView FirstSeg, Remainder;
		FPathViews::SplitFirstComponent(Cursor, FirstSeg, Remainder);
		if (FirstSeg.IsEmpty())
		{
			break;
		}
		FPathViews::Append(Acc, FirstSeg);
		if (!Sink(Acc.ToView()))
		{
			return;
		}
		Cursor = Remainder;
	}
}

// ---------------------------------------------------------------------------
// Initialization / Shutdown
// ---------------------------------------------------------------------------

void UContentBrowserExtensionDataSource::InitializeWithAssets(
	const FString& InMountRootPath,
	TArray<FContentBrowserExtensionAssetEntry>&& InAssets)
{
	// Data-population only. Lifecycle (base-class init + CB-framework activation) is owned
	// by UContentBrowserExtensionsSubsystem::RegisterDataSource (Plan 03 / D-02).
	PathIndex.BuildFromFlatList(InMountRootPath, InAssets);
	bDiscoveryComplete = true;

	UE_LOG(LogContentBrowserExtensions, Verbose,
		TEXT("InitializeWithAssets: mount='%s' folders=%d leaves=%d (data-population only; lifecycle owned by UContentBrowserExtensionsSubsystem::RegisterDataSource per D-02)"),
		*PathIndex.MountRoot, PathIndex.FolderPaths.Num(), PathIndex.LeafItems.Num());
}

void UContentBrowserExtensionDataSource::InitializeWithAssets(
	const TArray<FContentBrowserExtensionFolderSegment>& InMountRootSegments,
	TArray<FContentBrowserExtensionAssetEntry>&& InAssets)
{
	// Data-population only. Same lifecycle contract as the string overload — subsystem
	// owns Initialize/ActivateDataSource per D-02. This overload trusts consumer-supplied
	// PathName/DisplayName values and skips the sanitizer roundtrip.
	PathIndex.BuildFromFolderSegments(InMountRootSegments, InAssets);
	bDiscoveryComplete = true;

	UE_LOG(LogContentBrowserExtensions, Verbose,
		TEXT("InitializeWithAssets(segments): mount='%s' folders=%d leaves=%d segs=%d"),
		*PathIndex.MountRoot, PathIndex.FolderPaths.Num(), PathIndex.LeafItems.Num(), InMountRootSegments.Num());
}

void UContentBrowserExtensionDataSource::Shutdown()
{
	if (!IsInitialized())
	{
		return; // Already shut down (e.g. BeginDestroy after explicit Shutdown)
	}

	UE_LOG(LogContentBrowserExtensions, Verbose, TEXT(" Shutdown called."));

	// CB-framework deactivation lives in the subsystem's Unregister path (per D-02).
	// The base class only resets its data-population state here.
	Super::Shutdown();
	PathIndex.Reset();
	bDiscoveryComplete = false;
}

// ---------------------------------------------------------------------------
// Folder enumeration (Import All-style support)
// ---------------------------------------------------------------------------

void UContentBrowserExtensionDataSource::GetLeafAssetsUnderFolder(
	FName InternalFolderPath,
	const TSet<FName>& AllowedTypes,
	TArray<FContentBrowserExtensionAssetEntry>& OutEntries) const
{
	const FString FolderDir = InternalFolderPath.ToString();

	// F16: pre-size to the upper bound so the per-iteration Add doesn't re-grow the array.
	OutEntries.Reserve(OutEntries.Num() + PathIndex.LeafItems.Num());

	for (const auto& [LeafPath, Entry] : PathIndex.LeafItems)
	{
		const FString LeafStr = LeafPath.ToString();
		if (!FPaths::IsUnderDirectory(LeafStr, FolderDir))
		{
			continue;
		}
		if (!AllowedTypes.IsEmpty() && !AllowedTypes.Contains(Entry.Type))
		{
			continue;
		}
		OutEntries.Add(Entry);
	}
}

// ---------------------------------------------------------------------------
// Virtual path tree
// ---------------------------------------------------------------------------

void UContentBrowserExtensionDataSource::BuildRootPathVirtualTree()
{
	Super::BuildRootPathVirtualTree();

	if (!PathIndex.MountRoot.IsEmpty())
	{
		UE_LOG(LogContentBrowserExtensions, Verbose, TEXT(" BuildRootPathVirtualTree: registering mount root '%s'"),
			*PathIndex.MountRoot);

		// Register ancestor paths (e.g. "/Root") as internal paths so the CB
		// subsystem routes GetItemAtPath through our EnumerateItemsAtPath (which
		// provides display names from DisplayNameMap) instead of creating generic
		// virtual folder items with sanitized path segments as labels.
		//
		// Order: emit ancestors root-to-leaf (e.g. "/Root" before "/Root/Sub"),
		// then emit Mount itself as the final RootPathAdded call. The
		// EmitMountAncestorsBetween helper yields up-to-and-including Mount; we stop
		// just before Mount to keep the explicit RootPathAdded(Mount) call site
		// (preserves blame readability + matches the original two-step pattern).
		const FString& Mount = PathIndex.MountRoot;
		EmitMountAncestorsBetween(FStringView(TEXT("/")), Mount,
			[this, &Mount](FStringView Accumulated) -> bool
			{
				if (Accumulated.Equals(Mount, ESearchCase::IgnoreCase))
				{
					return false; // stop before emitting Mount itself
				}
				// F1: pass FStringView through directly (UContentBrowserDataSource::RootPathAdded
				// is declared `void RootPathAdded(const FStringView InInternalPath)` —
				// see ContentBrowserDataSource.h:941). No FString round-trip needed.
				RootPathAdded(Accumulated);
				return true;
			});

		RootPathAdded(PathIndex.MountRoot);
	}
	else
	{
		// Verbose (not Warning): empty MountRoot is a benign state during test fixtures
		// that build a DataSource without InitializeWithAssets. The data source simply
		// renders nothing in that state — no user-visible failure mode warrants Warning.
		UE_LOG(LogContentBrowserExtensions, Verbose, TEXT(" BuildRootPathVirtualTree: MountRoot is empty!"));
	}
}

// ---------------------------------------------------------------------------
// Filter compilation
// ---------------------------------------------------------------------------

void UContentBrowserExtensionDataSource::CompileFilter(
	const FName InPath,
	const FContentBrowserDataFilter& InFilter,
	FContentBrowserDataCompiledFilter& OutCompiledFilter)
{
	// Strip the /All/ prefix the CB adds at runtime.
	const FName InternalPath = StripAllPrefix(InPath);

	// Accept if the request path is an ancestor of our mount root OR under it.
	// The CB passes "/" as the root path for all data sources during folder tree population.
	// FPathViews::IsParentPathOf is boundary-aware (case-insensitive, treats / and \ as
	// equivalent, and ignores trailing separators) so it correctly rejects sibling-prefix
	// collisions like "/Foo" vs "/FooBar". It also returns true when Parent == Child.
	const FString& MountRoot = PathIndex.MountRoot;
	const FString InternalStr = InternalPath.ToString();
	const bool bIsUnderMount      = FPathViews::IsParentPathOf(MountRoot, InternalStr);
	const bool bIsAncestorOfMount = FPathViews::IsParentPathOf(InternalStr, MountRoot);
	if (!bIsUnderMount && !bIsAncestorOfMount)
	{
		return;
	}

	UE_LOG(LogContentBrowserExtensions, Verbose, TEXT(" CompileFilter: InPath='%s' internal='%s' folders=%d files=%d"),
		*InPath.ToString(), *InternalStr,
		EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders) ? 1 : 0,
		EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles) ? 1 : 0);

	// Retrieve (or create) the per-data-source filter list, then get our custom filter.
	FContentBrowserExtensionCompiledFilter& CompiledFilter =
		OutCompiledFilter.CompiledFilters.FindOrAdd(this).FindOrAddFilter<FContentBrowserExtensionCompiledFilter>();

	// Store the internal path root and recursion flag for enumeration scoping.
	CompiledFilter.PathRoot = InternalPath;
	CompiledFilter.bRecursive = InFilter.bRecursivePaths;

	// Determine whether folders and files should be included based on the CB item type filter.
	CompiledFilter.bIncludeFolders = EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders);
	CompiledFilter.bIncludeFiles   = EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles);

	// Check if the Misc category is allowed (extension file items use Category_Misc).
	const bool bIncludeMisc = EnumHasAnyFlags(InFilter.ItemCategoryFilter, EContentBrowserItemCategoryFilter::IncludeMisc);
	if (!bIncludeMisc)
	{
		// Misc items not wanted — suppress files.
		CompiledFilter.bIncludeFiles = false;
	}

	// Search text is not injected via CompileFilter in the standard CB flow; it is applied
	// per-item via DoesItemPassFilter. Leave SearchText empty here.
	CompiledFilter.SearchText.Empty();
}

// ---------------------------------------------------------------------------
// Item enumeration
// ---------------------------------------------------------------------------

void UContentBrowserExtensionDataSource::EnumerateItemsMatchingFilter(
	const FContentBrowserDataCompiledFilter& InFilter,
	TFunctionRef<bool(FContentBrowserItemData&&)> InCallback)
{
	const FContentBrowserDataFilterList* FilterList = InFilter.CompiledFilters.Find(this);
	if (!FilterList)
	{
		UE_LOG(LogContentBrowserExtensions, Verbose, TEXT(" EnumerateItemsMatchingFilter: no FilterList for this source"));
		return;
	}

	const FContentBrowserExtensionCompiledFilter* Filter = FilterList->FindFilter<FContentBrowserExtensionCompiledFilter>();
	if (!Filter)
	{
		UE_LOG(LogContentBrowserExtensions, Verbose, TEXT(" EnumerateItemsMatchingFilter: no FContentBrowserExtensionCompiledFilter"));
		return;
	}

	const FString PathRootStr = Filter->PathRoot.ToString();
	UE_LOG(LogContentBrowserExtensions, Verbose, TEXT(" EnumerateItemsMatchingFilter: PathRoot='%s' recursive=%d folders=%d files=%d"),
		*PathRootStr, Filter->bRecursive ? 1 : 0, Filter->bIncludeFolders ? 1 : 0, Filter->bIncludeFiles ? 1 : 0);

	if (!Filter->bRecursive)
	{
		// Non-recursive: return only direct children of PathRoot (like EnumerateItemsAtPath).
		// The PathRoot may be "/" (ancestor of mount) — in that case return our mount root folder.
		// IsParentPathOf returns true when Parent==Child, so explicitly exclude that case to
		// preserve the "ancestor only, not equal" semantics of the original boundary check.
		const FName PathRootName = Filter->PathRoot;
		const bool bIsAncestorOfMount = FPathViews::IsParentPathOf(PathRootStr, PathIndex.MountRoot)
			&& !PathRootStr.Equals(PathIndex.MountRoot, ESearchCase::IgnoreCase);

		if (bIsAncestorOfMount)
		{
			// PathRoot is above our mount (e.g. "/" or "/Root").
			// Return the immediate child folder segment under PathRoot as a virtual folder.
			// e.g. PathRoot="/" → return "/Root"
			//      PathRoot="/Root" → return "/Root/Sub"
			if (Filter->bIncludeFolders)
			{
				// Emit only the FIRST ancestor segment after PathRoot (which may be Mount
				// itself if PathRoot is the immediate parent of Mount). Stop the helper
				// after one emission by returning false from the sink.
				EmitMountAncestorsBetween(PathRootStr, PathIndex.MountRoot,
					[this, &InCallback](FStringView ChildPath) -> bool
					{
						FContentBrowserItemData Item = MakeFolderItem(FName(ChildPath));
						(void)InCallback(MoveTemp(Item)); // single emission — callback's stop signal has no follow-up to skip
						return false; // stop after the first ancestor
					});
			}
			return;
		}

		// PathRoot is at or under our mount — return direct children via ChildrenMap.
		const TArray<FName>* Children = PathIndex.ChildrenMap.Find(PathRootName);
		if (!Children)
		{
			return;
		}

		for (const FName& ChildPath : *Children)
		{
			// A path can be both a folder (it has children) and a leaf asset (it is itself
			// an asset) when an asset shares the same name as its containing folder.
			// Check both independently — mirroring the pattern in EnumerateItemsAtPath.
			if (Filter->bIncludeFolders && PathIndex.FolderPaths.Contains(ChildPath))
			{
				FContentBrowserItemData Item = MakeFolderItem(ChildPath);
				if (!InCallback(MoveTemp(Item))) { return; }
			}

			if (Filter->bIncludeFiles)
			{
				const FContentBrowserExtensionAssetEntry* Entry = PathIndex.LeafItems.Find(ChildPath);
				if (Entry)
				{
					FContentBrowserItemData Item = MakeFileItem(ChildPath, *Entry);
					if (!InCallback(MoveTemp(Item))) { return; }
				}
			}
		}
		return;
	}

	// Recursive: return all items under PathRoot.
	if (Filter->bIncludeFolders)
	{
		// Emit ancestor folders strictly between PathRoot and MountRoot so the folder tree
		// gets items with proper display names (instead of creating placeholder parents
		// that fall back to the sanitized path segment).
		//
		// The mount-root itself is in FolderPaths and emitted by the main loop below — we
		// stop just before reaching it. EmitMountAncestorsBetween yields up-to-and-including
		// Mount root-to-leaf; the sink filters out the Mount emission.
		const FString& Mount = PathIndex.MountRoot;
		bool bEarlyReturn = false;
		EmitMountAncestorsBetween(PathRootStr, Mount,
			[this, &Mount, &InCallback, &bEarlyReturn](FStringView Accumulated) -> bool
			{
				if (Accumulated.Equals(Mount, ESearchCase::IgnoreCase))
				{
					return false; // mount-root handled by FolderPaths loop below
				}
				FContentBrowserItemData Item = MakeFolderItem(FName(Accumulated));
				if (!InCallback(MoveTemp(Item)))
				{
					bEarlyReturn = true;
					return false; // CB requested stop
				}
				return true;
			});
		if (bEarlyReturn) { return; }

		for (const FName& FolderPath : PathIndex.FolderPaths)
		{
			// T4 boundary-bug fix: previous code used raw `StartsWith(PathRootStr)` which
			// matched sibling-prefix folders (e.g. "/X/FooBar/..." against PathRoot="/X/Foo").
			// FPathViews::IsParentPathOf is boundary-aware and rejects sibling prefixes.
			const FString FolderStr = FolderPath.ToString();
			if (!FPathViews::IsParentPathOf(PathRootStr, FolderStr))
			{
				continue;
			}
			FContentBrowserItemData Item = MakeFolderItem(FolderPath);
			if (!InCallback(MoveTemp(Item))) { return; }
		}
	}

	if (Filter->bIncludeFiles)
	{
		for (const auto& [LeafPath, Entry] : PathIndex.LeafItems)
		{
			// T4 boundary-bug fix (mirror of the folder loop above).
			const FString LeafStr = LeafPath.ToString();
			if (!FPathViews::IsParentPathOf(PathRootStr, LeafStr))
			{
				continue;
			}
			FContentBrowserItemData Item = MakeFileItem(LeafPath, Entry);
			if (!InCallback(MoveTemp(Item))) { return; }
		}
	}
}

void UContentBrowserExtensionDataSource::EnumerateItemsAtPath(
	const FName InPath,
	const EContentBrowserItemTypeFilter InItemTypeFilter,
	TFunctionRef<bool(FContentBrowserItemData&&)> InCallback)
{
	// Strip the /All/ prefix the CB adds at runtime.
	const FName InternalPath = StripAllPrefix(InPath);

	// Guard: only serve items for paths under or ancestral to our mount root.
	// FPathViews::IsParentPathOf is boundary-aware (matches the original raw StartsWith
	// + boundary-character checks but rejects sibling-prefix collisions like "/Foo" vs
	// "/FooBar"). It returns true when Parent == Child, so it covers the prior
	// `StartsWith || Equals` pair in a single call.
	const FString InternalStr = InternalPath.ToString();
	const FString& MountRoot = PathIndex.MountRoot;
	if (MountRoot.IsEmpty())
	{
		return;
	}
	const bool bIsUnderOrAtMount  = FPathViews::IsParentPathOf(MountRoot, InternalStr);
	const bool bIsAncestorOfMount = !bIsUnderOrAtMount
		&& FPathViews::IsParentPathOf(InternalStr, MountRoot);
	if (!bIsUnderOrAtMount && !bIsAncestorOfMount)
	{
		return;
	}

	// If this is an ancestor of our mount root (e.g. "/Root"), return it as a
	// folder item so breadcrumbs can get the display name from DisplayNameMap.
	if (bIsAncestorOfMount)
	{
		if (EnumHasAnyFlags(InItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
		{
			FContentBrowserItemData Item = MakeFolderItem(InternalPath);
			(void)InCallback(MoveTemp(Item)); // single emission — callback's stop signal has no follow-up to skip
		}
		return;
	}

	// EnumerateItemsAtPath returns items whose virtual path IS InPath (not children).
	// Check if this path is a folder in our index.
	if (EnumHasAnyFlags(InItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
	{
		if (PathIndex.FolderPaths.Contains(InternalPath))
		{
			FContentBrowserItemData Item = MakeFolderItem(InternalPath);
			if (!InCallback(MoveTemp(Item)))
			{
				return;
			}
		}
	}

	// Check if this path is a leaf (file) in our index.
	if (EnumHasAnyFlags(InItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles))
	{
		const FContentBrowserExtensionAssetEntry* Entry = PathIndex.LeafItems.Find(InternalPath);
		if (Entry)
		{
			FContentBrowserItemData Item = MakeFileItem(InternalPath, *Entry);
			(void)InCallback(MoveTemp(Item)); // single emission — callback's stop signal has no follow-up to skip
		}
	}
}

// ---------------------------------------------------------------------------
// Discovery status
// ---------------------------------------------------------------------------

bool UContentBrowserExtensionDataSource::IsDiscoveringItems(FText* OutStatus)
{
	if (!bDiscoveryComplete)
	{
		if (OutStatus)
		{
			*OutStatus = LOCTEXT("ContentBrowserExtensionsDiscovering", "Loading assets...");
		}
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Per-item filter check
// ---------------------------------------------------------------------------

bool UContentBrowserExtensionDataSource::DoesItemPassFilter(
	const FContentBrowserItemData& InItem,
	const FContentBrowserDataCompiledFilter& InFilter)
{
	const FContentBrowserDataFilterList* FilterList = InFilter.CompiledFilters.Find(this);
	if (!FilterList)
	{
		return false;
	}

	const FContentBrowserExtensionCompiledFilter* Filter = FilterList->FindFilter<FContentBrowserExtensionCompiledFilter>();
	if (!Filter)
	{
		return false;
	}

	if (InItem.IsFolder())
	{
		return Filter->bIncludeFolders;
	}

	if (!InItem.IsFile())
	{
		return false;
	}

	if (!Filter->bIncludeFiles)
	{
		return false;
	}

	const FContentBrowserExtensionFileItemPayload* Payload = GetFilePayload(InItem);
	if (!Payload)
	{
		return false;
	}

	// Search text filter.
	if (!Filter->SearchText.IsEmpty())
	{
		FString LowerPath = Payload->GetAssetPath().ToLower();
		if (!LowerPath.Contains(Filter->SearchText))
		{
			return false;
		}
	}

	// Type filter.
	if (!Filter->TypeFilters.IsEmpty() && !Filter->TypeFilters.Contains(Payload->GetAssetType()))
	{
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Item attributes (default impl — virtual; subclasses may override per D-09)
// ---------------------------------------------------------------------------

bool UContentBrowserExtensionDataSource::GetItemAttribute(
	const FContentBrowserItemData& InItem,
	const bool /*InIncludeMetaData*/,
	const FName InAttributeKey,
	FContentBrowserItemDataAttributeValue& OutAttributeValue)
{
	const FContentBrowserExtensionFileItemPayload* Payload = GetFilePayload(InItem);
	if (!Payload)
	{
		return false;
	}

	if (InAttributeKey == ContentBrowserItemAttributes::ItemTypeName)
	{
		OutAttributeValue.SetValue(Payload->GetAssetType());
		return true;
	}

	if (InAttributeKey == ContentBrowserItemAttributes::ItemTypeDisplayName)
	{
		OutAttributeValue.SetValue(FText::FromName(Payload->GetAssetType()));
		return true;
	}

	if (InAttributeKey == ContentBrowserItemAttributes::ItemDiskSize)
	{
		// Flat schema does not carry file size — report 0.
		OutAttributeValue.SetValue(static_cast<int64>(0));
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Thumbnail (default impl — virtual; subclasses may override per D-09)
// ---------------------------------------------------------------------------

bool UContentBrowserExtensionDataSource::UpdateThumbnail(
	const FContentBrowserItemData& /*InItem*/, FAssetThumbnail& /*InThumbnail*/)
{
	// Default: no custom thumbnail. CB falls back to the class icon.
	// Subclasses override with consumer-specific thumbnail logic.
	return false;
}

// ---------------------------------------------------------------------------
// Item reference (Copy Path) — virtual; subclasses may override per D-09
// ---------------------------------------------------------------------------

bool UContentBrowserExtensionDataSource::AppendItemReference(
	const FContentBrowserItemData& InItem,
	FString& InOutStr)
{
	const FContentBrowserExtensionFileItemPayload* Payload = GetFilePayload(InItem);
	if (!Payload)
	{
		return false;
	}

	if (!InOutStr.IsEmpty())
	{
		InOutStr += LINE_TERMINATOR;
	}
	InOutStr += Payload->GetAssetPath();
	return true;
}

// ---------------------------------------------------------------------------
// Double-click — virtual; subclasses may override per D-09
// ---------------------------------------------------------------------------

bool UContentBrowserExtensionDataSource::EditItem(const FContentBrowserItemData& /*InItem*/)
{
	// Default: no-op. CB will not invoke any editor.
	// Subclasses override with consumer-specific edit/import logic.
	return false;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

FContentBrowserItemData UContentBrowserExtensionDataSource::MakeFolderItem(FName InternalPath)
{
	// Convert internal path to the virtual path the CB framework expects.
	FName VirtualPath;
	TryConvertInternalPathToVirtual(InternalPath, VirtualPath);

	// Extract the leaf name (last path segment) from the virtual path.
	FString PathStr = VirtualPath.ToString();
	FString LeafStr(FPathViews::GetPathLeaf(PathStr));
	FName LeafName(*LeafStr);

	// Check DisplayNameMap for an original (unsanitized) display name, or for a
	// casing-upgrade override (when the FName was first stored with a lowercase form
	// from the bundle TOC and a later mixed-case entry upgraded it).
	// The map key is the sanitized internal path; the value is the best-cased segment name.
	FString DisplayStr = LeafStr;
	if (const FString* MappedName = PathIndex.DisplayNameMap.Find(InternalPath))
	{
		DisplayStr = *MappedName;
	}

	// Rebuild LeafName from DisplayStr so the path bar / breadcrumb (which uses the
	// item FName, not the display FText) also shows the correct mixed-case form.
	// Without this, the FName carries the first-inserted (possibly lowercase) string
	// even though DisplayStr has been upgraded via DisplayNameMap.
	FName CasedLeafName = DisplayStr.Equals(LeafStr, ESearchCase::CaseSensitive)
		? LeafName
		: FName(*DisplayStr);

	return FContentBrowserItemData(
		this,
		EContentBrowserItemFlags::Type_Folder,
		VirtualPath,
		CasedLeafName,
		FText::FromString(DisplayStr),
		MakeShared<FContentBrowserExtensionFolderItemPayload>(InternalPath),
		InternalPath
	);
}

FContentBrowserItemData UContentBrowserExtensionDataSource::MakeFileItem(
	FName InternalPath,
	const FContentBrowserExtensionAssetEntry& Entry)
{
	// Convert internal path to the virtual path the CB framework expects.
	FName VirtualPath;
	TryConvertInternalPathToVirtual(InternalPath, VirtualPath);

	// Extract the leaf name (last path segment) directly from Entry.Path — NOT from
	// VirtualPath.ToString(). VirtualPath goes through the FName display table which is
	// case-insensitive and may return the casing of a same-named folder at the same path
	// (e.g. if a folder "art_exterior" and a leaf "Art_Exterior" share a case-insensitive
	// FName key, ToString() can return either one's casing). Entry.Path carries the original
	// casing as supplied by the consumer and is the authoritative source for leaf names.
	FString LeafStr(FPathViews::GetPathLeaf(Entry.Path));
	FName LeafName(*LeafStr);

	return FContentBrowserItemData(
		this,
		EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Misc,
		VirtualPath,
		LeafName,
		FText::FromString(LeafStr),
		MakeShared<FContentBrowserExtensionFileItemPayload>(Entry),
		InternalPath
	);
}

const FContentBrowserExtensionFileItemPayload* UContentBrowserExtensionDataSource::GetFilePayload(
	const FContentBrowserItemData& InItem) const
{
	if (!InItem.IsFile())
	{
		return nullptr;
	}

	const TSharedPtr<const IContentBrowserItemDataPayload>& Payload = InItem.GetPayload();
	if (!Payload.IsValid())
	{
		return nullptr;
	}

	return static_cast<const FContentBrowserExtensionFileItemPayload*>(Payload.Get());
}

#undef LOCTEXT_NAMESPACE
