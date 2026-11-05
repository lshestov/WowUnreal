#pragma once
#include "CoreMinimal.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"

class FMpqManager;

/**
 * Manages WoW font loading from MPQ archives.
 * Extracts TTF files from MPQ to a temp directory and creates FSlateFontInfo objects.
 * Maps WoW font object names (like "GameFontNormal") to appropriate TTF files.
 */
class WOWUI_API FWowFontManager
{
public:
	FWowFontManager();
	~FWowFontManager();

	/** Initialize the font manager and extract fonts from MPQ */
	bool Initialize(FMpqManager* Mpq);

	/** Shutdown and cleanup */
	void Shutdown();

	/** Get a font info for a WoW font name and size */
	FSlateFontInfo GetFont(const FString& WowFontName, float Size = 12, EFontHinting Hinting = EFontHinting::Default) const;

	/** Get the default UI font */
	FSlateFontInfo GetDefaultFont(float Size = 12, EFontHinting Hinting = EFontHinting::Default) const;

	/** Register a WoW font name mapping from FrameXML */
	void RegisterFontMapping(const FString& WowFontName, const FString& InheritsFrom, int32 DefaultSize, const FString& FontFile = FString());
	int32 GetFontSize(const FString& Name) const { const auto* Size=DefaultSizes.Find(Name); return Size?*Size:Name.Contains(TEXT("Small"))?10:12; }

	/** Check if fonts are available */
	bool IsInitialized() const { return bInitialized; }

private:
	/** Extract a TTF font file from MPQ to the temp directory */
	bool ExtractFont(FMpqManager* Mpq, const FString& MpqPath, const FString& LocalPath);

	/** Create a font info from a local TTF file path */
	FSlateFontInfo CreateFontFromFile(const FString& FilePath, float Size, EFontHinting Hinting) const;

	/** Set up the default WoW font mappings */
	void SetupFontMappings();

	// Font mappings from WoW font names to TTF file names
	TMap<FString, FString> FontMappings;
	TMap<FString,int32> DefaultSizes;

	// Directory where fonts are extracted
	FString FontsDirectory;

	// Available font paths
	TMap<FString, FString> AvailableFonts;

	bool bInitialized = false;
};
