// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionPathIndex.h"
#include "ContentBrowserExtensionsStatics.h"

#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"

// ---------------------------------------------------------------------------
// FContentBrowserExtensionPathIndex
// ---------------------------------------------------------------------------

void FContentBrowserExtensionPathIndex::BuildFromFlatList(const FString& InMountRoot, const TArray<FContentBrowserExtensionAssetEntry>& FlatList, bool bSanitizePathNames)
{
	// Derive (PathName, DisplayName) segments by sanitizing the input and pairing each
	// sanitized segment with its original form, then delegate to the structured overload.
	const FString Sanitized = UContentBrowserExtensionsStatics::SanitizeForPackagePath(InMountRoot);

	TArray<FString> OrigSegments;
	TArray<FString> SanitizedSegments;
	InMountRoot.ParseIntoArray(OrigSegments, TEXT("/"), /*bCullEmpty=*/false);
	Sanitized.ParseIntoArray(SanitizedSegments, TEXT("/"), /*bCullEmpty=*/false);

	TArray<FContentBrowserExtensionFolderSegment> Segments;
	Segments.Reserve(FMath::Min(OrigSegments.Num(), SanitizedSegments.Num()));

	const int32 SegCount = FMath::Min(OrigSegments.Num(), SanitizedSegments.Num());
	for (int32 i = 0; i < SegCount; ++i)
	{
		// Skip the empty leading-slash segment and any segment that the sanitizer dropped
		// (e.g. an entire segment composed of stripped characters).
		if (OrigSegments[i].IsEmpty() || SanitizedSegments[i].IsEmpty())
		{
			continue;
		}
		Segments.Emplace(SanitizedSegments[i], OrigSegments[i], bSanitizePathNames);
	}

	BuildFromFolderSegments(Segments, FlatList);
}

void FContentBrowserExtensionPathIndex::BuildFromFolderSegments(
	const TArray<FContentBrowserExtensionFolderSegment>& InMountRootSegments,
	const TArray<FContentBrowserExtensionAssetEntry>& FlatList)
{
	Reset();

	// Build the mount root by joining the path-form (.PathName) of each segment with '/'
	// and seed DisplayNameMap from any segment whose .DisplayName differs from .PathName.
	// Trust the consumer's PathName values verbatim — no sanitizer applied here. (Use
	// FContentBrowserExtensionFolderSegment::FromDisplayName at the consumer call site if
	// you want the plugin's default sanitizer to derive PathName from DisplayName.)
	//
	// Use TStringBuilder + FPathViews::Append for the join. We track whether any segment
	// was actually emitted so that an empty/all-empty segment list yields MountRoot="" —
	// matching the original `RunningPath += TEXT("/"); RunningPath += Seg.PathName;` behavior.
	// (FPathViews::Append handles separator dedup: when the builder ends in '/', no extra
	// separator is added; otherwise it inserts one before the appended segment. We seed
	// with a leading "/" only after the first non-empty segment is observed.)
	TStringBuilder<128> RunningBuilder;
	bool bAnyEmitted = false;
	for (const FContentBrowserExtensionFolderSegment& Seg : InMountRootSegments)
	{
		if (Seg.PathName.IsEmpty())
		{
			continue;
		}

		if (!bAnyEmitted)
		{
			RunningBuilder.AppendChar(TEXT('/'));
			bAnyEmitted = true;
		}

		FPathViews::Append(RunningBuilder, Seg.PathName);

		if (!Seg.DisplayName.IsEmpty() && !Seg.DisplayName.Equals(Seg.PathName, ESearchCase::CaseSensitive))
		{
			DisplayNameMap.Add(FName(RunningBuilder.ToView()), Seg.DisplayName);
		}
	}

	MountRoot = FString(RunningBuilder);

	FName MountRootName(*MountRoot);

	// Always add the mount root as a folder path
	FolderPaths.Add(MountRootName);

	IndexAssetEntries(FlatList);
}

