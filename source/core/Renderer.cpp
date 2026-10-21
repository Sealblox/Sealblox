#include <core/Renderer.hpp>
#include <core/Window.hpp>

#include <bx/math.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

template <typename T>
void destroyHandle(T &handle)
{
    if (bgfx::isValid(handle))
        bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
}

Renderer::~Renderer()
{
    shutdown();
}

bool Renderer::init(const Window &window)
{
    if (m_initialized)
    {
        return true;
    }

    m_width = window.getWidth();
    m_height = window.getHeight();
    m_shaderRoot = window.getBasePath() + "shaders/";

    bgfx::renderFrame();

    bgfx::Init init;
    init.type = bgfx::RendererType::Count;

#if defined(_WIN32)
    init.type = bgfx::RendererType::Vulkan;
#endif

    if (window.isWayland())
    {
        init.platformData.type = bgfx::NativeWindowHandleType::Wayland;
    }

    init.swapChain.nwh = window.getNativeWindowHandle();
    init.swapChain.ndt = window.getNativeDisplayHandle();
    init.swapChain.width = m_width;
    init.swapChain.height = m_height;

    init.reset = m_resetFlags;

    if (!bgfx::init(init))
    {
        std::fprintf(stderr, "Renderer: bgfx::init failed\n");
        return false;
    }

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);

    initCube();

    m_initialized = true;
    return true;
}

void Renderer::shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    destroyCubeResources();

    bgfx::shutdown();
    m_initialized = false;
}

void Renderer::destroyCubeResources()
{
    destroyHandle(m_cubeIbh);
    destroyHandle(m_cubeVbh);
    destroyHandle(m_cubeShader.program);
    destroyHandle(m_cubeShader.baseColorSampler);
    destroyHandle(m_cubeShader.baseColorFactor);
    destroyHandle(m_cubeShader.materialParams);
    destroyHandle(m_cubeShader.whiteTexture);
    m_cubeShader = CubeShaderResources{};
}

void Renderer::resize(uint32_t width, uint32_t height)
{
    if (!m_initialized || width == 0 || height == 0)
    {
        return;
    }

    m_width = width;
    m_height = height;

    bgfx::SwapChain swapChain;
    swapChain.width = m_width;
    swapChain.height = m_height;
    bgfx::reset(m_resetFlags, &swapChain);
}

void Renderer::beginFrame()
{
    if (!m_initialized)
        return;

    bgfx::setViewRect(0, 0, 0, uint16_t(m_width), uint16_t(m_height));
    bgfx::touch(0);
}

void Renderer::initCube()
{
    std::vector<CubeVertex> vertices;
    std::vector<uint16_t> indices;

    static const float kNormals[6][3] = {
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};

    vertices.clear();
    indices.clear();

    for (const float *n : kNormals)
    {
        const bx::Vec3 normal = {n[0], n[1], n[2]};
        const bx::Vec3 u = {n[1], n[2], n[0]};
        const bx::Vec3 v = bx::cross(normal, u);

        const uint16_t base = uint16_t(vertices.size());
        const float corners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
        bx::Vec3 positions[4] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
        for (int i = 0; i < 4; ++i)
        {
            positions[i] = bx::add(normal, bx::add(bx::mul(u, corners[i][0]), bx::mul(v, corners[i][1])));
            vertices.push_back({positions[i].x,
                                positions[i].y,
                                positions[i].z,
                                0xffffffff});
        }

        const bx::Vec3 winding = bx::cross(bx::sub(positions[1], positions[0]), bx::sub(positions[2], positions[0]));
        const bool inward = bx::dot(winding, normal) < 0.0f;
        static const uint16_t kInward[6] = {0, 1, 2, 0, 2, 3};
        static const uint16_t kFlipped[6] = {0, 2, 1, 0, 3, 2};
        for (uint16_t i : (inward ? kInward : kFlipped))
            indices.push_back(uint16_t(base + i));
    }

    bgfx::VertexLayout layout;

    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    m_cubeVbh = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(CubeVertex))), layout);
    m_cubeIbh = bgfx::createIndexBuffer(bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint16_t))));

    m_cubeShader.program = loadProgram("vs_cube.sc", "fs_cube.sc");
    m_cubeShader.baseColorSampler = bgfx::createUniform("s_baseColor", bgfx::UniformType::Sampler);
    m_cubeShader.baseColorFactor = bgfx::createUniform("u_baseColorFactor", bgfx::UniformType::Vec4);
    m_cubeShader.materialParams = bgfx::createUniform("u_materialParams", bgfx::UniformType::Vec4);
    const uint32_t white = 0xffffffff;
    m_cubeShader.whiteTexture = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                                      BGFX_TEXTURE_NONE, bgfx::copy(&white, sizeof(white)));
}

void Renderer::drawCube(const float *transform)
{
    if (!m_initialized || !bgfx::isValid(m_cubeShader.program) || !bgfx::isValid(m_cubeVbh))
    {
        return;
    }

    const float params[4] = {-1.0f, 0.0f, 0.0f, 0.0f};
    auto submit = [&](bgfx::ViewId view, const CubeShaderResources &shader)
    {
        bgfx::setTransform(transform);
        bgfx::setVertexBuffer(0, m_cubeVbh);
        bgfx::setIndexBuffer(m_cubeIbh);
        bgfx::setTexture(0, shader.baseColorSampler, shader.whiteTexture);
        bgfx::setState(BGFX_STATE_DEFAULT);
        bgfx::submit(view, shader.program);
    };

    submit(0, m_cubeShader);
}

void Renderer::endFrame()
{
    if (!m_initialized)
    {
        return;
    }

    bgfx::frame();
}

const char *Renderer::shaderDir()
{
    switch (bgfx::getRendererType())
    {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dx11/";
    case bgfx::RendererType::Metal:
        return "metal/";
    case bgfx::RendererType::OpenGL:
        return "glsl/";
    case bgfx::RendererType::OpenGLES:
        return "essl/";
    case bgfx::RendererType::Vulkan:
        return "spirv/";
    default:
        return "unknown/";
    }
}

bgfx::ProgramHandle Renderer::loadProgram(const char *vs, const char *fs) const
{
    bgfx::ShaderHandle vsh = loadShader(vs);
    bgfx::ShaderHandle fsh = loadShader(fs);
    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh))
    {
        if (bgfx::isValid(vsh))
            bgfx::destroy(vsh);
        if (bgfx::isValid(fsh))
            bgfx::destroy(fsh);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(vsh, fsh, true);
}

bgfx::ShaderHandle Renderer::loadShader(const char *name) const
{
    const std::string path = m_shaderRoot + shaderDir() + name + ".bin";

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::fprintf(stderr, "Renderer: failed to open shader %s\n", path.c_str());
        return BGFX_INVALID_HANDLE;
    }

    std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const bgfx::Memory *mem = bgfx::copy(data.data(), uint32_t(data.size()));
    bgfx::ShaderHandle h = bgfx::createShader(mem);
    bgfx::setName(h, name);
    return h;
}