// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserExtensionsStatics.h"
#include "ContentBrowserExtensionsModule.h"


FString UContentBrowserExtensionsStatics::SanitizeForPackagePath(const FString& InPath)
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

			// Case policy:
			//   - Mixed-case words (have BOTH upper and lower alpha): preserved verbatim.
			//     Treated as intentional PascalCase / camelCase by the consumer; do NOT
			//     mangle. Makes the sanitizer idempotent on already-cased identifiers
			//     (e.g. "ThirdParty" -> "ThirdParty", not "Thirdparty").
			//   - Short all-uppercase tokens (<=3 chars, e.g. "II", "IV"): preserved.
			//   - Otherwise (all-upper >3 chars, all-lower, or alpha-less): title-case —
			//     uppercase the first alpha character, lowercase the rest. Non-alpha
			//     characters (digits, _, -, .) ride along unchanged.
			bool bHasUpperAlpha = false;
			bool bHasLowerAlpha = false;
			for (TCHAR Ch : CleanWord)
			{
				if (FChar::IsAlpha(Ch))
				{
					if (FChar::IsUpper(Ch)) { bHasUpperAlpha = true; }
					else                    { bHasLowerAlpha = true; }
				}
			}
			const bool bMixedCase = bHasUpperAlpha && bHasLowerAlpha;
			const bool bAllUpper  = bHasUpperAlpha && !bHasLowerAlpha;

			if (bMixedCase || (bAllUpper && CleanWord.Len() <= 3))
			{
				PascalSeg += CleanWord;
			}
			else
			{
				bool bFirstAlphaSeen = false;
				for (TCHAR Ch : CleanWord)
				{
					if (FChar::IsAlpha(Ch))
					{
						if (!bFirstAlphaSeen)
						{
							PascalSeg.AppendChar(FChar::ToUpper(Ch));
							bFirstAlphaSeen = true;
						}
						else
						{
							PascalSeg.AppendChar(FChar::ToLower(Ch));
						}
					}
					else
					{
						PascalSeg.AppendChar(Ch);
					}
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

FString UContentBrowserExtensionsStatics::SanitizeSegment(const FString& InSegment)
{
	FString Sanitized = UContentBrowserExtensionsStatics::SanitizeForPackagePath(FString(TEXT("/")) + InSegment);
	if (Sanitized.StartsWith(TEXT("/")))
	{
		Sanitized.RemoveAt(0);
	}
	return Sanitized;
}
