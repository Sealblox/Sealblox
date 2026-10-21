#pragma once

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ECS
{
    struct ModelVertex
    {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
    };

    enum class AlphaMode : uint8_t
    {
        Opaque,
        Mask,
        Blend
    };

    struct ImageData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint8_t mipCount = 1;
        std::vector<uint8_t> rgba;
    };

    struct MaterialData
    {
        std::string name;
        float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        int baseColorImage = -1;
        uint32_t samplerFlags = 0;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
    };

    struct PrimitiveData
    {
        std::string name;
        std::vector<ModelVertex> vertices;
        std::vector<uint32_t> indices;
        int material = -1;
    };

    struct ModelData
    {
        std::vector<PrimitiveData> primitives;
        std::vector<MaterialData> materials;
        std::vector<ImageData> images;
        bx::Vec3 boundsMin = {0.0f, 0.0f, 0.0f};
        bx::Vec3 boundsMax = {0.0f, 0.0f, 0.0f};
        std::vector<std::string> warnings;
    };

    bool loadModelData(const std::string &source, ModelData &out, std::string &error,
                       const std::atomic<bool> *cancel = nullptr);

    bool parseModelData(const void *bytes, size_t size, const std::string &source, ModelData &out,
                        std::string &error, const std::atomic<bool> *cancel = nullptr);

    bool loadImageData(const std::string &source, ImageData &out, std::string &error,
                       const std::atomic<bool> *cancel = nullptr);

    class Texture
    {
    public:
        enum class State
        {
            Loading,
            Ready,
            Failed
        };

        Texture() = default;
        explicit Texture(std::string source) : m_source(std::move(source)) {};
        ~Texture();

        Texture(const Texture &) = delete;
        Texture &operator=(const Texture &) = delete;

        bool create(const ImageData &image);
        void destroy();

        State getState() const { return m_state; }
        bool isReady() const { return m_state == State::Ready; }
        const std::string &getError() const { return m_error; }
        const std::string &getSource() const { return m_source; }
        bgfx::TextureHandle getHandle() const { return m_handle; }

    private:
        friend class ModelLoader;

        void fail(std::string error);

        std::string m_source;
        std::string m_error;
        State m_state = State::Loading;
        bgfx::TextureHandle m_handle = BGFX_INVALID_HANDLE;
    };

    struct PartColors
    {
        std::unordered_map<std::string, std::array<float, 4>> colors;

        std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
    };

    bool parseHexColor(const std::string &hex, float *rgba);
    std::string partKey(const std::string &name);
    bool setPartColor(PartColors &colors, const std::string &part, const std::string &hex);
    void setMaterialTexture(PartColors &colors, const std::string &material, std::shared_ptr<Texture> texture);

    struct ModelShaderResources
    {
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle baseColorSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle baseColorFactor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle materialParams = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle whiteTexture = BGFX_INVALID_HANDLE;
        /* Bound to stage 1 when valid
        bgfx::UniformHandle shadowSampler = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle shadowMap = BGFX_INVALID_HANDLE;
        */
    };

    class Model
    {
    public:
        enum class State
        {
            Loading,
            Ready,
            Failed
        };

        Model() = default;
        explicit Model(std::string source) : m_source(std::move(source)) {};
        ~Model();

        Model(const Model &) = delete;
        Model &operator=(const Model &) = delete;

        bool create(const ModelData &data);
        void destroy();

        bool loadBlocking(const std::string &source);

        void submit(bgfx::ViewId view, const ModelShaderResources &shader, const float *transform,
                    const PartColors *colors) const;

        std::vector<std::string> getPartNames() const;

        State getState() const { return m_state; }
        bool isReady() const { return m_state == State::Ready; }
        const std::string &getError() const { return m_error; }
        const std::string &getSource() const { return m_source; }

        const bx::Vec3 &getBoundsMin() const { return m_boundsMin; }
        const bx::Vec3 &getBoundsMax() const { return m_boundsMax; }
        size_t getPrimitiveCount() const { return m_primitives.size(); }

        void getFitTransform(float *out, float size, const bx::Vec3 &position) const;

    private:
        friend class ModelLoader;

        struct GPUPrimitive
        {
            bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
            bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
            int material = -1;
            std::string name;
            std::string key;
        };

        void fail(std::string error);

        std::string m_source;
        std::string m_error;
        State m_state = State::Loading;

        std::vector<GPUPrimitive> m_primitives;
        std::vector<MaterialData> m_materials;
        std::vector<bgfx::TextureHandle> m_textures;

        bx::Vec3 m_boundsMin = {0.0f, 0.0f, 0.0f};
        bx::Vec3 m_boundsMax = {0.0f, 0.0f, 0.0f};
    };

    class ModelLoader
    {
    public:
        ModelLoader();
        ~ModelLoader();

        ModelLoader(const ModelLoader &) = delete;
        ModelLoader &operator=(const ModelLoader &) = delete;

        std::shared_ptr<Model> load(const std::string &source);

        std::shared_ptr<Texture> loadTexture(const std::string &source);

        std::shared_ptr<Model> find(const std::string &source) const;
        bool isCached(const std::string &source) const { return find(source) != nullptr; }

        void unload(const std::string &source);

        size_t purgeUnused();

        void clearCache();

        size_t getCacheSize() const { return m_cache.size(); }

        void update();

        size_t getPendingCount() const;

    private:
        static std::string cacheKey(const std::string &source);

        void enqueue(const std::shared_ptr<Model> &model, const std::string &source);
        void enqueue(const std::shared_ptr<Texture> &texture, const std::string &source);

        std::unordered_map<std::string, std::shared_ptr<Model>> m_cache;
        std::unordered_map<std::string, std::shared_ptr<Texture>> m_textureCache;

        struct Job
        {
            std::shared_ptr<Model> model;
            std::shared_ptr<Texture> texture;
            std::string source;
            std::unique_ptr<ModelData> data;
            std::unique_ptr<ImageData> image;
            std::string error;
        };

        void workerMain();

        std::thread m_thread;
        mutable std::mutex m_mutex;
        std::condition_variable m_wake;
        std::deque<Job> m_queued;
        std::deque<Job> m_finished;
        size_t m_inFlight = 0;
        std::atomic<bool> m_quit{false};
    };
}