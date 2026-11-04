extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include <cstdio>
int main(int argc,char** argv)
{
    lua_State* L=luaL_newstate();
    if(!L || argc!=2) return 2;
    const int Status=luaL_loadfile(L,argv[1]);
    if(Status) std::fprintf(stderr,"%s\n",lua_tostring(L,-1));
    else std::puts("PASS: layout probe parses in bundled Lua 5.1.5");
    lua_close(L); return Status?1:0;
}
