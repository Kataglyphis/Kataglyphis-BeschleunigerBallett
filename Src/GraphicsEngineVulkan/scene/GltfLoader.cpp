module;

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_transform_2d.hpp>

#include "spdlog/spdlog.h"

#include <cgltf.h>

module kataglyphis.vulkan.gltf_loader;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.mesh_range;
import kataglyphis.vulkan.model_assembly;
import kataglyphis.vulkan.sampler_builder;
import kataglyphis.shared.util.file_reader;

namespace Kataglyphis {

GltfLoader::GltfLoader(std::shared_ptr<VulkanDevice> device, vk::CommandPool command_pool)  // DEVICE_SINK_OK: moved into member
  : device(std::move(device)), command_pool(command_pool)
{}

std::shared_ptr<Model> GltfLoader::loadModel(const std::string &modelFile)
{
    if (!parseCpu(modelFile)) {
        spdlog::error("Failed to parse glTF: {}", modelFile);
        return nullptr;
    }
    return uploadParsed();
}

void GltfLoader::adoptParsed(GltfLoader &&other)
{
    vertices = std::move(other.vertices);
    indices = std::move(other.indices);
    materials = std::move(other.materials);
    materialIndex = std::move(other.materialIndex);
    textureImages = std::move(other.textureImages);
    textureSamplerDescs = std::move(other.textureSamplerDescs);
    meshRanges = std::move(other.meshRanges);
}

std::shared_ptr<Model> GltfLoader::uploadParsed()
{
    if (!device) {
        spdlog::error("GltfLoader::uploadParsed called on a device-free loader");
        return nullptr;
    }
    if (vertices.empty()) {
        spdlog::error("GltfLoader::uploadParsed called before a successful parseCpu");
        return nullptr;
    }

    std::shared_ptr<Model> model = std::make_shared<Model>(device);

    // Decode + upload each recorded base-colour image (increment d); textureID
    // in each material indexes into these, added in the same order. If a
    // document had no textures, reserve the default so textureID 0 is valid -
    // matching the OBJ path.
    for (std::size_t i = 0; i < textureImages.size(); ++i) {
        const std::vector<unsigned char> &encoded = textureImages[i];
        Texture texture;
        const bool created = texture.createFromMemory(device, command_pool, encoded.data(), encoded.size());
        addTextureOrDefault(*model, device, command_pool, created, std::move(texture), textureSamplerDescs[i]);
    }
    ensureAtLeastOneTexture(*model, device, command_pool);

    // One Mesh per glTF primitive (backlog #10). Each range is that primitive's
    // slice of the flat arrays; a sub-mesh's indices are re-based to its own
    // vertex subset. A single-primitive glTF has exactly one range spanning
    // everything, so this builds one mesh - behaviour-identical to before. Each
    // mesh shares the full materials array (its materialIndex holds the original
    // indices); a per-mesh material subset is a later optimisation.
    addMeshesForRanges(*model, device, command_pool, vertices, indices, materialIndex, materials, meshRanges);
    return model;
}

TexCoordSetInfo describeTexCoordSet(int texcoord)
{
    return { static_cast<unsigned int>(texcoord), texcoord == 0 };
}

namespace {

/// Neutral Lambertian stand-in, used for primitives with no material. The
/// fields are ObjMaterial's; textureID -1 means untextured.
ObjMaterial neutralMaterial()
{
    return ObjMaterial(glm::vec3(0.8F),// diffuse
      glm::vec3(0.0F),// emission
      1.0F,// shininess
      1.0F,// dissolve
      -1);// textureID
}

/// Maps a glTF material to the engine's ObjMaterial. Base-colour factor becomes
/// diffuse (the dominant term this forward renderer reads) and its alpha becomes
/// dissolve. metallic_factor and roughness_factor both carry through losslessly
/// to ObjMaterial::metallic and ObjMaterial::roughness. `shininess` (below) is
/// still derived from roughness_factor and kept as the OBJ-only fallback path:
/// OBJ has no roughness input, only `Ns`, so material_roughness() uses it only
/// when ObjMaterial::roughness is the "no authored roughness" sentinel.
/// Textures are incremented; textureID stays -1 here.
ObjMaterial fromGltfMaterial(const cgltf_material &material)
{
    glm::vec3 baseColor(0.8F);
    float baseAlpha = 1.0F;
    float roughness = 1.0F;
    float metallic = 0.0F;
    // -1.0F: no authored roughness (matches ObjMaterial's sentinel default).
    // Set below only when the material actually has a pbr_metallic_roughness
    // block, so materials without one keep resolving through `shininess`.
    float authoredRoughness = -1.0F;
    const char *materialName = material.name != nullptr ? material.name : "<unnamed>";
    if (material.has_pbr_metallic_roughness != 0) {
        const cgltf_pbr_metallic_roughness &pbr = material.pbr_metallic_roughness;
        baseColor = glm::vec3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
        baseAlpha = pbr.base_color_factor[3];
        roughness = pbr.roughness_factor;
        metallic = glm::clamp(pbr.metallic_factor, 0.0F, 1.0F);
        authoredRoughness = glm::clamp(pbr.roughness_factor, 0.0F, 1.0F);

        // Vertex has exactly one UV slot (bound to TEXCOORD_0); a base-colour
        // texture that names any other set is silently mis-sampled unless we
        // say so. The Rust loader supports TEXCOORD_0/1 and warns past that -
        // this loader supports only TEXCOORD_0, so it warns on any non-zero set.
        const TexCoordSetInfo texCoordInfo = describeTexCoordSet(pbr.base_color_texture.texcoord);
        if (!texCoordInfo.supported) {
            spdlog::warn("GltfLoader: material '{}' base-colour texture uses TEXCOORD_{}, but only TEXCOORD_0 is "
                         "supported; sampling with UV0",
              materialName, texCoordInfo.set);
        }
    }
    // KHR_materials_emissive_strength scales the emissive contribution past the
    // [0,1] glTF factor range (for HDR emitters). Fold it into the factor so the
    // shading paths stay unchanged; default 1.0 when the extension is absent.
    const float emissiveStrength = material.has_emissive_strength != 0 ? material.emissive_strength.emissive_strength : 1.0F;
    const glm::vec3 emission(material.emissive_factor[0] * emissiveStrength,
      material.emissive_factor[1] * emissiveStrength,
      material.emissive_factor[2] * emissiveStrength);
    // Smoother surfaces (low roughness) get a tighter, stronger highlight.
    const float shininess = glm::mix(128.0F, 1.0F, glm::clamp(roughness, 0.0F, 1.0F));

    // glTF alphaMode MASK -> the shader discards where base-colour alpha < cutoff
    // (cut-out foliage/decals). OPAQUE and BLEND map to -1 (never discard); real
    // BLEND compositing needs a sorted transparent pass that this engine does not
    // have yet, so BLEND currently renders opaque - MASK is the common cut-out case.
    const float alphaCutoff = (material.alpha_mode == cgltf_alpha_mode_mask) ? material.alpha_cutoff : -1.0F;

    // glTF KHR_texture_transform on the base-colour texture: the full T*R*S UV
    // transform. Absent -> identity rows (1,0,0)/(0,1,0), so untransformed
    // materials are bit-unchanged.
    glm::vec3 uvTransformRow0(1.0F, 0.0F, 0.0F);
    glm::vec3 uvTransformRow1(0.0F, 1.0F, 0.0F);
    if (material.has_pbr_metallic_roughness != 0 && material.pbr_metallic_roughness.base_color_texture.has_transform != 0) {
        const cgltf_texture_transform &transform = material.pbr_metallic_roughness.base_color_texture.transform;
        const glm::vec2 offset(transform.offset[0], transform.offset[1]);
        const glm::vec2 scaleVec(transform.scale[0], transform.scale[1]);

        // T * R * S, same convention as the Rust loader's gltf_loader.rs:618-625,
        // including the negated rotation sign (glTF's `rotation` is clockwise in
        // UV space).
        const glm::mat3 uvMatrix =
          glm::scale(glm::rotate(glm::translate(glm::mat3(1.0F), offset), -transform.rotation), scaleVec);
        uvTransformRow0 = glm::vec3(uvMatrix[0][0], uvMatrix[1][0], uvMatrix[2][0]);
        uvTransformRow1 = glm::vec3(uvMatrix[0][1], uvMatrix[1][1], uvMatrix[2][1]);

        // KHR_texture_transform can itself override which UV set the transform
        // applies to. That is not applied - so say so rather than silently
        // dropping it.
        if (transform.has_texcoord != 0 && transform.texcoord != 0) {
            spdlog::warn("GltfLoader: material '{}' KHR_texture_transform overrides the base-colour UV set to "
                         "TEXCOORD_{}, but only TEXCOORD_0 is supported; ignoring the override",
              materialName, static_cast<unsigned int>(transform.texcoord));
        }
    }

    return ObjMaterial(baseColor,// diffuse
      emission,// emission
      shininess,// shininess
      baseAlpha,// dissolve (glTF baseColorFactor.a)
      -1,// textureID (increment d)
      alphaCutoff,// glTF MASK cutoff (-1 = OPAQUE/BLEND)
      uvTransformRow0,// KHR_texture_transform T*R*S row 0
      uvTransformRow1,// KHR_texture_transform T*R*S row 1
      metallic,// glTF pbrMetallicRoughness.metallicFactor
      authoredRoughness);// glTF pbrMetallicRoughness.roughnessFactor (-1 = not authored)
}

/// Reads a float attribute (2, 3 or 4 components) into `out`, one entry per
/// accessor element. cgltf handles the underlying component type and stride.
template<int N, typename VecT>
void readAttribute(const cgltf_accessor *accessor, std::vector<VecT> &out)
{
    // Pre-fill with 1.0: cgltf_accessor_read_float writes only
    // min(accessor's component count, N) floats, so a VEC3 COLOR_0 accessor
    // read with N=4 leaves the 4th (alpha) component untouched. Pre-filling
    // makes that case default to alpha = 1.0 instead of uninitialized memory.
    // A no-op for every attribute whose accessor component count equals N
    // (position, normal, uv), since the read then overwrites every component.
    out.assign(accessor->count, VecT(1.0F));
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        cgltf_accessor_read_float(accessor, i, glm::value_ptr(out[i]), N);
    }
}

/// Percent-decodes a URI (glTF requires URI-encoding for reserved characters,
/// so a real exporter emits a space as %20). Decodes only well-formed %XX
/// triplets (two valid hex digits); anything else - a bare '%' or a truncated
/// escape - is left byte-for-byte rather than failing the whole decode.
std::string percentDecodeUri(const std::string &uri)
{
    std::string out;
    out.reserve(uri.size());
    for (std::string::size_type i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[i + 1])) != 0
            && std::isxdigit(static_cast<unsigned char>(uri[i + 2])) != 0) {
            out.push_back(static_cast<char>(std::strtoul(uri.substr(i + 1, 2).c_str(), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(uri[i]);
        }
    }
    return out;
}

/// True if `candidate` (already weakly_canonical) is `base` itself or nested
/// under it. Used to reject a glTF image URI that walks out of the document's
/// directory via `..` - a `.gltf` is untrusted input fed from the GUI's file
/// picker. lexically_relative (rather than a string-prefix compare) avoids the
/// classic "/foo/bar" matching "/foo/barbaz" false positive.
bool isWithinDirectory(const std::filesystem::path &candidate, const std::filesystem::path &base)
{
    const std::filesystem::path rel = candidate.lexically_relative(base);
    return !rel.empty() && rel.begin()->string() != "..";
}

/// Returns the ENCODED image bytes (PNG/JPG/...) for a glTF image, or empty if
/// unavailable. Handles the three forms a glTF document can use: glb
/// buffer-view embedded (bytes in a loaded buffer), a base64 data-URI (cgltf
/// decodes it), and a sibling-file URI resolved against `documentDir` - the
/// layout `obj2gltf`/`obj_to_gltf.rs` emits. A remote URI (`http:`/`https:`)
/// is out of scope and yields no bytes, not a fetch.
std::vector<unsigned char> extractImageBytes(
  const cgltf_image *image, const cgltf_options &options, const std::filesystem::path &documentDir)
{
    if (image == nullptr) { return {}; }

    if (image->buffer_view != nullptr && image->buffer_view->buffer != nullptr
        && image->buffer_view->buffer->data != nullptr) {
        const auto *base = static_cast<const unsigned char *>(image->buffer_view->buffer->data);
        const cgltf_size offset = image->buffer_view->offset;
        const cgltf_size size = image->buffer_view->size;
        // A malformed file can declare a buffer view that runs past its
        // buffer; slicing base+offset..base+offset+size then reads OOB. Reject
        // any view that does not fit (offset+size guarded against overflow).
        const cgltf_size buffer_size = image->buffer_view->buffer->size;
        if (offset > buffer_size || size > buffer_size - offset) { return {}; }
        return std::vector<unsigned char>(base + offset, base + offset + size);
    }

    if (image->uri != nullptr) {
        const std::string uri = image->uri;
        const std::string marker = "base64,";
        const std::string::size_type pos = uri.find(marker);
        if (pos != std::string::npos) {
            const char *b64 = uri.c_str() + pos + marker.size();
            const cgltf_size b64len = uri.size() - pos - marker.size();
            // A valid base64 payload is a positive multiple of 4. Anything
            // shorter made (b64len/4)*3 - padding UNDERFLOW (cgltf_size is
            // unsigned), producing a ~SIZE_MAX allocation/read request from a
            // one-character URI. Reject non-conforming lengths outright.
            if (b64len < 4 || (b64len % 4) != 0) { return {}; }
            cgltf_size padding = 0;
            if (b64[b64len - 1] == '=') { ++padding; }
            if (b64[b64len - 2] == '=') { ++padding; }
            const cgltf_size decoded = (b64len / 4) * 3 - padding;
            void *out = nullptr;
            if (cgltf_load_buffer_base64(&options, decoded, b64, &out) == cgltf_result_success && out != nullptr) {
                const auto *bytes = static_cast<const unsigned char *>(out);
                std::vector<unsigned char> result(bytes, bytes + decoded);
                free(out);// cgltf's default allocator is malloc/free
                return result;
            }
            return {};
        }

        // Not a data URI. A scheme prefix (data:/http:/https:) names a form
        // this loader does not fetch - a remote asset is out of scope, and a
        // malformed data: URI (no "base64," marker) must not fall through to
        // being read as a relative filesystem path.
        if (uri.rfind("data:", 0) == 0 || uri.rfind("http:", 0) == 0 || uri.rfind("https:", 0) == 0) { return {}; }

        std::string decodedUri = percentDecodeUri(uri);
        std::replace(decodedUri.begin(), decodedUri.end(), '\\', '/');

        const std::filesystem::path baseDir = documentDir.empty() ? std::filesystem::path(".") : documentDir;
        const std::filesystem::path candidate = baseDir / decodedUri;

        std::error_code baseEc;
        const std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(baseDir, baseEc);
        std::error_code candidateEc;
        const std::filesystem::path canonicalCandidate = std::filesystem::weakly_canonical(candidate, candidateEc);
        if (baseEc || candidateEc) { return {}; }

        if (!isWithinDirectory(canonicalCandidate, canonicalBase)) {
            spdlog::warn(
              "GltfLoader: external image URI '{}' resolves outside the document directory ('{}'); rejected",
              uri,
              canonicalCandidate.string());
            return {};
        }

        const std::vector<char> fileBytes = Kataglyphis::Shared::readBinaryFile(canonicalCandidate.string());
        if (fileBytes.empty()) {
            spdlog::warn(
              "GltfLoader: external image URI '{}' resolved to '{}' but the file could not be read",
              uri,
              canonicalCandidate.string());
            return {};
        }
        return std::vector<unsigned char>(fileBytes.begin(), fileBytes.end());
    }
    return {};
}

/// Maps a glTF sampler's wrap/filter settings onto GltfSamplerDesc, mirroring
/// asset/gltf_loader.rs's `to_cpu_sampler` (the mapping the WebGPU renderer
/// uses for the same document). A null `sampler` (the texture named none) or
/// an unset (`_undefined`) filter yields GltfSamplerDesc's own defaults -
/// repeat + linear + linear, byte-identical to this loader's
/// pre-sampler-support behaviour.
GltfSamplerDesc gltfSamplerDesc(const cgltf_sampler *sampler)
{
    GltfSamplerDesc desc{};
    if (sampler == nullptr) { return desc; }

    const auto wrap = [](cgltf_wrap_mode mode) {
        switch (mode) {
        case cgltf_wrap_mode_clamp_to_edge:
            return vk::SamplerAddressMode::eClampToEdge;
        case cgltf_wrap_mode_mirrored_repeat:
            return vk::SamplerAddressMode::eMirroredRepeat;
        case cgltf_wrap_mode_repeat:
        default:
            return vk::SamplerAddressMode::eRepeat;
        }
    };
    desc.addressModeU = wrap(sampler->wrap_s);
    desc.addressModeV = wrap(sampler->wrap_t);

    desc.magFilter = sampler->mag_filter == cgltf_filter_type_nearest ? vk::Filter::eNearest : vk::Filter::eLinear;

    // The mipmap half of minFilter selects mipmapMode, the magnification half
    // selects minFilter - the same split to_cpu_sampler makes via
    // (min_nearest, mip_nearest). A bare Nearest/Linear (no mipmap qualifier)
    // is treated the same as its *_mipmap_nearest/*_mipmap_linear sibling,
    // matching the Rust loader exactly.
    switch (sampler->min_filter) {
    case cgltf_filter_type_nearest:
    case cgltf_filter_type_nearest_mipmap_nearest:
        desc.minFilter = vk::Filter::eNearest;
        desc.mipmapMode = vk::SamplerMipmapMode::eNearest;
        break;
    case cgltf_filter_type_nearest_mipmap_linear:
        desc.minFilter = vk::Filter::eNearest;
        desc.mipmapMode = vk::SamplerMipmapMode::eLinear;
        break;
    case cgltf_filter_type_linear_mipmap_nearest:
        desc.minFilter = vk::Filter::eLinear;
        desc.mipmapMode = vk::SamplerMipmapMode::eNearest;
        break;
    case cgltf_filter_type_linear:
    case cgltf_filter_type_linear_mipmap_linear:
    case cgltf_filter_type_undefined:
    default:
        desc.minFilter = vk::Filter::eLinear;
        desc.mipmapMode = vk::SamplerMipmapMode::eLinear;
        break;
    }

    return desc;
}

}// namespace

