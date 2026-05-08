// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionPathIndex.h"
#include "ContentBrowserExtensionsStatics.h"

#include "Misc/Char.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"

// ---------------------------------------------------------------------------
// File-local helpers (TA2 / 260508-ih3)
// ---------------------------------------------------------------------------
//
// Replace the four `*.ToLower().Equals(...)` casing predicates that the prior
// shipped state used inside the IndexAssetEntries best-casing-wins logic with
// 1-pass FStringView scans. Same truth table for any string with at least one
// alpha char; for all-digit / all-symbol strings both predicates return
// (true, false) which preserves the no-upgrade behavior of the original
// (`ToLower().Equals(s)` returns true for non-alpha-only strings — so the
// original `bExistingAllLower && bNewHasUpper` becomes (true && false) = false,
// matching `IsAllLowerAlpha(true) && HasUpperAlpha(false)`).
//
// Behavior preserved exactly because:
//   - FString::ToLower only changes alpha characters; non-alpha (digits,
//     slashes, underscore, hyphen, dot) compare equal in both versions.
//   - The shape of the casing-predicate ("existing has no uppercase alpha AND
//     incoming has at least one uppercase alpha") is what these helpers
//     encode — and that's the only real-world casing-upgrade trigger.
//
// See 260508-ih3-RESEARCH.md § "Pitfalls → Verifying IsAllLowerAlpha
// correctness" for the full derivation.
namespace
{
	bool IsAllLowerAlpha(FStringView S)
	{
		for (TCHAR C : S)
		{
			if (FChar::IsAlpha(C) && FChar::IsUpper(C))
			{
				return false;
			}
		}
		return true;
	}

	bool HasUpperAlpha(FStringView S)
	{
		for (TCHAR C : S)
		{
			if (FChar::IsAlpha(C) && FChar::IsUpper(C))
			{
				return true;
			}
		}
		return false;
	}
}

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
		// FName is case-insensitive natively — same dedup grouping as a lowercased-FString
		// key, but without the per-iteration `ToLower()` allocation. Iteration order over
		// this map is FName-hash-bucket (not deterministic-lowercase-FString) but that
		// doesn't affect the final FName-display-table priming because each Pair.Value
		// is independently constructed as an FName below; order is irrelevant for the
		// "best casing wins" semantic. (260508-ih3 / TA2: lifts Don't-touch #7.)
		TMap<FName, FString> BestFullPath; // case-insensitive ancestor-path → best-cased ancestor path

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

				// TA2 / 260508-ih3 (F5+F14): replace lowercase-key + ToLower-equality
				// predicates with case-insensitive FName key + IsAllLowerAlpha /
				// HasUpperAlpha 1-pass scans. Same behavior, ~3 fewer heap allocations
				// per ancestor visit on the hot loop.
				FString CurrentPath(Ancestor);
				const FName Key(*CurrentPath);
				if (FString* Existing = BestFullPath.Find(Key))
				{
					if (IsAllLowerAlpha(*Existing) && HasUpperAlpha(CurrentPath))
					{
						*Existing = MoveTemp(CurrentPath);
					}
				}
				else
				{
					BestFullPath.Add(Key, MoveTemp(CurrentPath));
				}
				return true;
			});
		}

		// Intentional FName-display-table priming: constructing FName from the
		// mixed-case Pair.Value ensures the global FName display index records
		// the best-cased form for downstream lookups. The (void)Seed cast marks
		// the discard as deliberate (not a forgotten variable).
		//
		// FName display-index semantics: the FIRST FName constructed for a given
		// case-insensitive comparison key sets the display string. Later
		// FName(lowercase) constructions find the existing entry and leave the
		// display string unchanged.
		for (const auto& Pair : BestFullPath)
		{
			FName Seed(*Pair.Value);
			(void)Seed;
		}
	}

	// Track the best-known display name for each internal path segment, keyed by FName
	// (case-insensitive natively).  The consumer may emit some entries with lowercase
	// names and others with mixed-case names, so the same logical folder may appear as
	// both "levels/mp" and "Levels/MP".  FName merges them case-insensitively but stores
	// the display string from the *first* insertion, which is typically lowercase.  We
	// resolve this by tracking the best-cased segment name for every ancestor path and
	// populating DisplayNameMap so MakeFolderItem can use it.
	// Key   = full internal path (FName, case-insensitive — e.g. FName("/root/sub/levels/mp"))
	// Value = best-cased leaf segment seen so far (e.g. "MP")
	// (TA2 / 260508-ih3: lifts Don't-touch #7 — was TMap<FString,FString> with `ToLower()`
	// keys and `*.ToLower().Equals(...)` casing predicates; now FName-keyed with
	// IsAllLowerAlpha / HasUpperAlpha 1-pass scans.)
	TMap<FName, FString> BestSegmentName; // case-insensitive full-path → best display segment

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
				// TA2 / 260508-ih3 (F5+F14): FName-keyed lookup + IsAllLowerAlpha /
				// HasUpperAlpha 1-pass scans replace the lowercased-FString key and
				// `*.ToLower().Equals(...)` predicates. Same behavior; ~3 fewer
				// allocations per ancestor visit on the hot loop.
				FString CurrentSegment(FPathViews::GetPathLeaf(PrevAncestor));
				const FName CurrentPathKey(*PrevAncestor);
				FString* ExistingBest = BestSegmentName.Find(CurrentPathKey);
				if (!ExistingBest)
				{
					BestSegmentName.Add(CurrentPathKey, MoveTemp(CurrentSegment));
				}
				else
				{
					if (IsAllLowerAlpha(*ExistingBest) && HasUpperAlpha(CurrentSegment))
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
		// Pair.Key is the FName used by FolderPaths / ChildrenMap (case-insensitive).
		// .ToString() gives back the first-inserted casing — which may be lowercase if
		// the consumer emitted lowercase entries first. (TA2 / 260508-ih3: this used to
		// reconstruct `FName(*Pair.Key)` from a lowercased-FString key — now it's
		// already FName, so the indirection is gone.)
		const FName& InternalName = Pair.Key;
		const FString& BestSegment = Pair.Value;

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
