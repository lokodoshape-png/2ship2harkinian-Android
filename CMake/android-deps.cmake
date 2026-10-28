# Android has no package manager, so every library the port, LUS and ZAPD look up with find_package is
# built from source here. OVERRIDE_FIND_PACKAGE makes those later find_package calls resolve to these builds.
# The SDL2 tag must match the Java sources in Android/app/src/main/java/org/libsdl.
include(FetchContent)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
# Some of these projects still declare a pre-3.5 minimum, which CMake 4 refuses without this.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

#=================== SDL2 ===================
set(SDL_SHARED ON)
set(SDL_STATIC OFF)
set(SDL_TEST OFF)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.8
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(SDL2)

#=================== nlohmann-json ===================
set(JSON_BuildTests OFF)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(nlohmann_json)

#=================== tinyxml2 ===================
set(tinyxml2_BUILD_TESTING OFF)
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 10.0.0
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(tinyxml2)

#=================== spdlog ===================
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(spdlog)

#=================== libzip ===================
# zlib comes from the NDK; the optional codecs and crypto backends are not needed for .o2r archives.
set(BUILD_TOOLS OFF)
set(BUILD_REGRESS OFF)
set(BUILD_OSSFUZZ OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOC OFF)
set(ENABLE_BZIP2 OFF)
set(ENABLE_LZMA OFF)
set(ENABLE_ZSTD OFF)
set(ENABLE_COMMONCRYPTO OFF)
set(ENABLE_GNUTLS OFF)
set(ENABLE_MBEDTLS OFF)
set(ENABLE_OPENSSL OFF)
set(ENABLE_WINDOWS_CRYPTO OFF)
FetchContent_Declare(
    libzip
    GIT_REPOSITORY https://github.com/nih-at/libzip.git
    GIT_TAG v1.10.1
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(libzip)
if (NOT TARGET libzip::zip)
    add_library(libzip::zip ALIAS zip)
endif()

#=================== libpng (ZAPD) ===================
set(PNG_SHARED OFF)
set(PNG_STATIC ON)
set(PNG_TESTS OFF)
set(PNG_TOOLS OFF)
set(PNG_FRAMEWORK OFF)
FetchContent_Declare(
    PNG
    GIT_REPOSITORY https://github.com/pnggroup/libpng.git
    GIT_TAG v1.6.43
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(PNG)
if (NOT TARGET PNG::PNG)
    add_library(PNG::PNG ALIAS png_static)
endif()
set(PNG_PNG_INCLUDE_DIR ${png_SOURCE_DIR} ${png_BINARY_DIR})
# pnglibconf.h is generated in the build folder, which libpng's own target does not export.
target_include_directories(png_static INTERFACE $<BUILD_INTERFACE:${png_BINARY_DIR}>)

#=================== Ogg / Vorbis / Opus / OpusFile (audio replacement) ===================
set(INSTALL_DOCS OFF)
FetchContent_Declare(
    Ogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG v1.3.5
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(Ogg)

FetchContent_Declare(
    Vorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG v1.3.7
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(Vorbis)
foreach(vorbis_lib vorbis vorbisenc vorbisfile)
    if (NOT TARGET Vorbis::${vorbis_lib})
        add_library(Vorbis::${vorbis_lib} ALIAS ${vorbis_lib})
    endif()
endforeach()

set(OPUS_BUILD_TESTING OFF)
set(OPUS_BUILD_PROGRAMS OFF)
FetchContent_Declare(
    Opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG v1.5.2
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(Opus)

set(OP_DISABLE_HTTP ON)
set(OP_DISABLE_EXAMPLES ON)
set(OP_DISABLE_DOCS ON)
FetchContent_Declare(
    OpusFile
    GIT_REPOSITORY https://github.com/xiph/opusfile.git
    # The v0.12 release predates opusfile's CMake build.
    GIT_TAG 6dfd29e7adb87f2e193575fc3fa88cbf1a0b27df
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(OpusFile)