void GltfLoader::processPrimitive(const cgltf_primitive *primitive,
  const glm::mat4 &world,
  const glm::mat3 &normalMatrix,
  const cgltf_data *data,
  unsigned int fallbackMaterial,
  bool mirrored)
{
    // Triangles, triangle strips and triangle fans are all supported
    // (strips/fans are triangulated into a list below). Points and
    // lines are not drawable in this triangle-only renderer, so they
    // are skipped - but a strip/fan used to be skipped too, silently
    // dropping whole meshes exported that way.
    const cgltf_primitive_type primType = primitive->type;
    if (primType != cgltf_primitive_type_triangles && primType != cgltf_primitive_type_triangle_strip
        && primType != cgltf_primitive_type_triangle_fan) {
        return;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec4> colors;

    for (cgltf_size a = 0; a < primitive->attributes_count; ++a) {
        const cgltf_attribute *attribute = &primitive->attributes[a];
        switch (attribute->type) {
        case cgltf_attribute_type_position:
            readAttribute<3>(attribute->data, positions);
            break;
        case cgltf_attribute_type_normal:
            readAttribute<3>(attribute->data, normals);
            break;
        case cgltf_attribute_type_texcoord:
            if (attribute->index == 0) { readAttribute<2>(attribute->data, uvs); }
            break;
        case cgltf_attribute_type_color:
            // COLOR_0 multiplies the base colour, alpha included (glTF spec).
            // The file may store it as vec3 or vec4; readAttribute<4> pre-fills
            // alpha to 1.0 for the vec3 case (see its comment). Absent -> white
            // below, so the shader multiply is a no-op for the common
            // uncoloured mesh.
            if (attribute->index == 0) { readAttribute<4>(attribute->data, colors); }
            break;
        default:
            break;
        }
    }
    if (positions.empty()) { return; }

    const auto base = static_cast<unsigned int>(vertices.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const glm::vec3 worldPos = glm::vec3(world * glm::vec4(positions[i], 1.0F));
        // A missing NORMAL used to become a constant (0,1,0), so a
        // normal-less glTF lit as if every face pointed up. The spec
        // requires computing flat normals; done below once this
        // primitive's index list is known. Placeholder for now.
        const glm::vec3 worldNormal =
          i < normals.size() ? glm::normalize(normalMatrix * normals[i]) : glm::vec3(0.0F, 1.0F, 0.0F);
        const glm::vec2 uv = i < uvs.size() ? uvs[i] : glm::vec2(0.0F);
        const glm::vec4 vcolor = i < colors.size() ? colors[i] : glm::vec4(1.0F);
        vertices.emplace_back(worldPos, worldNormal, vcolor, uv);
    }

    // Gather the primitive's local index sequence (from the accessor,
    // or the implicit 0..N-1 for a non-indexed primitive), then emit a
    // triangle LIST into the global buffer - triangulating strips and
    // fans as we go.
    std::vector<unsigned int> localSeq;
    if (primitive->indices != nullptr) {
        const cgltf_accessor *idx = primitive->indices;
        localSeq.reserve(idx->count);
        for (cgltf_size i = 0; i < idx->count; ++i) {
            localSeq.push_back(static_cast<unsigned int>(cgltf_accessor_read_index(idx, i)));
        }
    } else {
        localSeq.reserve(positions.size());
        for (std::size_t i = 0; i < positions.size(); ++i) {
            localSeq.push_back(static_cast<unsigned int>(i));
        }
    }

    const std::size_t primIndexStart = indices.size();
    const auto vertexCount = positions.size();
    std::size_t droppedTriangles = 0;
    // Malformed files can carry indices that don't address any vertex the
    // primitive actually shipped (fuzz-found hazard, mirrors ObjLoader.cpp's
    // face_valid guard): validate every corner before emitting the triangle.
    // computeFlatNormals below and the BLAS build both index vertices
    // unchecked, so an out-of-range corner reaching either is an
    // out-of-bounds write / device fault rather than a diagnosable error.
    //
    // glTF 2.0 spec (3.7.4. Transformations): "the determinant of the
    // node's global transform... indicates the winding order of the mesh
    // triangles - if positive, the winding order triangle is unchanged; if
    // negative, the winding order must be reversed". `mirrored` carries that
    // sign so a mirrored node's triangles still front-face towards the
    // viewer. MeshDrawRecorder.cpp sets eBack culling for every non-double-
    // sided mesh, so an unreversed mirror is back-face-culled into
    // invisibility; computeFlatNormals below derives its normal from this
    // same corner order, so it must run (and does) after this reversal.
    const auto emitTri = [&](unsigned int a, unsigned int b, unsigned int c) {
        if (a >= vertexCount || b >= vertexCount || c >= vertexCount) {
            ++droppedTriangles;
            return;
        }
        if (mirrored) {
            indices.push_back(base + a);
            indices.push_back(base + c);
            indices.push_back(base + b);
        } else {
            indices.push_back(base + a);
            indices.push_back(base + b);
            indices.push_back(base + c);
        }
    };
    if (primType == cgltf_primitive_type_triangle_strip) {
        // Strip: alternate winding each step to keep a consistent
        // front face (glTF/OpenGL convention).
        for (std::size_t i = 0; i + 2 < localSeq.size(); ++i) {
            if ((i & 1U) == 0U) {
                emitTri(localSeq[i], localSeq[i + 1], localSeq[i + 2]);
            } else {
                emitTri(localSeq[i + 1], localSeq[i], localSeq[i + 2]);
            }
        }
    } else if (primType == cgltf_primitive_type_triangle_fan) {
        // Fan: vertex 0 is shared by every triangle.
        for (std::size_t i = 1; i + 1 < localSeq.size(); ++i) {
            emitTri(localSeq[0], localSeq[i], localSeq[i + 1]);
        }
    } else {
        for (std::size_t i = 0; i + 2 < localSeq.size(); i += 3) {
            emitTri(localSeq[i], localSeq[i + 1], localSeq[i + 2]);
        }
    }

    if (droppedTriangles > 0) {
        spdlog::warn("GltfLoader: dropped {} out-of-range triangle(s) in a primitive with {} vertices",
          droppedTriangles,
          vertexCount);
    }

    // Flat normals when NORMAL is absent (glTF spec: implementations
    // MUST compute them). Per-triangle geometric normal from the
    // already-world-space vertex positions, assigned to each of the
    // triangle's vertices - the same flat approximation the OBJ path
    // uses (kataglyphis.vulkan.vertex's computeFlatNormals, the shared
    // copy of this loop). Only runs for primitives that shipped no normals.
    if (normals.empty()) { vertex::computeFlatNormals(vertices, indices, primIndexStart); }

    // One material id per triangle of this primitive (materialIndex is
    // per-face, like the OBJ path). All of a primitive's triangles share
    // its material.
    const unsigned int primitiveMaterial =
      primitive->material != nullptr
        ? static_cast<unsigned int>(primitive->material - data->materials)
        : fallbackMaterial;
    // One id per EMITTED triangle - after triangulation, not from the
    // raw index count (which for a strip/fan over-counts by ~3x).
    const std::size_t triStart = materialIndex.size();
    const std::size_t emittedTriangles = (indices.size() - primIndexStart) / 3;
    materialIndex.insert(materialIndex.end(), emittedTriangles, primitiveMaterial);

    // This primitive's slice of the flat arrays, so uploadParsed can build
    // it as its own Mesh (backlog #10). The union of all ranges is exactly
    // the flat arrays, so a single-primitive glTF yields one range and is
    // behaviour-identical.
    const bool doubleSided = primitive->material != nullptr && primitive->material->double_sided != 0;
    meshRanges.push_back(MeshRange{ static_cast<std::size_t>(base),
      positions.size(),
      primIndexStart,
      indices.size() - primIndexStart,
      triStart,
      emittedTriangles,
      doubleSided });
}

void GltfLoader::visitNode(const cgltf_node *node, const cgltf_data *data, unsigned int fallbackMaterial)
{
    if (node->mesh != nullptr) {
        // glTF 2.0 spec (Skins): the transform of a skinned mesh node MUST be
        // ignored - only joint transforms position a skinned mesh. The engine
        // has no joint animation, so skinned vertices stay in bind pose.
        glm::mat4 world(1.0F);
        if (node->skin == nullptr) {
            cgltf_float worldRaw[16];
            cgltf_node_transform_world(node, worldRaw);
            world = glm::make_mat4(worldRaw);
        }
        const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(world));
        // A skinned node keeps world == identity above, so its determinant is
        // +1 and it is never mirrored - correct, bind-pose vertices are
        // unmirrored regardless of the joint transforms that will move them.
        const bool mirrored = glm::determinant(glm::mat3(world)) < 0.0F;

        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            processPrimitive(&node->mesh->primitives[p], world, normalMatrix, data, fallbackMaterial, mirrored);
        }
    }

    for (cgltf_size c = 0; c < node->children_count; ++c) {
        visitNode(node->children[c], data, fallbackMaterial);
    }
}

