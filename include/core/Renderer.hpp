#pragma once

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <cstdint>
#include <vector>
#include <string>

struct CubeVertex
{
    float x, y, z;
    uint32_t color;
};

struct CubeShaderResources
{
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle baseColorSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle baseColorFactor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle materialParams = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteTexture = BGFX_INVALID_HANDLE;
};

class Window;

class Renderer
{
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    bool init(const Window &window);
    void shutdown();

    void resize(uint32_t width, uint32_t height);

    void beginFrame();
    void initCube();
    void drawCube(const float *transform);
    void endFrame();

    bgfx::ProgramHandle loadProgram(const char *vs, const char *fs) const;

private:
    static const char *shaderDir();
    void destroyCubeResources();
    bgfx::ShaderHandle loadShader(const char *name) const;

    std::string m_shaderRoot;

    bgfx::VertexBufferHandle m_cubeVbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_cubeIbh = BGFX_INVALID_HANDLE;
    CubeShaderResources m_cubeShader;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_resetFlags = BGFX_RESET_VSYNC;
    bool m_initialized = false;
};