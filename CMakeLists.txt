cmake_minimum_required(VERSION 3.24.0)
project(Sealblox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(SDL_TESTS OFF CACHE INTERNAL "")
set(SDL_TEST_LIBRARY OFF CACHE INTERNAL "")

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git"
    GIT_TAG        "release-3.4.18"
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(SDL3)

file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
)

set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)

add_subdirectory(external/bgfx.cmake)

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

set(VERTEX_SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_cube.sc"
)

set(FRAGMENT_SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_cube.sc"
)

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS ${VERTEX_SHADERS}
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/external/bgfx.cmake/bgfx/src"
)

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS ${FRAGMENT_SHADERS}
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/external/bgfx.cmake/bgfx/src"
)

FetchContent_Declare(imgui
   GIT_REPOSITORY https://github.com/ocornut/imgui.git
   GIT_TAG        v1.92.8
   GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

FetchContent_Declare(cgltf
   GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
   GIT_TAG        v1.15
   GIT_SHALLOW    TRUE
   SOURCE_SUBDIR  _header_only_)
FetchContent_Declare(stb
   GIT_REPOSITORY https://github.com/nothings/stb.git
   GIT_TAG        master
   GIT_SHALLOW    TRUE
   SOURCE_SUBDIR  _header_only_)
FetchContent_MakeAvailable(cgltf stb)

if(WIN32 AND NOT MSVC)
    set(BUILD_CURL_EXE      OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS   ON  CACHE BOOL "" FORCE)
    set(BUILD_TESTING       OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS  OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS     OFF CACHE BOOL "" FORCE)
    set(ENABLE_CURL_MANUAL  OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL   ON  CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL    OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL     OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2    OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB           OFF CACHE BOOL "" FORCE)
    set(CURL_BROTLI         OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD           OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2         OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2         OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP   ON  CACHE BOOL "" FORCE)
    FetchContent_Declare(curl
        URL https://curl.se/download/curl-8.11.1.tar.xz)
    FetchContent_MakeAvailable(curl)
else()
    find_package(CURL REQUIRED)
endif()

add_executable(Sealblox
    ${SourceFiles}
    ${FRAGMENT_SHADERS}
    ${VERTEX_SHADERS}
)

target_include_directories(Sealblox PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${cgltf_SOURCE_DIR}"
    "${stb_SOURCE_DIR}"
)

target_link_libraries(Sealblox PRIVATE
    bgfx
    bimg
    bx
    SDL3::SDL3
    imgui
    CURL::libcurl
)