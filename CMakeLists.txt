# 3.24+: the ENet and OpenAL downloads below use FetchContent's DOWNLOAD_EXTRACT_TIMESTAMP (new
# in 3.24), and FindLua knows Lua 5.4. Visual Studio 2022 and current vcpkg ship a newer CMake.
cmake_minimum_required(VERSION 3.24)
project(BlockSmith C CXX)   # the BlockSmith engine

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# main.cpp has thousands of sol2 bindings; MSVC needs /bigobj for that many sections
if (MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
endif()

# Server-only builds (e.g. a headless Linux box): only BlockSmithServer, without the game's
# graphics, window, sound and JSON libraries. Off by default: both targets, as before.
#   cmake -B build -DBLOCKSMITH_SERVER_ONLY=ON
#   cmake --build build --config Release --target BlockSmithServer
option(BLOCKSMITH_SERVER_ONLY "Build only the dedicated server (no OpenGL/glfw3/OpenAL needed)" OFF)
if (BLOCKSMITH_SERVER_ONLY)
    message(STATUS "BlockSmith: server-only build (BlockSmithServer only; the game is not built)")
endif()

# Find vcpkg packages (the game alone needs OpenGL, glfw3 and nlohmann_json)
if (NOT BLOCKSMITH_SERVER_ONLY)
    find_package(OpenGL REQUIRED)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()
find_package(glm CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)

# vcpkg ships Lua as unofficial-lua, other platforms use CMake's FindLua
find_package(unofficial-lua CONFIG QUIET)
if (TARGET unofficial::lua::lua)
    set(VOXEL_LUA_TARGET unofficial::lua::lua)
else()
    find_package(Lua 5.4 REQUIRED)
    add_library(voxel_lua IMPORTED INTERFACE)
    target_include_directories(voxel_lua INTERFACE ${LUA_INCLUDE_DIR})
    target_link_libraries(voxel_lua INTERFACE ${LUA_LIBRARIES})
    set(VOXEL_LUA_TARGET voxel_lua)
endif()

if (NOT unofficial-lua_FOUND AND NOT Lua_FOUND)
    message(FATAL_ERROR "Could not find unofficial-lua (VCPKG) or Lua library")
endif()

# ENet (multiplayer networking) - downloaded and built automatically the first time
# CMake configures. ENet's own CMakeLists is old, so tell newer CMake versions to accept it.
include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(enet
    URL https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(enet)

# ---------------------------------------------------------------------------
# BlockSmith: the game (skipped in server-only builds)
# ---------------------------------------------------------------------------
if (NOT BLOCKSMITH_SERVER_ONLY)

# OpenAL Soft (sound), the first of:
#   1. vcpkg's openal-soft (Windows)
#   2. the system's OpenAL (Linux / BSD package, e.g. libopenal-dev, openal-soft, openal)
#   3. downloaded and built the first time CMake configures, like ENet
# -DBLOCKSMITH_BUNDLED_OPENAL=ON skips 2 and always builds it.
option(BLOCKSMITH_BUNDLED_OPENAL "Always download and build OpenAL Soft, even if the system has OpenAL" OFF)
find_package(OpenAL CONFIG QUIET)
if (NOT TARGET OpenAL::OpenAL AND NOT BLOCKSMITH_BUNDLED_OPENAL AND NOT WIN32 AND NOT APPLE)
    # (not on macOS: its OpenAL.framework has <OpenAL/al.h>, the game includes <AL/al.h>)
    find_package(OpenAL MODULE QUIET)
    if (OPENAL_FOUND)
        if (NOT TARGET OpenAL::OpenAL)        # (CMake older than 3.25 makes no target)
            add_library(OpenAL::OpenAL INTERFACE IMPORTED)
            set_target_properties(OpenAL::OpenAL PROPERTIES
                INTERFACE_LINK_LIBRARIES "${OPENAL_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${OPENAL_INCLUDE_DIR}")
        endif()
        # OPENAL_INCLUDE_DIR is usually .../include/AL; the game includes <AL/al.h>, so the
        # folder above it is needed too (/usr/local/include on the BSDs isn't searched by default)
        get_filename_component(_openal_parent "${OPENAL_INCLUDE_DIR}" DIRECTORY)
        set_property(TARGET OpenAL::OpenAL APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_openal_parent}")
        message(STATUS "Sound: the system's OpenAL (${OPENAL_LIBRARY})")
    endif()
endif()
if (TARGET OpenAL::OpenAL)
    if (NOT OPENAL_FOUND)
        message(STATUS "Sound: OpenAL from vcpkg")
    endif()
else()
    message(STATUS "Sound: downloading OpenAL Soft")
    # No built-in HRTF data: the game doesn't use HRTF, and its rule depends on a file with a
    # space in its name ("Default HRTF.mhr") that BSD make can't handle
    set(ALSOFT_EMBED_HRTF_DATA OFF CACHE BOOL "" FORCE)
    set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
    set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
    set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
    set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
    set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
    set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(openal
        URL https://github.com/kcat/openal-soft/archive/refs/tags/1.24.3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(openal)
endif()

add_library(stb IMPORTED INTERFACE)
add_library(unofficial::stb ALIAS stb)

target_include_directories(stb INTERFACE "${CMAKE_CURRENT_LIST_DIR}/libs")

add_executable(BlockSmith
    main.cpp
    Player.cpp
    World.cpp
    ChunkMesh.cpp
    Block.cpp
    Hotbar.cpp
    PauseMenu.cpp
    FontRenderer.cpp
    ParticleSystem.cpp
    Lighting.cpp
    Lighting.h
    Skybox.cpp
    Skybox.h
    CaveGenerator.cpp
    CaveGenerator.h
    Model.cpp
    Model.h
    ChatSystem.cpp
    WebLink.cpp
    WebLink.h
    ChatSystem.h
    MainMenu.cpp
    MainMenu.h
    Clouds.cpp
    Clouds.h
    Fog.h
    Fog.cpp
    Inventory.cpp
    Inventory.h
    WorldRenderer.cpp
    WorldRenderer.h
    ChunkData.cpp
    ChunkData.h
    Light.cpp
    Light.h
    FirstPerson.cpp
    FirstPerson.h
    ClientMods.cpp
    ClientMods.h
    ClientModRules.h
    ChunkGenerator.cpp
    ChunkGenerator.h
    Fluids.cpp
    Fluids.h
    WeatherEffects.cpp
    WeatherEffects.h
    Firewall.cpp
    Audio.cpp
    Audio.h
    StbVorbis.cpp
    Net.cpp
    Entities.cpp
    BlockModifiers.cpp
    BlockModifiers.h
    Entities.h
    EntityLua.h
    Net.h
    RemoteContent.cpp
    RemoteContent.h
    PlayerMovement.cpp
    PlayerMovement.h
    BlockDefs.cpp
    BlockDefs.h
    PlayerAvatars.cpp
    PlayerAvatars.h
    deps/glad.c
)

target_include_directories(BlockSmith PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps
    ${enet_SOURCE_DIR}/include
)

target_link_libraries(BlockSmith PRIVATE
    glfw
    glm::glm
    sol2::sol2
    nlohmann_json::nlohmann_json
    OpenGL::GL
    ${CMAKE_DL_LIBS}
    unofficial::stb
    ${VOXEL_LUA_TARGET}
    enet
    OpenAL::OpenAL
)

# The BlockSmith icon on the .exe files (icons/blocksmith.ico, via BlockSmith.rc)
if (WIN32)
    target_sources(BlockSmith PRIVATE BlockSmith.rc)
endif()

# OpenAL Soft is a DLL on Windows: put it next to BlockSmith.exe so the game finds it
if (WIN32 AND TARGET OpenAL)
    add_custom_command(TARGET BlockSmith POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:BlockSmith>)
endif()

# ENet needs the Windows socket and timer libraries
if (WIN32)
    target_link_libraries(BlockSmith PRIVATE ws2_32 winmm shell32)
endif()

if (TARGET unofficial::lua::lua)
    target_link_libraries(BlockSmith PRIVATE unofficial::lua::lua)
else()
    target_link_libraries(BlockSmith PRIVATE ${LUA_LIBRARY})
endif()

endif()   # NOT BLOCKSMITH_SERVER_ONLY (the game)

# ---------------------------------------------------------------------------
# BlockSmithServer: the dedicated (headless) server. No window, no graphics -
# just the world, the mods and the networking.
# Build it with:  cmake --build build --config Release --target BlockSmithServer
# ---------------------------------------------------------------------------
add_executable(BlockSmithServer
    ServerMain.cpp
    ChunkGenerator.cpp
    Fluids.cpp
    World.cpp
    ChunkData.cpp
    Light.cpp
    CaveGenerator.cpp
    Block.cpp
    BlockDefs.cpp
    Net.cpp
    Entities.cpp
    BlockModifiers.cpp
    BlockModifiers.h
    Entities.h
    EntityLua.h
    RemoteContent.cpp
    PlayerMovement.cpp
)

if (WIN32)
    target_sources(BlockSmithServer PRIVATE BlockSmith.rc)   # same icon for the server .exe
endif()

target_include_directories(BlockSmithServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps
    ${enet_SOURCE_DIR}/include
)

target_link_libraries(BlockSmithServer PRIVATE
    glm::glm
    sol2::sol2
    ${VOXEL_LUA_TARGET}
    enet
)

if (WIN32)
    target_link_libraries(BlockSmithServer PRIVATE ws2_32 winmm)
endif()

if (NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(BlockSmithServer PRIVATE Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Online mod content (RemoteContent.cpp): HTTPS downloads of DATA (JSON, text, pictures, sounds)
# from GitHub and approved sites. Windows: WinHTTP (built in). Elsewhere: libcurl if installed;
# without it the game still builds and remote.* says downloads aren't available.
# ---------------------------------------------------------------------------
if (WIN32)
    if (TARGET BlockSmith)
        target_link_libraries(BlockSmith PRIVATE winhttp)
    endif()
    target_link_libraries(BlockSmithServer PRIVATE winhttp)
else()
    find_package(CURL QUIET)
    if (CURL_FOUND)
        message(STATUS "BlockSmith: online mod content uses libcurl")
        foreach(target BlockSmith BlockSmithServer)
            if (TARGET ${target})
                target_link_libraries(${target} PRIVATE CURL::libcurl)
                target_compile_definitions(${target} PRIVATE BLOCKSMITH_HAS_CURL=1)
            endif()
        endforeach()
    else()
        message(STATUS "BlockSmith: libcurl not found - online mod content (remote.*) is disabled in this build")
    endif()
endif()
