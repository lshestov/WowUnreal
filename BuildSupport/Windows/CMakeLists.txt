cmake_minimum_required(VERSION 3.20)
project(IdleWowUnrealDependencies LANGUAGES C CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use the Visual Studio generator with -A x64")
endif()
if(NOT DEFINED WOWUNREAL_SOURCE)
    get_filename_component(WOWUNREAL_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
if(NOT EXISTS "${WOWUNREAL_SOURCE}/WowUnreal.uproject")
    message(FATAL_ERROR "Set WOWUNREAL_SOURCE to the WowUnreal checkout")
endif()
file(TO_CMAKE_PATH "${WOWUNREAL_SOURCE}" WOWUNREAL_SOURCE)
# Match the release CRT used by Unreal Development builds (/MD).
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "" FORCE)
set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(STORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STORM_UNICODE ON CACHE BOOL "" FORCE)
set(THIRD_PARTY "${WOWUNREAL_SOURCE}/Source/ThirdParty")
add_subdirectory("${THIRD_PARTY}/StormLib" stormlib)

file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${THIRD_PARTY}/lua/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac|print)\\.c$")
add_library(lua51 STATIC ${LUA_SOURCES})
target_include_directories(lua51 PUBLIC "${THIRD_PARTY}/lua")
target_compile_definitions(lua51 PRIVATE _CRT_SECURE_NO_WARNINGS)

add_executable(dependency-smoke
    "${CMAKE_CURRENT_LIST_DIR}/smoke.cpp"
    "${WOWUNREAL_SOURCE}/Source/WowData/Private/Mpq/StormLibWrapper.c"
    "${THIRD_PARTY}/pugixml/pugixml.cpp")
target_compile_features(dependency-smoke PRIVATE cxx_std_17)
target_compile_definitions(dependency-smoke PRIVATE UNICODE _UNICODE __STORMLIB_NO_STATIC_LINK__)
target_include_directories(dependency-smoke PRIVATE
    "${THIRD_PARTY}/pugixml" "${WOWUNREAL_SOURCE}/Source/WowData/Private/Mpq")
target_link_libraries(dependency-smoke PRIVATE storm lua51)

install(FILES "$<TARGET_FILE:storm>" "$<TARGET_FILE:lua51>" DESTINATION lib)
