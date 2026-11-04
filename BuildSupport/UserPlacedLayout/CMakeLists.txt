cmake_minimum_required(VERSION 3.20)
project(UserPlacedLayoutChecks LANGUAGES CXX C)
enable_testing()
add_executable(user-placed-layout-tests tests.cpp)
target_compile_features(user-placed-layout-tests PRIVATE cxx_std_17)
target_include_directories(user-placed-layout-tests PRIVATE ../../Source/WowUI/Private)
add_test(NAME user-placed-layout COMMAND user-placed-layout-tests)

# Compile the actual bundled Lua 5.1 parser for the portable fixture. No UE,
# packaged runtime or shared dependency output is touched by this check.
file(GLOB LUA_SOURCES ../../Source/ThirdParty/lua/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac|print)\\.c$")
add_executable(layout-lua-check check-lua.cpp ${LUA_SOURCES})
target_include_directories(layout-lua-check PRIVATE ../../Source/ThirdParty/lua)
add_test(NAME layout-addon-lua51 COMMAND layout-lua-check ${CMAKE_CURRENT_SOURCE_DIR}/../../Scripts/Verification/UserPlacedLayout/IdleWowLayoutProbe/Probe.lua)
if(WIN32)
    add_test(NAME layout-probe-fixtures COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/../../Scripts/Verification/UserPlacedLayout/Verify-Fixtures.ps1 -LuaChecker $<TARGET_FILE:layout-lua-check>)
endif()
