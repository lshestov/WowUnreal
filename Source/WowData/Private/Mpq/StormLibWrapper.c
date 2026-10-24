// C ABI boundary: Unreal passes UTF-8 paths; Windows StormLib uses UTF-16.
#include <stdbool.h>
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundef"
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "StormLib.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <stdlib.h>

typedef void* STORMLIB_HANDLE;
typedef unsigned int STORMLIB_UINT32;

bool StormLib_OpenArchive(const char* Path, STORMLIB_HANDLE* OutHandle)
{
    HANDLE hMpq = NULL;
    bool Result;
#ifdef _WIN32
    int Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path, -1, NULL, 0);
    wchar_t* WidePath;
    *OutHandle = NULL;
    if (Length <= 0) return false;
    WidePath = (wchar_t*)malloc((size_t)Length * sizeof(wchar_t));
    if (!WidePath) return false;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Path, -1, WidePath, Length))
    {
        free(WidePath);
        return false;
    }
    Result = SFileOpenArchive(WidePath, 0, MPQ_OPEN_READ_ONLY, &hMpq);
    free(WidePath);
#else
    Result = SFileOpenArchive(Path, 0, MPQ_OPEN_READ_ONLY, &hMpq);
#endif
    *OutHandle = hMpq;
    return Result;
}

void StormLib_CloseArchive(STORMLIB_HANDLE Handle) { SFileCloseArchive((HANDLE)Handle); }
bool StormLib_OpenFile(STORMLIB_HANDLE Archive, const char* FileName, STORMLIB_HANDLE* OutFile)
{
    HANDLE hFile = NULL;
    bool Result = SFileOpenFileEx((HANDLE)Archive, FileName, 0, &hFile);
    *OutFile = hFile;
    return Result;
}
void StormLib_CloseFile(STORMLIB_HANDLE File) { SFileCloseFile((HANDLE)File); }
STORMLIB_UINT32 StormLib_GetFileSize(STORMLIB_HANDLE File) { return SFileGetFileSize((HANDLE)File, NULL); }
bool StormLib_ReadFile(STORMLIB_HANDLE File, void* Buffer, STORMLIB_UINT32 Size, STORMLIB_UINT32* BytesRead)
{
    DWORD Read = 0;
    bool Result = SFileReadFile((HANDLE)File, Buffer, Size, &Read, NULL);
    if (BytesRead) *BytesRead = (STORMLIB_UINT32)Read;
    return Result;
}