void FContentBrowserExtensionPathIndex::IndexAssetEntries(const TArray<FContentBrowserExtensionAssetEntry>& FlatList)
{
	// (Implementation below is the original asset-walking pass; mount-root setup now
	//  lives in BuildFromFolderSegments above and is shared by both public entry points.)

	// Pre-pass: collect the best-cased full path for every ANCESTOR (intermediate folder)
	// and seed the FName display table before the main loop.
	//
	// Why this is necessary: UE's Content Browser tree widget derives folder labels from
	// VirtualPath.ToString(), which returns the display string stored in the FName table at
	// first-insertion time. Because consumer data may emit lowercase entries first (e.g.
	// "levels/mp/sample/art_exterior/SomeAsset"), the FName table records the lowercase
	// display string for ancestor folder paths. Seeding with the best-cased ancestor path
	// fixes this.
	//
	// IMPORTANT: We deliberately exclude the leaf path (FullPath itself) from this pre-seed.
	// A leaf asset and a same-named folder can coexist at the same case-insensitive path key
	// (e.g. leaf "Art_Exterior" alongside folder "art_exterior" containing children).
	// Including the leaf's own path would cause the leaf's casing to contaminate the folder's
	// FName entry via the shared case-insensitive FName table, displaying the folder with the
	// leaf's casing — and the leaf with whatever casing the FName happened to store.
	// By seeding ONLY ancestor paths, each folder gets the best casing seen across paths that
	// pass through it as an intermediate segment.
	{
		TMap<FString, FString> BestFullPath; // lowercase ancestor-path → best-cased ancestor path

		for (const FContentBrowserExtensionAssetEntry& Entry : FlatList)
		{
			TStringBuilder<256> FullPathBuilder;
			FullPathBuilder << MountRoot;
			FPathViews::Append(FullPathBuilder, Entry.Path);
			FString FullPath(FullPathBuilder);

			// Walk every strict ancestor of FullPath up to (but not into) MountRoot,
			// recording the best-cased form for each ancestor path.
			//
			// CRITICAL INVARIANT — skip the first emission (the leaf path itself):
			// IterateAncestors yields the input path as its first call, then each parent
			// leaf-to-root. We MUST skip the leaf because a leaf asset and a same-named
			// folder can coexist at the same case-insensitive path key (e.g. leaf
			// "Art_Exterior" alongside folder "art_exterior" containing children); seeding
			// the FName table with the leaf's casing would contaminate the folder display
			// (and vice-versa) via the shared case-insensitive FName table. Only paths that
			// appear as INTERMEDIATE ancestors of some asset path contribute casing.
			//
			// Mount-root boundary: stop when an ancestor reaches/falls below MountRoot.Len().
			// (Matches the original `if (CurrentPath.Len() <= MountRoot.Len()) break;`.)
			bool bSkippedLeaf = false;
			FPathViews::IterateAncestors(FullPath, [&](FStringView Ancestor) -> bool
			{
				if (!bSkippedLeaf)
				{
					bSkippedLeaf = true;
					return true; // skip the leaf itself
				}
				if (Ancestor.Len() <= MountRoot.Len())
				{
					return false; // stop at the mount-root boundary
				}

				FString CurrentPath(Ancestor);
				FString Lower = CurrentPath.ToLower();
				if (FString* Existing = BestFullPath.Find(Lower))
				{
					const bool bExistingAllLower = Existing->Equals(Existing->ToLower());
					const bool bNewHasUpper      = !CurrentPath.Equals(CurrentPath.ToLower());
					if (bExistingAllLower && bNewHasUpper)
					{
						*Existing = MoveTemp(CurrentPath);
					}
				}
				else
				{
					BestFullPath.Add(Lower, MoveTemp(CurrentPath));
				}
				return true;
			});
		}

		// Seed the FName display table: constructing FName with the mixed-case string first
		// sets the display-index to that casing. Later FName(lowercase) constructions find the
		// existing comparison entry (case-insensitive) and leave the display string unchanged.
		for (const auto& Pair : BestFullPath)
		{
			FName Seed(*Pair.Value);
			(void)Seed;
		}
	}

	// Track the best-known display name for each internal path segment, keyed by lowercase
	// full path.  The consumer may emit some entries with lowercase names and others with
	// mixed-case names, so the same logical folder may appear as both "levels/mp" and
	// "Levels/MP".  FName merges them case-insensitively but stores the display string from
	// the *first* insertion, which is typically lowercase.  We resolve this by tracking the
	// best-cased segment name for every ancestor path and populating DisplayNameMap so
	// MakeFolderItem can use it.
	// Key   = full internal path lowercased (e.g. "/root/sub/levels/mp")
	// Value = best-cased leaf segment seen so far (e.g. "MP")
	TMap<FString, FString> BestSegmentName; // lowercase full-path → best display segment

	for (const FContentBrowserExtensionAssetEntry& Entry : FlatList)
	{
		// Build full path preserving original case from the asset entry
		TStringBuilder<256> FullPathBuilder;
		FullPathBuilder << MountRoot;
		FPathViews::Append(FullPathBuilder, Entry.Path);
		FString FullPath(FullPathBuilder);
		// F2: build LeafName directly from the builder's FStringView (FName has an FStringView
		// ctor — see UObject/NameTypes.h). Saves the redundant `*FullPath` C-string deref now
		// that the builder is the canonical source of the path content. Uses .ToView() (rather
		// than `FStringView(FullPathBuilder)` direct-init) to avoid the most-vexing-parse where
		// the parens-form of the temporary makes the line parse as a function declaration.
		FName LeafName(FullPathBuilder.ToView());

		// Add the leaf item (last entry wins for duplicates)
		LeafItems.Add(LeafName, Entry);

		// Walk up the ancestor chain via FPathViews::IterateAncestors (leaf-to-root),
		// adding folder entries and tracking best casing. The visitor receives each ancestor
		// in turn and forms edges (Child, Parent) using a one-step lag: `PrevAncestor` holds
		// the previous emission (the child of the current emission).
		//
		// CRITICAL INVARIANT — exclude the leaf's own path from BestSegmentName:
		// A leaf asset's casing (e.g. "Art_Exterior") must not contaminate the display name
		// of a same-named folder at the same case-insensitive path (e.g. "art_exterior"
		// folder containing children). Only paths that appear as an INTERMEDIATE ancestor
		// of some asset contribute casing. We achieve this by skipping BestSegmentName
		// updates for the (FullPath, FullPath.Parent) edge — i.e. the first edge formed by
		// the IterateAncestors walk. ChildrenMap and FolderPaths additions still happen for
		// that edge (they don't go through the case-folding FName display table).
		FString PrevAncestor = FullPath;
		bool bFirst = true; // true on the very first emission (the leaf path itself)
		bool bStop  = false;
		FPathViews::IterateAncestors(FullPath, [&](FStringView Ancestor) -> bool
		{
			if (bFirst)
			{
				// First emission is FullPath itself — no edge yet, just seed PrevAncestor.
				bFirst = false;
				return true;
			}

			// Form edge: PrevAncestor is the child, Ancestor is the parent.
			FString ParentPath(Ancestor);
			FName ParentName(*ParentPath);
			FName ChildName(*PrevAncestor);

			TArray<FName>& Children = ChildrenMap.FindOrAdd(ParentName);
			Children.AddUnique(ChildName);

			if (ParentPath != MountRoot)
			{
				FolderPaths.Add(ParentName);
			}

			// Track BestSegmentName for the CHILD (PrevAncestor) — but only when it is not
			// the leaf path itself. The leaf-edge has PrevAncestor == FullPath.
			if (PrevAncestor != FullPath)
			{
				FString CurrentSegment(FPathViews::GetPathLeaf(PrevAncestor));
				FString CurrentPathLower = PrevAncestor.ToLower();
				FString* ExistingBest = BestSegmentName.Find(CurrentPathLower);
				if (!ExistingBest)
				{
					BestSegmentName.Add(CurrentPathLower, MoveTemp(CurrentSegment));
				}
				else
				{
					const bool bExistingIsLower = ExistingBest->Equals(ExistingBest->ToLower());
					const bool bNewHasUpper     = !CurrentSegment.Equals(CurrentSegment.ToLower());
					if (bExistingIsLower && bNewHasUpper)
					{
						*ExistingBest = MoveTemp(CurrentSegment);
					}
				}
			}

			// Stop after processing the edge whose parent is MountRoot.
			if (ParentPath == MountRoot)
			{
				bStop = true;
				return false;
			}

			PrevAncestor = MoveTemp(ParentPath);
			return true;
		});
		(void)bStop;
	}

	// Populate DisplayNameMap for any asset-path segment where the best-cased name
	// differs from what FName.ToString() would return (the first-inserted, possibly
	// lowercase string).  MakeFolderItem checks DisplayNameMap to get the display name.
	for (const auto& Pair : BestSegmentName)
	{
		const FString& LowerPath     = Pair.Key;
		const FString& BestSegment   = Pair.Value;

		// FName constructed from the original (possibly lowercase) path — this is what
		// FolderPaths / ChildrenMap store.  .ToString() gives back the first-inserted casing.
		FName InternalName(*LowerPath); // FName lookup is case-insensitive; finds existing entry
		FString StoredDisplay = InternalName.ToString();

		// Extract the leaf segment of the stored display string for comparison.
		FString StoredSegment(FPathViews::GetPathLeaf(StoredDisplay));

		if (!StoredSegment.Equals(BestSegment, ESearchCase::CaseSensitive))
		{
			// The FName stores a different (likely lowercase) segment — override via DisplayNameMap.
			DisplayNameMap.FindOrAdd(InternalName) = BestSegment;
		}
	}
}

void FContentBrowserExtensionPathIndex::Reset()
{
	ChildrenMap.Empty();
	LeafItems.Empty();
	FolderPaths.Empty();
	DisplayNameMap.Empty();
	MountRoot.Empty();
}
