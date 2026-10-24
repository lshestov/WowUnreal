#include "StormLibWrapper.h"
#include "pugixml.hpp"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <cstring>
#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    lua_State* L = luaL_newstate();
    if (!L) return 3;
    luaL_openlibs(L);
    const bool LuaOk = luaL_dostring(L, "assert(_VERSION == 'Lua 5.1'); return 6 * 7") == 0
        && lua_tointeger(L, -1) == 42;
    lua_close(L);
    if (!LuaOk) return 4;
    std::cout << "PASS: Lua 5.1 execution\n";

    pugi::xml_document Document;
    if (!Document.load_string("<Ui><Frame name='IdleWow'/></Ui>")
        || std::strcmp(Document.child("Ui").child("Frame").attribute("name").value(), "IdleWow")) return 5;
    std::cout << "PASS: pugixml FrameXML parsing\n";

    STORMLIB_HANDLE Archive = nullptr, File = nullptr;
    if (!StormLib_OpenArchive(argv[1], &Archive)) return 6;
    if (!StormLib_OpenFile(Archive, "DBFilesClient\\ChrRaces.dbc", &File))
    {
        StormLib_CloseArchive(Archive);
        return 7;
    }
    const auto Size = StormLib_GetFileSize(File);
    if (Size < 20 || Size > 16 * 1024 * 1024)
    {
        StormLib_CloseFile(File);
        StormLib_CloseArchive(Archive);
        return 8;
    }
    std::vector<char> Data(Size);
    STORMLIB_UINT32 Read = 0;
    const bool ReadOk = StormLib_ReadFile(File, Data.data(), Size, &Read);
    StormLib_CloseFile(File);
    StormLib_CloseArchive(Archive);
    if (!ReadOk || Read != Size || std::memcmp(Data.data(), "WDBC", 4)) return 9;
    std::cout << "PASS: Windows StormLib wrapper reads ChrRaces.dbc (" << Size << " bytes) from existing MPQ\n";
    return 0;
}
