// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionsSanitize.h"
#include "ContentBrowserExtensionsModule.h"

// ---------------------------------------------------------------------------
// SanitizeForPackagePath
// ---------------------------------------------------------------------------

FString UE::ContentBrowserExtensions::SanitizeForPackagePath(const FString& InPath)
{
	if (InPath.IsEmpty())
	{
		return InPath;
	}

	// Split into segments by '/', keeping empty entries so leading slash is preserved.
	TArray<FString> Segments;
	InPath.ParseIntoArray(Segments, TEXT("/"), /*bCullEmpty=*/false);

	for (FString& Seg : Segments)
	{
		if (Seg.IsEmpty())
		{
			continue; // leading slash produces empty segment
		}

		// Split segment into words by spaces.
		TArray<FString> Words;
		Seg.ParseIntoArray(Words, TEXT(" "), /*bCullEmpty=*/true);

		FString PascalSeg;
		PascalSeg.Reserve(Seg.Len());

		for (const FString& Word : Words)
		{
			// Strip non-valid characters from the word first.
			FString CleanWord;
			CleanWord.Reserve(Word.Len());
			for (TCHAR Ch : Word)
			{
				if (FChar::IsAlpha(Ch) || FChar::IsDigit(Ch))
				{
					CleanWord.AppendChar(Ch);
				}
				else if (Ch == TEXT('_') || Ch == TEXT('-') || Ch == TEXT('.'))
				{
					CleanWord.AppendChar(Ch);
				}
				// Strip TM (U+2122), registered (U+00AE), copyright (U+00A9), parentheses, and all other chars
			}

			if (CleanWord.IsEmpty())
			{
				continue;
			}

			// PascalCase: capitalize first letter, lowercase the rest.
			// Exception: short all-uppercase words (<=3 chars, e.g. "II", "IV") stay as-is.
			bool bAllUpper = true;
			for (TCHAR Ch : CleanWord)
			{
				if (FChar::IsAlpha(Ch) && !FChar::IsUpper(Ch))
				{
					bAllUpper = false;
					break;
				}
			}

			if (bAllUpper && CleanWord.Len() <= 3)
			{
				PascalSeg += CleanWord;
			}
			else
			{
				PascalSeg.AppendChar(FChar::ToUpper(CleanWord[0]));
				for (int32 i = 1; i < CleanWord.Len(); ++i)
				{
					PascalSeg.AppendChar(FChar::ToLower(CleanWord[i]));
				}
			}
		}

		Seg = MoveTemp(PascalSeg);
	}

	// Drop empty sanitized segments to avoid emitting collapsed-slash paths like "/Foo//Bar"
	// (which can happen when an entire segment is composed of stripped characters, e.g.
	// "/Foo/(R)/Bar" or "/Foo/ /Bar"). Preserve the leading empty segment so the leading
	// slash is retained in the joined output.
	TArray<FString> NonEmpty;
	NonEmpty.Reserve(Segments.Num());
	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		if (i == 0 && Segments[i].IsEmpty())
		{
			NonEmpty.Add(Segments[i]); // preserve leading slash
			continue;
		}
		if (Segments[i].IsEmpty())
		{
			UE_LOG(LogContentBrowserExtensions, Warning,
				TEXT("SanitizeForPackagePath: segment %d of '%s' sanitized to empty; dropping to avoid collapsed slashes"),
				i, *InPath);
			continue;
		}
		NonEmpty.Add(Segments[i]);
	}

	return FString::Join(NonEmpty, TEXT("/"));
}
