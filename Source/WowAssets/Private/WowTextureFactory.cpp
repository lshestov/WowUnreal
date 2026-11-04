#include "WowTextureFactory.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowTexture, Log, All);

UTexture2D* FWowTextureFactory::CreateTexture(const FBlpTexture& BlpData, const FString& Name, bool bUseAuthoredMips)
{
    if (!BlpData.bIsValid || BlpData.MipLevels.Num() == 0) return nullptr;

    EPixelFormat PF;
    const TCHAR* PFName = TEXT("Unknown");
    switch (BlpData.PixelFormat)
    {
        case EBlpPixelFormat::DXT1: PF = PF_DXT1; PFName = TEXT("DXT1"); break;
        case EBlpPixelFormat::DXT3: PF = PF_DXT3; PFName = TEXT("DXT3"); break;
        case EBlpPixelFormat::DXT5: PF = PF_DXT5; PFName = TEXT("DXT5"); break;
        case EBlpPixelFormat::RGBA8: PF = PF_R8G8B8A8; PFName = TEXT("RGBA8"); break;
        default:
            UE_LOG(LogWowTexture, Warning, TEXT("Unsupported BLP pixel format for '%s'"), *Name);
            return nullptr;
    }

    const auto& Mip0 = BlpData.MipLevels[0];
    const uint64 BaseBytes = GPixelFormats[PF].Get2DImageSizeInBytes(BlpData.Width, BlpData.Height);
    if (!BaseBytes || BaseBytes > uint64(Mip0.Data.Num())) return nullptr;
    UTexture2D* Tex = UTexture2D::CreateTransient(BlpData.Width, BlpData.Height, PF, *Name,
        TConstArrayView64<uint8>(Mip0.Data.GetData(), int64(BaseBytes)));
    if (!Tex)
    {
        UE_LOG(LogWowTexture, Error, TEXT("CreateTransient failed for '%s' (%dx%d %s)"),
            *Name, BlpData.Width, BlpData.Height, PFName);
        return nullptr;
    }

    Tex->SRGB = true;
    Tex->NeverStream = true;
    auto& Mips = Tex->GetPlatformData()->Mips;
    for (int32 Level = 1; bUseAuthoredMips && Level < BlpData.MipLevels.Num(); ++Level)
    {
        const auto& Prior = BlpData.MipLevels[Level-1];
        const auto& Source = BlpData.MipLevels[Level];
        if ((Prior.Width == 1 && Prior.Height == 1) || Source.Width != FMath::Max(1u, Prior.Width / 2)
            || Source.Height != FMath::Max(1u, Prior.Height / 2)) break;
        const uint64 Bytes = GPixelFormats[PF].Get2DImageSizeInBytes(Source.Width, Source.Height);
        if (!Bytes || Bytes > uint64(Source.Data.Num())) break;
        auto* Mip = new FTexture2DMipMap(Source.Width, Source.Height, 1);
        Mips.Add(Mip);
        Mip->BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Mip->BulkData.Realloc(Bytes), Source.Data.GetData(), Bytes);
        Mip->BulkData.Unlock();
    }
    Tex->Filter = Mips.Num() > 1 ? TF_Trilinear : TF_Bilinear;
    Tex->UpdateResource();

    UE_LOG(LogWowTexture, Verbose, TEXT("Created texture '%s': %dx%d %s (%d bytes)"),
        *Name, BlpData.Width, BlpData.Height, PFName, Mip0.Data.Num());

    return Tex;
}
