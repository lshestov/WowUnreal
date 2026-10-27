#include "Formats/WdtParser.h"
#include "Formats/ChunkId.h"
DEFINE_LOG_CATEGORY_STATIC(LogWdt, Log, All);

// These match what's ON DISK (reversed)
static constexpr uint32 WDT_MVER = MakeFourCC('R', 'E', 'V', 'M');
static constexpr uint32 WDT_MPHD = MakeFourCC('D', 'H', 'P', 'M');
static constexpr uint32 WDT_MAIN = MakeFourCC('N', 'I', 'A', 'M');
static constexpr uint32 WDT_MWMO = MakeFourCC('O', 'M', 'W', 'M');
static constexpr uint32 WDT_MODF = MakeFourCC('F', 'D', 'O', 'M');

FWdtData FWdtParser::Parse(const TArray<uint8>& Data)
{
    FWdtData Result;
    if (Data.Num() < 8) return Result;

    const uint8* Ptr = Data.GetData();
    const uint8* End = Ptr + Data.Num();

    bool HasMain=false;
    while (End-Ptr >= 8)
    {
        uint32 Magic = *reinterpret_cast<const uint32*>(Ptr);
        uint32 Size  = *reinterpret_cast<const uint32*>(Ptr + 4);
        const uint8* ChunkData = Ptr + 8;

        if (Size>uint64(End-ChunkData)) return FWdtData();

        if (Magic == WDT_MPHD)
        {
            if (Size >= 4)
            {
                Result.Flags = *reinterpret_cast<const uint32*>(ChunkData);
                Result.bUseBigAlpha = (Result.Flags & 0x04) != 0;
            }
        }
        else if (Magic == WDT_MAIN)
        {
            if (Size >= 64 * 64 * 8)
            {
                HasMain=true;
                int32 TileCount = 0;
                for (int32 y = 0; y < 64; y++)
                {
                    for (int32 x = 0; x < 64; x++)
                    {
                        uint32 Flags = *reinterpret_cast<const uint32*>(ChunkData + (y * 64 + x) * 8);
                        Result.TileExists[x][y] = (Flags & 0x01) != 0;
                        if (Flags & 0x01) TileCount++;
                    }
                }
                UE_LOG(LogWdt, Log, TEXT("MAIN chunk: found %d existing tiles"), TileCount);
            }
        }
        else if (Magic == WDT_MWMO)
        {
            if (Size > 0)
            {
                bool Terminated=false; for(uint32 I=0;I<Size;++I) if(!ChunkData[I]) {Terminated=true; break;} if(!Terminated) return FWdtData();
                Result.GlobalWmoPath = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(ChunkData)));
            }
        }
        else if(Magic==WDT_MODF)
        {
            if(Size!=64 || Result.bHasGlobalWmoPlacement) return FWdtData();
            uint32 Header[2]; float Values[12]; uint16 Fields[4];
            FMemory::Memcpy(Header,ChunkData,8); FMemory::Memcpy(Values,ChunkData+8,48); FMemory::Memcpy(Fields,ChunkData+56,8);
            for(float V:Values) if(!FMath::IsFinite(V)) return FWdtData();
            auto& P=Result.GlobalWmoPlacement; P.NameIndex=int32(Header[0]); P.UniqueId=Header[1];
            P.Position=FVector(Values[0],Values[1],Values[2]); P.Rotation=FVector(Values[3],Values[4],Values[5]); P.BoundingBox=FBox(FVector(Values[6],Values[7],Values[8]),FVector(Values[9],Values[10],Values[11]));
            P.Flags=Fields[0]; P.DoodadSet=Fields[1]; P.NameSet=Fields[2]; P.Scale=Fields[3]; Result.bHasGlobalWmoPlacement=true;
        }

        Ptr = ChunkData + Size;
    }

    Result.bIsValid = Ptr==End && ((Result.Flags&1)?Result.bHasGlobalWmoPlacement && !Result.GlobalWmoPath.IsEmpty() && Result.GlobalWmoPlacement.NameIndex==0:HasMain);
    UE_LOG(LogWdt, Log, TEXT("WDT parsed: flags=0x%X, bigAlpha=%d, globalWmo='%s'"),
        Result.Flags, Result.bUseBigAlpha ? 1 : 0, *Result.GlobalWmoPath);
    return Result;
}
