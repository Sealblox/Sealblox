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

file(GLOB_RECURSE SourceFiles "source/*.cpp")

set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "Build bgfx examples." FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "Build bgfx tools." FORCE)

add_subdirectory(external/bgfx.cmake)

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

set(SHADER_FILES 
    "${SHADER_DIR}/"
)

# bgfx_compile_shaders(${SHADER_FILES})

add_executable(Sealblox ${SourceFiles}) # ${SHADER_FILES}

target_include_directories(Sealblox PRIVATE include)

target_link_libraries(Sealblox PRIVATE bgfx bimg bx SDL3::SDL3)