bool GltfLoader::parseCpu(const std::string &modelFile)
{
    // clear prior state if called multiple times on the same instance
    vertices.clear();
    indices.clear();
    materials.clear();
    materialIndex.clear();
    textureImages.clear();
    textureSamplerDescs.clear();
    meshRanges.clear();

    cgltf_options options{};
    cgltf_data *data = nullptr;
    if (cgltf_parse_file(&options, modelFile.c_str(), &data) != cgltf_result_success) { return false; }
    // Structural validation before we walk the document: cgltf_parse checks
    // JSON well-formedness, cgltf_validate checks that counts, indices and
    // references are internally consistent. The GUI feeds arbitrary user
    // files here, and the walk below trusts these invariants.
    if (cgltf_validate(data) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }
    if (cgltf_load_buffers(&options, data, modelFile.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }

    // One ObjMaterial per glTF material, plus a trailing neutral used by any
    // primitive that references none. `materialIndex` (per triangle) points into
    // this table.
    //
    // Materials that share the same glTF (image, sampler) pair share one
    // textureImages slot: each is decoded/uploaded once no matter how many
    // materials point at it, which matters because textureID indexes into the
    // engine's fixed MAX_TEXTURE_COUNT descriptor budget. The key is the PAIR,
    // not just the image - two textures may share one image with different
    // samplers, and collapsing them onto one slot would make the sampler
    // unobservable.
    const std::filesystem::path documentDir = std::filesystem::path(modelFile).parent_path();

    std::map<std::pair<const cgltf_image *, const cgltf_sampler *>, int> imageSlot;
    for (cgltf_size m = 0; m < data->materials_count; ++m) {
        const cgltf_material &material = data->materials[m];
        ObjMaterial objMaterial = fromGltfMaterial(material);
        // A base-colour texture (per material, like the OBJ path): record its
        // encoded bytes and point the material at the new texture slot.
        if (material.has_pbr_metallic_roughness != 0
            && material.pbr_metallic_roughness.base_color_texture.texture != nullptr) {
            const cgltf_texture *tex = material.pbr_metallic_roughness.base_color_texture.texture;
            const cgltf_image *img = tex->image;
            const auto key = std::make_pair(img, tex->sampler);
            const auto existing = imageSlot.find(key);
            if (existing != imageSlot.end()) {
                objMaterial.textureID = existing->second;
            } else {
                std::vector<unsigned char> bytes = extractImageBytes(img, options, documentDir);
                if (!bytes.empty()) {
                    objMaterial.textureID = static_cast<int>(textureImages.size());
                    imageSlot[key] = objMaterial.textureID;
                    textureImages.push_back(std::move(bytes));
                    textureSamplerDescs.push_back(gltfSamplerDesc(tex->sampler));
                }
            }
        }
        materials.push_back(objMaterial);
    }
    const auto fallbackMaterial = static_cast<unsigned int>(materials.size());
    materials.push_back(neutralMaterial());

    // Walk the default scene only (glTF spec: a document may name one via
    // `scene`; otherwise the first entry of `scenes` is the convention every
    // viewer follows), matching asset/gltf_loader.rs's
    // `default_scene().or_else(|| scenes().next())` - the two walks must
    // change together. Each mesh instance carries its world transform (glTF
    // has nodes the OBJ path lacks; bake the transform into positions like
    // the Rust loader does). A mesh referenced by several nodes is emitted
    // once per node, matching glTF instancing semantics.
    const cgltf_scene *scene =
      data->scene != nullptr ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (scene != nullptr) {
        for (cgltf_size n = 0; n < scene->nodes_count; ++n) { visitNode(scene->nodes[n], data, fallbackMaterial); }
    } else {
        // No `scenes` array at all - cgltf_validate permits this. Fall back to
        // the old flat walk (every node processed once, no recursion into
        // children - they are already in data->nodes) so such a file still
        // loads, but warn since this is not the spec's intended path and can
        // pull in nodes no scene ever referenced.
        spdlog::warn("GltfLoader: {} has no scenes; loading every node in the document", modelFile);
        for (cgltf_size n = 0; n < data->nodes_count; ++n) {
            const cgltf_node *node = &data->nodes[n];
            if (node->mesh == nullptr) { continue; }

            glm::mat4 world(1.0F);
            if (node->skin == nullptr) {
                cgltf_float worldRaw[16];
                cgltf_node_transform_world(node, worldRaw);
                world = glm::make_mat4(worldRaw);
            }
            const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(world));
            const bool mirrored = glm::determinant(glm::mat3(world)) < 0.0F;

            for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
                processPrimitive(&node->mesh->primitives[p], world, normalMatrix, data, fallbackMaterial, mirrored);
            }
        }
    }

    cgltf_free(data);

    return !vertices.empty();
}

}// namespace Kataglyphis
