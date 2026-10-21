#include <ecosystem/Model.hpp>
#include <core/http.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace ECS
{

    bool readFile(const std::string &path, std::vector<uint8_t> &out, std::string &error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "cannot open file: " + path;
            return false;
        }
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

    bool fetch(const std::string &source, std::vector<uint8_t> &out, std::string &error,
               const std::atomic<bool> *cancel)
    {
        if (!http::isUrl(source))
            return readFile(source, out, error);

        http::RequestOptions options;
        options.cancel = cancel;
        http::Response response = http::get(source, options);
        if (!response.ok())
        {
            error = source + ": " + (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error);
            return false;
        }
        out = std::move(response.body);
        return true;
    }

    std::string baseOf(const std::string &source)
    {
        const size_t slash = source.find_last_of(http::isUrl(source) ? "/" : "/\\");
        return slash == std::string::npos ? std::string() : source.substr(0, slash + 1);
    }

    std::string resolveUri(const std::string &source, const char *uri)
    {
        std::string u = uri;
        if (http::isUrl(u))
            return u;

        if (http::isUrl(source))
            return baseOf(source) + u;

        std::vector<char> decoded(u.begin(), u.end());
        decoded.push_back('\0');
        cgltf_decode_uri(decoded.data());
        return baseOf(source) + decoded.data();
    }

    bool isDataUri(const char *uri)
    {
        return uri && std::strncmp(uri, "data:", 5) == 0;
    }

    bool decodeDataUri(const char *uri, std::vector<uint8_t> &out)
    {
        const char *comma = std::strchr(uri, ',');
        if (!comma || comma - uri < 7 || std::strncmp(comma - 7, ";base64", 7) != 0)
            return false;

        const char *payload = comma + 1;
        size_t len = std::strlen(payload);
        size_t size = len / 4 * 3;
        if (len >= 1 && payload[len - 1] == '=')
            --size;
        if (len >= 2 && payload[len - 2] == '=')
            --size;
        if (size == 0)
            return false;

        cgltf_options options = {};
        void *data = nullptr;
        if (cgltf_load_buffer_base64(&options, size, payload, &data) != cgltf_result_success)
            return false;

        out.assign(static_cast<uint8_t *>(data), static_cast<uint8_t *>(data) + size);
        std::free(data);
        return true;
    }

    const char *resultString(cgltf_result r)
    {
        switch (r)
        {
        case cgltf_result_success:
            return "success";
        case cgltf_result_data_too_short:
            return "data too short";
        case cgltf_result_unknown_format:
            return "unknown format";
        case cgltf_result_invalid_json:
            return "invalid JSON";
        case cgltf_result_invalid_gltf:
            return "invalid glTF";
        case cgltf_result_invalid_options:
            return "invalid options";
        case cgltf_result_file_not_found:
            return "file not found";
        case cgltf_result_io_error:
            return "I/O error";
        case cgltf_result_out_of_memory:
            return "out of memory";
        case cgltf_result_legacy_gltf:
            return "legacy glTF 1.0 is not supported";
        default:
            return "unknown error";
        }
    }

    void buildMipChain(ImageData &img)
    {
        uint32_t w = img.width;
        uint32_t h = img.height;
        size_t srcOffset = 0;
        img.mipCount = 1;

        while (w > 1 || h > 1)
        {
            const uint32_t nw = std::max(1u, w / 2);
            const uint32_t nh = std::max(1u, h / 2);
            const size_t dstOffset = img.rgba.size();
            img.rgba.resize(dstOffset + size_t(nw) * nh * 4);

            const uint8_t *src = img.rgba.data() + srcOffset;
            uint8_t *dst = img.rgba.data() + dstOffset;

            for (uint32_t y = 0; y < nh; ++y)
            {
                const uint32_t y0 = std::min(y * 2, h - 1);
                const uint32_t y1 = std::min(y * 2 + 1, h - 1);
                for (uint32_t x = 0; x < nw; ++x)
                {
                    const uint32_t x0 = std::min(x * 2, w - 1);
                    const uint32_t x1 = std::min(x * 2 + 1, w - 1);
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        const uint32_t sum = src[(size_t(y0) * w + x0) * 4 + c] +
                                             src[(size_t(y0) * w + x1) * 4 + c] +
                                             src[(size_t(y1) * w + x0) * 4 + c] +
                                             src[(size_t(y1) * w + x1) * 4 + c];
                        dst[(size_t(y) * nw + x) * 4 + c] = uint8_t((sum + 2) / 4);
                    }
                }
            }

            srcOffset = dstOffset;
            w = nw;
            h = nh;
            ++img.mipCount;
        }
    }

    bool decodeImageBytes(const uint8_t *bytes, size_t size, ImageData &out, std::string &error)
    {
        if (!bytes || size == 0 || size > size_t(INT32_MAX))
        {
            error = "image has no data";
            return false;
        }

        int w = 0, h = 0, comp = 0;
        stbi_uc *pixels = stbi_load_from_memory(bytes, int(size), &w, &h, &comp, 4);
        if (!pixels)
        {
            error = std::string("cannot decode image (") + stbi_failure_reason() + ")";
            return false;
        }

        out.width = uint32_t(w);
        out.height = uint32_t(h);
        out.rgba.assign(pixels, pixels + size_t(w) * h * 4);
        stbi_image_free(pixels);

        buildMipChain(out);
        return true;
    }

    bool decodeImage(const cgltf_image &image, const std::string &source, ImageData &out,
                     std::string &error, const std::atomic<bool> *cancel)
    {
        std::vector<uint8_t> encoded;
        const uint8_t *bytes = nullptr;
        size_t size = 0;

        if (image.buffer_view)
        {
            bytes = cgltf_buffer_view_data(image.buffer_view);
            size = image.buffer_view->size;
        }
        else if (isDataUri(image.uri))
        {
            if (!decodeDataUri(image.uri, encoded))
            {
                error = "bad data URI";
                return false;
            }
            bytes = encoded.data();
            size = encoded.size();
        }
        else if (image.uri)
        {
            if (!fetch(resolveUri(source, image.uri), encoded, error, cancel))
                return false;
            bytes = encoded.data();
            size = encoded.size();
        }

        return decodeImageBytes(bytes, size, out, error);
    }

    uint32_t samplerFlagsFrom(const cgltf_sampler *sampler)
    {
        if (!sampler)
            return 0;

        auto wrap = [](cgltf_wrap_mode mode, uint32_t clamp, uint32_t mirror) -> uint32_t
        {
            if (mode == cgltf_wrap_mode_clamp_to_edge)
                return clamp;
            if (mode == cgltf_wrap_mode_mirrored_repeat)
                return mirror;
            return 0;
        };

        uint32_t flags = wrap(sampler->wrap_s, BGFX_SAMPLER_U_CLAMP, BGFX_SAMPLER_U_MIRROR) |
                         wrap(sampler->wrap_t, BGFX_SAMPLER_V_CLAMP, BGFX_SAMPLER_V_MIRROR);

        if (sampler->mag_filter == cgltf_filter_type_nearest)
            flags |= BGFX_SAMPLER_MAG_POINT;
        if (sampler->min_filter == cgltf_filter_type_nearest ||
            sampler->min_filter == cgltf_filter_type_nearest_mipmap_nearest ||
            sampler->min_filter == cgltf_filter_type_nearest_mipmap_linear)
            flags |= BGFX_SAMPLER_MIN_POINT;

        return flags;
    }

    struct Builder
    {
        const cgltf_data *data = nullptr;
        ModelData *out = nullptr;
        bx::Vec3 bmin = {FLT_MAX, FLT_MAX, FLT_MAX};
        bx::Vec3 bmax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        bool warnedSkin = false;
        bool warnedMorph = false;
        bool warnedMode = false;

        void warn(const std::string &message) { out->warnings.push_back(message); }

        void addPrimitive(const cgltf_primitive &prim, const float *world, bool skinned, const char *name);
        void visit(const cgltf_node *node);
    };

    void Builder::addPrimitive(const cgltf_primitive &prim, const float *m, bool skinned, const char *name)
    {
        if (prim.type != cgltf_primitive_type_triangles)
        {
            if (!warnedMode)
                warn("skipped non-triangle primitives (points/lines/strips/fans)");
            warnedMode = true;
            return;
        }
        if (prim.has_draco_mesh_compression)
        {
            warn("skipped a Draco-compressed primitive (not supported)");
            return;
        }
        if (skinned && !warnedSkin)
        {
            warn("skinning is ignored; skinned meshes are shown in their node pose");
            warnedSkin = true;
        }
        if (prim.targets_count && !warnedMorph)
        {
            warn("morph targets are ignored");
            warnedMorph = true;
        }

        const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
        for (cgltf_size i = 0; i < prim.attributes_count; ++i)
        {
            const cgltf_attribute &a = prim.attributes[i];
            if (a.type == cgltf_attribute_type_position && a.index == 0)
                pos = a.data;
            else if (a.type == cgltf_attribute_type_normal && a.index == 0)
                nrm = a.data;
            else if (a.type == cgltf_attribute_type_texcoord && a.index == 0)
                uv = a.data;
        }
        if (!pos || pos->type != cgltf_type_vec3 || pos->count == 0)
            return;

        const size_t count = pos->count;
        std::vector<float> P(count * 3), N, T;
        cgltf_accessor_unpack_floats(pos, P.data(), P.size());
        if (nrm && nrm->type == cgltf_type_vec3 && nrm->count == count)
        {
            N.resize(count * 3);
            cgltf_accessor_unpack_floats(nrm, N.data(), N.size());
        }
        if (uv && uv->type == cgltf_type_vec2 && uv->count == count)
        {
            T.resize(count * 2);
            cgltf_accessor_unpack_floats(uv, T.data(), T.size());
        }

        PrimitiveData result;
        result.name = name ? name : "";
        result.material = prim.material ? int(prim.material - data->materials) : -1;

        if (prim.indices)
        {
            result.indices.resize(prim.indices->count);
            for (cgltf_size i = 0; i < prim.indices->count; ++i)
            {
                const cgltf_size index = cgltf_accessor_read_index(prim.indices, i);
                if (index >= count)
                {
                    warn("skipped a primitive with out-of-range indices");
                    return;
                }
                result.indices[i] = uint32_t(index);
            }
        }
        else
        {
            result.indices.resize(count);
            for (size_t i = 0; i < count; ++i)
                result.indices[i] = uint32_t(i);
        }
        result.indices.resize(result.indices.size() / 3 * 3);
        if (result.indices.empty())
            return;

        if (N.empty())
        {
            N.assign(count * 3, 0.0f);
            for (size_t t = 0; t < result.indices.size(); t += 3)
            {
                const uint32_t ia = result.indices[t], ib = result.indices[t + 1], ic = result.indices[t + 2];
                const bx::Vec3 a = {P[ia * 3], P[ia * 3 + 1], P[ia * 3 + 2]};
                const bx::Vec3 b = {P[ib * 3], P[ib * 3 + 1], P[ib * 3 + 2]};
                const bx::Vec3 c = {P[ic * 3], P[ic * 3 + 1], P[ic * 3 + 2]};
                const bx::Vec3 fn = bx::cross(bx::sub(b, a), bx::sub(c, a));
                for (uint32_t idx : {ia, ib, ic})
                {
                    N[idx * 3] += fn.x;
                    N[idx * 3 + 1] += fn.y;
                    N[idx * 3 + 2] += fn.z;
                }
            }
        }

        const bx::Vec3 c0 = {m[0], m[1], m[2]};
        const bx::Vec3 c1 = {m[4], m[5], m[6]};
        const bx::Vec3 c2 = {m[8], m[9], m[10]};
        const bx::Vec3 n0 = bx::cross(c1, c2);
        const bx::Vec3 n1 = bx::cross(c2, c0);
        const bx::Vec3 n2 = bx::cross(c0, c1);
        const float det = bx::dot(c0, n0);
        const float nsign = det < 0.0f ? -1.0f : 1.0f;

        result.vertices.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            const float px = P[i * 3], py = P[i * 3 + 1], pz = P[i * 3 + 2];
            const float nx = N[i * 3], ny = N[i * 3 + 1], nz = N[i * 3 + 2];

            bx::Vec3 p = {m[0] * px + m[4] * py + m[8] * pz + m[12],
                          m[1] * px + m[5] * py + m[9] * pz + m[13],
                          m[2] * px + m[6] * py + m[10] * pz + m[14]};
            bx::Vec3 n = bx::mul(bx::add(bx::add(bx::mul(n0, nx), bx::mul(n1, ny)), bx::mul(n2, nz)), nsign);
            const float len = bx::length(n);
            n = len > 1e-12f ? bx::mul(n, 1.0f / len) : bx::Vec3{0.0f, 1.0f, 0.0f};

            p.z = -p.z;
            n.z = -n.z;

            ModelVertex &v = result.vertices[i];
            v.x = p.x;
            v.y = p.y;
            v.z = p.z;
            v.nx = n.x;
            v.ny = n.y;
            v.nz = n.z;
            v.u = T.empty() ? 0.0f : T[i * 2];
            v.v = T.empty() ? 0.0f : T[i * 2 + 1];

            bmin = bx::min(bmin, p);
            bmax = bx::max(bmax, p);
        }

        if (det < 0.0f)
        {
            for (size_t t = 0; t < result.indices.size(); t += 3)
                std::swap(result.indices[t + 1], result.indices[t + 2]);
        }

        out->primitives.push_back(std::move(result));
    }

    void Builder::visit(const cgltf_node *node)
    {
        if (node->mesh)
        {
            float world[16];
            cgltf_node_transform_world(node, world);
            const char *name = node->name ? node->name : node->mesh->name;
            for (cgltf_size i = 0; i < node->mesh->primitives_count; ++i)
                addPrimitive(node->mesh->primitives[i], world, node->skin != nullptr, name);
        }
        for (cgltf_size i = 0; i < node->children_count; ++i)
            visit(node->children[i]);
    }

    bool checkRequiredExtensions(const cgltf_data *data, std::string &error)
    {
        static const char *kSupported[] = {
            "KHR_mesh_quantization",
            "KHR_materials_unlit",
            "KHR_texture_transform",
        };
        for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
        {
            const char *ext = data->extensions_required[i];
            bool ok = false;
            for (const char *s : kSupported)
                ok = ok || std::strcmp(ext, s) == 0;
            if (!ok)
            {
                error = std::string("requires unsupported extension ") + ext;
                return false;
            }
        }
        return true;
    }

    bool loadModelData(const std::string &source, ModelData &out, std::string &error,
                       const std::atomic<bool> *cancel)
    {
        std::vector<uint8_t> bytes;
        if (!fetch(source, bytes, error, cancel))
            return false;
        return parseModelData(bytes.data(), bytes.size(), source, out, error, cancel);
    }

    bool loadImageData(const std::string &source, ImageData &out, std::string &error,
                       const std::atomic<bool> *cancel)
    {
        std::vector<uint8_t> bytes;
        if (!fetch(source, bytes, error, cancel))
            return false;
        if (!decodeImageBytes(bytes.data(), bytes.size(), out, error))
        {
            error = source + ": " + error;
            return false;
        }
        return true;
    }

    bool parseModelData(const void *bytes, size_t size, const std::string &source, ModelData &out,
                        std::string &error, const std::atomic<bool> *cancel)
    {
        out = ModelData{};

        cgltf_options options = {};
        cgltf_data *data = nullptr;
        cgltf_result r = cgltf_parse(&options, bytes, size, &data);
        if (r != cgltf_result_success)
        {
            error = std::string("glTF parse failed: ") + resultString(r);
            return false;
        }
        std::unique_ptr<cgltf_data, void (*)(cgltf_data *)> guard(data, cgltf_free);

        if (!checkRequiredExtensions(data, error))
            return false;

        for (cgltf_size i = 0; i < data->buffers_count; ++i)
        {
            cgltf_buffer &buffer = data->buffers[i];
            if (buffer.data || !buffer.uri || isDataUri(buffer.uri))
                continue;

            std::vector<uint8_t> contents;
            if (!fetch(resolveUri(source, buffer.uri), contents, error, cancel))
                return false;
            if (contents.size() < buffer.size)
            {
                error = std::string("buffer too short: ") + buffer.uri;
                return false;
            }
            buffer.data = std::malloc(contents.size());
            if (!buffer.data)
            {
                error = "out of memory";
                return false;
            }
            std::memcpy(buffer.data, contents.data(), contents.size());
            buffer.data_free_method = cgltf_data_free_method_memory_free;
        }

        r = cgltf_load_buffers(&options, data, nullptr);
        if (r != cgltf_result_success)
        {
            error = std::string("loading buffers failed: ") + resultString(r);
            return false;
        }

        r = cgltf_validate(data);
        if (r != cgltf_result_success)
        {
            error = std::string("glTF validation failed: ") + resultString(r);
            return false;
        }

        std::unordered_map<const cgltf_image *, int> imageIndex;
        out.materials.resize(data->materials_count);
        for (cgltf_size i = 0; i < data->materials_count; ++i)
        {
            const cgltf_material &src = data->materials[i];
            MaterialData &dst = out.materials[i];
            if (src.name)
                dst.name = src.name;
            dst.doubleSided = src.double_sided;
            dst.alphaCutoff = src.alpha_cutoff;
            dst.alphaMode = src.alpha_mode == cgltf_alpha_mode_blend  ? AlphaMode::Blend
                            : src.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
                                                                      : AlphaMode::Opaque;

            if (!src.has_pbr_metallic_roughness)
                continue;

            const cgltf_pbr_metallic_roughness &pbr = src.pbr_metallic_roughness;
            std::memcpy(dst.baseColor, pbr.base_color_factor, sizeof(dst.baseColor));

            const cgltf_texture *tex = pbr.base_color_texture.texture;
            if (!tex || !tex->image)
                continue;

            if (pbr.base_color_texture.texcoord != 0)
                out.warnings.push_back("material '" + dst.name + "' uses TEXCOORD_" +
                                       std::to_string(pbr.base_color_texture.texcoord) +
                                       "; only TEXCOORD_0 is supported");

            dst.samplerFlags = samplerFlagsFrom(tex->sampler);

            auto it = imageIndex.find(tex->image);
            if (it == imageIndex.end())
            {
                ImageData image;
                std::string imageError;
                int index = -1;
                if (decodeImage(*tex->image, source, image, imageError, cancel))
                {
                    index = int(out.images.size());
                    out.images.push_back(std::move(image));
                }
                else
                {
                    out.warnings.push_back("texture skipped: " + imageError);
                }
                it = imageIndex.emplace(tex->image, index).first;
            }
            dst.baseColorImage = it->second;
        }

        if (cancel && cancel->load())
        {
            error = "cancelled";
            return false;
        }

        Builder builder;
        builder.data = data;
        builder.out = &out;

        const cgltf_scene *scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
        if (scene)
        {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i)
                builder.visit(scene->nodes[i]);
        }
        else
        {
            for (cgltf_size i = 0; i < data->nodes_count; ++i)
                if (!data->nodes[i].parent)
                    builder.visit(&data->nodes[i]);
        }

        if (out.primitives.empty())
        {
            error = "no drawable triangle meshes found";
            return false;
        }

        out.boundsMin = builder.bmin;
        out.boundsMax = builder.bmax;
        return true;
    }

    const bgfx::VertexLayout &modelLayout()
    {
        static bgfx::VertexLayout layout = []
        {
            bgfx::VertexLayout l;
            l.begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .end();
            return l;
        }();
        return layout;
    }

    Texture::~Texture()
    {
        destroy();
    }

    void Texture::destroy()
    {
        if (bgfx::isValid(m_handle))
            bgfx::destroy(m_handle);
        m_handle = BGFX_INVALID_HANDLE;
    }

    void Texture::fail(std::string error)
    {
        destroy();
        m_error = std::move(error);
        m_state = State::Failed;
    }

    bool Texture::create(const ImageData &image)
    {
        destroy();

        const bgfx::Caps *caps = bgfx::getCaps();
        if (!caps || image.width > caps->limits.maxTextureSize || image.height > caps->limits.maxTextureSize)
        {
            fail("texture is larger than the GPU supports");
            return false;
        }

        m_handle = bgfx::createTexture2D(uint16_t(image.width), uint16_t(image.height), image.mipCount > 1, 1,
                                         bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                         bgfx::copy(image.rgba.data(), uint32_t(image.rgba.size())));
        if (!bgfx::isValid(m_handle))
        {
            fail("texture creation failed");
            return false;
        }

        m_state = State::Ready;
        return true;
    }

    Model::~Model()
    {
        destroy();
    }

    void Model::destroy()
    {
        for (GPUPrimitive &p : m_primitives)
        {
            if (bgfx::isValid(p.vbh))
                bgfx::destroy(p.vbh);
            if (bgfx::isValid(p.ibh))
                bgfx::destroy(p.ibh);
        }
        for (bgfx::TextureHandle t : m_textures)
            if (bgfx::isValid(t))
                bgfx::destroy(t);

        m_primitives.clear();
        m_textures.clear();
        m_materials.clear();
    }

    void Model::fail(std::string error)
    {
        destroy();
        m_error = std::move(error);
        m_state = State::Failed;
    }

    bool Model::create(const ModelData &data)
    {
        destroy();

        const bgfx::Caps *caps = bgfx::getCaps();
        const bool index32 = caps && (caps->supported & BGFX_CAPS_INDEX32) != 0;

        for (const ImageData &img : data.images)
        {
            bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;
            if (caps && img.width <= caps->limits.maxTextureSize && img.height <= caps->limits.maxTextureSize)
            {
                tex = bgfx::createTexture2D(uint16_t(img.width), uint16_t(img.height), img.mipCount > 1, 1,
                                            bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                            bgfx::copy(img.rgba.data(), uint32_t(img.rgba.size())));
            }
            m_textures.push_back(tex);
        }
        m_materials = data.materials;

        for (const PrimitiveData &prim : data.primitives)
        {
            GPUPrimitive gpu;
            gpu.material = prim.material;
            gpu.name = prim.name;
            gpu.key = partKey(prim.name);
            gpu.vbh = bgfx::createVertexBuffer(
                bgfx::copy(prim.vertices.data(), uint32_t(prim.vertices.size() * sizeof(ModelVertex))),
                modelLayout());

            if (prim.vertices.size() <= 0xffff)
            {
                std::vector<uint16_t> small(prim.indices.begin(), prim.indices.end());
                gpu.ibh = bgfx::createIndexBuffer(bgfx::copy(small.data(), uint32_t(small.size() * sizeof(uint16_t))));
            }
            else if (index32)
            {
                gpu.ibh = bgfx::createIndexBuffer(
                    bgfx::copy(prim.indices.data(), uint32_t(prim.indices.size() * sizeof(uint32_t))),
                    BGFX_BUFFER_INDEX32);
            }
            else
            {
                bgfx::destroy(gpu.vbh);
                m_error = "mesh needs 32-bit indices, which this GPU/backend does not support";
                continue;
            }
            m_primitives.push_back(gpu);
        }

        if (m_primitives.empty())
        {
            fail(m_error.empty() ? "no primitives uploaded" : m_error);
            return false;
        }

        m_boundsMin = data.boundsMin;
        m_boundsMax = data.boundsMax;
        m_state = State::Ready;
        return true;
    }

    bool Model::loadBlocking(const std::string &source)
    {
        m_source = source;
        ModelData data;
        std::string error;
        if (!loadModelData(source, data, error))
        {
            fail(error);
            return false;
        }
        return create(data);
    }

    std::string partKey(const std::string &name)
    {
        std::string key;
        key.reserve(name.size());
        for (unsigned char c : name)
            if (c != ' ' && c != '_' && c != '-')
                key.push_back(char(std::tolower(c)));
        return key;
    }

    bool parseHexColor(const std::string &hex, float *rgba)
    {
        size_t start = 0;
        while (start < hex.size() && std::isspace(static_cast<unsigned char>(hex[start])))
            ++start;
        if (start < hex.size() && hex[start] == '#')
            ++start;
        size_t end = hex.size();
        while (end > start && std::isspace(static_cast<unsigned char>(hex[end - 1])))
            --end;

        const size_t length = end - start;
        if (length != 3 && length != 4 && length != 6 && length != 8)
            return false;

        int digits[8];
        for (size_t i = 0; i < length; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(hex[start + i]);
            if (!std::isxdigit(c))
                return false;
            digits[i] = std::isdigit(c) ? c - '0' : std::tolower(c) - 'a' + 10;
        }

        rgba[3] = 1.0f;
        if (length <= 4)
        {
            for (size_t i = 0; i < length; ++i)
                rgba[i] = float(digits[i] * 17) / 255.0f;
        }
        else
        {
            for (size_t i = 0; i < length / 2; ++i)
                rgba[i] = float(digits[i * 2] * 16 + digits[i * 2 + 1]) / 255.0f;
        }
        return true;
    }

    bool setPartColor(PartColors &colors, const std::string &part, const std::string &hex)
    {
        std::array<float, 4> rgba;
        if (!parseHexColor(hex, rgba.data()))
        {
            std::fprintf(stderr, "Model: bad color \"%s\" for part \"%s\"\n", hex.c_str(), part.c_str());
            return false;
        }
        colors.colors[partKey(part)] = rgba;
        return true;
    }

    void setMaterialTexture(PartColors &colors, const std::string &material, std::shared_ptr<Texture> texture)
    {
        colors.textures[partKey(material)] = std::move(texture);
    }

    std::vector<std::string> Model::getPartNames() const
    {
        std::vector<std::string> names;
        for (const GPUPrimitive &prim : m_primitives)
            if (!prim.name.empty() && std::find(names.begin(), names.end(), prim.name) == names.end())
                names.push_back(prim.name);
        return names;
    }

    void Model::submit(bgfx::ViewId view, const ModelShaderResources &shader, const float *transform,
                       const PartColors *colors) const
    {
        if (m_state != State::Ready || !bgfx::isValid(shader.program))
            return;

        static const MaterialData kDefaultMaterial;

        for (const GPUPrimitive &prim : m_primitives)
        {
            const MaterialData &mat =
                (prim.material >= 0 && size_t(prim.material) < m_materials.size()) ? m_materials[prim.material] : kDefaultMaterial;

            bgfx::TextureHandle texture = shader.whiteTexture;
            if (mat.baseColorImage >= 0 && size_t(mat.baseColorImage) < m_textures.size() &&
                bgfx::isValid(m_textures[mat.baseColorImage]))
                texture = m_textures[mat.baseColorImage];

            bool overlay = false;
            if (colors && !colors->textures.empty() && !mat.name.empty())
            {
                const auto it = colors->textures.find(partKey(mat.name));
                if (it != colors->textures.end() && it->second && it->second->isReady())
                {
                    texture = it->second->getHandle();
                    overlay = true;
                }
            }

            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
            if (!mat.doubleSided)
                state |= BGFX_STATE_CULL_CW;
            if (mat.alphaMode == AlphaMode::Blend)
                state |= BGFX_STATE_BLEND_ALPHA;
            else
                state |= BGFX_STATE_WRITE_Z;

            const float params[4] = {mat.alphaMode == AlphaMode::Mask ? mat.alphaCutoff : -1.0f,
                                     overlay ? 1.0f : 0.0f, 0.0f, 0.0f};

            bgfx::setTransform(transform);
            bgfx::setVertexBuffer(0, prim.vbh);
            bgfx::setIndexBuffer(prim.ibh);
            float baseColor[4] = {mat.baseColor[0], mat.baseColor[1], mat.baseColor[2], mat.baseColor[3]};
            if (colors && !prim.key.empty())
            {
                const auto it = colors->colors.find(prim.key);
                if (it != colors->colors.end())
                {
                    baseColor[0] = it->second[0];
                    baseColor[1] = it->second[1];
                    baseColor[2] = it->second[2];
                }
            }

            bgfx::setUniform(shader.baseColorFactor, baseColor);
            bgfx::setUniform(shader.materialParams, params);
            bgfx::setTexture(0, shader.baseColorSampler, texture, mat.samplerFlags);
            bgfx::setState(state);
            bgfx::submit(view, shader.program);
        }
    }

    void Model::getFitTransform(float *out, float size, const bx::Vec3 &position) const
    {
        const bx::Vec3 extent = bx::sub(m_boundsMax, m_boundsMin);
        const float largest = bx::max(extent.x, bx::max(extent.y, extent.z));
        const float s = largest > 0.0f ? size / largest : 1.0f;

        const float cx = (m_boundsMin.x + m_boundsMax.x) * 0.5f;
        const float cz = (m_boundsMin.z + m_boundsMax.z) * 0.5f;

        bx::mtxSRT(out, s, s, s, 0.0f, 0.0f, 0.0f,
                   position.x - cx * s, position.y - m_boundsMin.y * s, position.z - cz * s);
    }

    ModelLoader::ModelLoader()
    {
        m_thread = std::thread(&ModelLoader::workerMain, this);
    }

    ModelLoader::~ModelLoader()
    {
        m_quit = true;
        m_wake.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    std::string ModelLoader::cacheKey(const std::string &source)
    {
        if (http::isUrl(source))
            return source;

        std::error_code ec;
        const std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::absolute(source, ec), ec);
        return ec ? source : p.lexically_normal().string();
    }

    std::shared_ptr<Model> ModelLoader::load(const std::string &source)
    {
        const std::string key = cacheKey(source);

        auto it = m_cache.find(key);
        if (it != m_cache.end())
        {
            std::shared_ptr<Model> &cached = it->second;

            if (cached->getState() == Model::State::Failed)
            {
                cached->m_error.clear();
                cached->m_state = Model::State::Loading;
                enqueue(cached, source);
            }
            return cached;
        }

        auto model = std::make_shared<Model>(source);
        m_cache.emplace(key, model);
        enqueue(model, source);
        return model;
    }

    std::shared_ptr<Texture> ModelLoader::loadTexture(const std::string &source)
    {
        const std::string key = cacheKey(source);

        auto it = m_textureCache.find(key);
        if (it != m_textureCache.end())
        {
            std::shared_ptr<Texture> &cached = it->second;
            if (cached->getState() == Texture::State::Failed)
            {
                cached->m_error.clear();
                cached->m_state = Texture::State::Loading;
                enqueue(cached, source);
            }
            return cached;
        }

        auto texture = std::make_shared<Texture>(source);
        m_textureCache.emplace(key, texture);
        enqueue(texture, source);
        return texture;
    }

    void ModelLoader::enqueue(const std::shared_ptr<Model> &model, const std::string &source)
    {
        Job job;
        job.model = model;
        job.source = source;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queued.push_back(std::move(job));
            ++m_inFlight;
        }
        m_wake.notify_one();
    }

    void ModelLoader::enqueue(const std::shared_ptr<Texture> &texture, const std::string &source)
    {
        Job job;
        job.texture = texture;
        job.source = source;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queued.push_back(std::move(job));
            ++m_inFlight;
        }
        m_wake.notify_one();
    }

    std::shared_ptr<Model> ModelLoader::find(const std::string &source) const
    {
        auto it = m_cache.find(cacheKey(source));
        return it != m_cache.end() ? it->second : nullptr;
    }

    void ModelLoader::unload(const std::string &source)
    {
        m_cache.erase(cacheKey(source));
    }

    size_t ModelLoader::purgeUnused()
    {
        size_t released = 0;
        for (auto it = m_cache.begin(); it != m_cache.end();)
        {
            if (it->second.use_count() == 1)
            {
                it = m_cache.erase(it);
                ++released;
            }
            else
            {
                ++it;
            }
        }
        for (auto it = m_textureCache.begin(); it != m_textureCache.end();)
        {
            if (it->second.use_count() == 1)
            {
                it = m_textureCache.erase(it);
                ++released;
            }
            else
            {
                ++it;
            }
        }
        return released;
    }

    void ModelLoader::clearCache()
    {
        m_cache.clear();
        m_textureCache.clear();
    }

    void ModelLoader::workerMain()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait(lock, [this]
                            { return m_quit.load() || !m_queued.empty(); });
                if (m_quit)
                    return;
                job = std::move(m_queued.front());
                m_queued.pop_front();
            }

            if (job.texture)
            {
                auto image = std::make_unique<ImageData>();
                if (loadImageData(job.source, *image, job.error, &m_quit))
                    job.image = std::move(image);
            }
            else
            {
                auto data = std::make_unique<ModelData>();
                if (loadModelData(job.source, *data, job.error, &m_quit))
                    job.data = std::move(data);
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            m_finished.push_back(std::move(job));
        }
    }

    void ModelLoader::update()
    {
        std::deque<Job> finished;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            finished.swap(m_finished);
            m_inFlight -= finished.size();
        }

        for (Job &job : finished)
        {
            if (job.texture)
            {
                if (!job.image)
                {
                    job.texture->fail(job.error);
                    std::fprintf(stderr, "Texture: failed to load %s: %s\n", job.source.c_str(), job.error.c_str());
                }
                else if (job.texture->create(*job.image))
                    std::printf("Texture: loaded %s (%ux%u)\n", job.source.c_str(), job.image->width, job.image->height);
                else
                    std::fprintf(stderr, "Texture: GPU upload failed for %s: %s\n", job.source.c_str(),
                                 job.texture->getError().c_str());
                continue;
            }

            if (!job.data)
            {
                job.model->fail(job.error);
                std::fprintf(stderr, "Model: failed to load %s: %s\n", job.source.c_str(), job.error.c_str());
                continue;
            }

            for (const std::string &w : job.data->warnings)
                std::fprintf(stderr, "Model: %s: %s\n", job.source.c_str(), w.c_str());

            if (job.model->create(*job.data))
            {
                std::printf("Model: loaded %s (%zu primitives, %zu textures)\n", job.source.c_str(),
                            job.data->primitives.size(), job.data->images.size());
                for (const std::string &part : job.model->getPartNames())
                    std::printf("Model:   part \"%s\"\n", part.c_str());
            }
            else
                std::fprintf(stderr, "Model: GPU upload failed for %s: %s\n", job.source.c_str(),
                             job.model->getError().c_str());
        }
    }

    size_t ModelLoader::getPendingCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_inFlight;
    }
}