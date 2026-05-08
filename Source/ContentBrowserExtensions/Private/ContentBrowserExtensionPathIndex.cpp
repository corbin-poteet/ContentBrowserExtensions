// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionPathIndex.h"
#include "ContentBrowserExtensionsSanitize.h"

// ---------------------------------------------------------------------------
// FContentBrowserExtensionPathIndex
// ---------------------------------------------------------------------------

void FContentBrowserExtensionPathIndex::BuildFromFlatList(const FString& InMountRoot, const TArray<FContentBrowserExtensionAssetEntry>& FlatList)
{
	// Derive (PathName, DisplayName) segments by sanitizing the input and pairing each
	// sanitized segment with its original form, then delegate to the structured overload.
	const FString Sanitized = UE::ContentBrowserExtensions::SanitizeForPackagePath(InMountRoot);

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
		Segments.Emplace(SanitizedSegments[i], OrigSegments[i]);
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
	FString RunningPath;
	for (const FContentBrowserExtensionFolderSegment& Seg : InMountRootSegments)
	{
		if (Seg.PathName.IsEmpty())
		{
			continue;
		}

		RunningPath += TEXT("/");
		RunningPath += Seg.PathName;

		if (!Seg.DisplayName.IsEmpty() && !Seg.DisplayName.Equals(Seg.PathName, ESearchCase::CaseSensitive))
		{
			DisplayNameMap.Add(FName(*RunningPath), Seg.DisplayName);
		}
	}

	MountRoot = MoveTemp(RunningPath);

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
			FString FullPath = MountRoot + TEXT("/") + Entry.Path;

			// Start from the PARENT of FullPath, not FullPath itself.
			// This excludes the leaf asset path from contaminating the folder FName table.
			int32 InitialLastSlash;
			if (!FullPath.FindLastChar(TEXT('/'), InitialLastSlash) || InitialLastSlash == 0)
			{
				continue;
			}
			FString CurrentPath = FullPath.Left(InitialLastSlash);
			if (CurrentPath.Len() <= MountRoot.Len())
			{
				continue;
			}

			while (!CurrentPath.IsEmpty())
			{
				FString Lower = CurrentPath.ToLower();
				if (FString* Existing = BestFullPath.Find(Lower))
				{
					const bool bExistingAllLower = Existing->Equals(Existing->ToLower());
					const bool bNewHasUpper      = !CurrentPath.Equals(CurrentPath.ToLower());
					if (bExistingAllLower && bNewHasUpper)
					{
						*Existing = CurrentPath;
					}
				}
				else
				{
					BestFullPath.Add(Lower, CurrentPath);
				}

				int32 LastSlash;
				if (!CurrentPath.FindLastChar(TEXT('/'), LastSlash) || LastSlash == 0)
				{
					break;
				}
				CurrentPath = CurrentPath.Left(LastSlash);
				if (CurrentPath.Len() <= MountRoot.Len()) { break; }
			}
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
		FString FullPath = MountRoot + TEXT("/") + Entry.Path;
		FName LeafName(*FullPath);

		// Add the leaf item (last entry wins for duplicates)
		LeafItems.Add(LeafName, Entry);

		// Walk up the ancestor chain, adding folder entries and tracking best casing.
		// We start from the PARENT of FullPath — deliberately skipping the leaf's own
		// path segment. This prevents a leaf asset's casing (e.g. "Art_Exterior") from
		// contaminating the display name of a same-named folder at the same case-insensitive
		// path (e.g. "art_exterior" folder containing children).
		// Only paths where a segment appears as an INTERMEDIATE ancestor contribute casing.
		FString CurrentPath = FullPath;
		FString ParentPath;

		// First iteration: establish parent/child relationship for the leaf, but do NOT
		// add the leaf's own segment to BestSegmentName.
		{
			int32 LastSlash;
			if (!CurrentPath.FindLastChar(TEXT('/'), LastSlash))
			{
				continue;
			}

			ParentPath = CurrentPath.Left(LastSlash);
			FName ParentName(*ParentPath);
			FName ChildName(*CurrentPath);

			TArray<FName>& Children = ChildrenMap.FindOrAdd(ParentName);
			Children.AddUnique(ChildName);

			if (ParentPath != MountRoot)
			{
				FolderPaths.Add(ParentName);
			}

			// Do NOT track BestSegmentName for the leaf's own path — see comment above.

			if (ParentPath == MountRoot)
			{
				continue;
			}

			CurrentPath = ParentPath;
		}

		// Subsequent iterations: walk ancestor folders, tracking best casing for each.
		while (true)
		{
			int32 LastSlash;
			if (!CurrentPath.FindLastChar(TEXT('/'), LastSlash))
			{
				break;
			}

			ParentPath = CurrentPath.Left(LastSlash);
			FName ParentName(*ParentPath);
			FName ChildName(*CurrentPath);

			TArray<FName>& Children = ChildrenMap.FindOrAdd(ParentName);
			Children.AddUnique(ChildName);

			if (ParentPath != MountRoot)
			{
				FolderPaths.Add(ParentName);
			}

			// Track the best-cased segment name for this path level.
			// A segment is "better" if it contains uppercase characters while the
			// currently stored best is all-lowercase.
			{
				FString CurrentSegment = CurrentPath.RightChop(LastSlash + 1);
				FString CurrentPathLower = CurrentPath.ToLower();
				FString* ExistingBest = BestSegmentName.Find(CurrentPathLower);
				if (!ExistingBest)
				{
					BestSegmentName.Add(CurrentPathLower, CurrentSegment);
				}
				else
				{
					const bool bExistingIsLower = ExistingBest->Equals(ExistingBest->ToLower());
					const bool bNewHasUpper     = !CurrentSegment.Equals(CurrentSegment.ToLower());
					if (bExistingIsLower && bNewHasUpper)
					{
						*ExistingBest = CurrentSegment;
					}
				}
			}

			if (ParentPath == MountRoot)
			{
				break;
			}

			CurrentPath = ParentPath;
		}
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
		int32 LastSlashInStored;
		FString StoredSegment = StoredDisplay.FindLastChar(TEXT('/'), LastSlashInStored)
			? StoredDisplay.RightChop(LastSlashInStored + 1)
			: StoredDisplay;

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
