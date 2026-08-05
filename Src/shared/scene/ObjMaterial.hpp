#ifndef KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP
#define KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP

#include <glm/glm.hpp>
#include <type_traits>

struct ObjMaterial
{
    glm::vec3 diffuse{ 0.7F, 0.7F, 0.7F };
    // No authored Ke/emissive_factor means no emitted radiance: the shading
    // paths add material.emission unattenuated after shadowing
    // (rasterizer.slang:84-86), so any non-zero default is a scene-wide glow
    // nothing authored.
    glm::vec3 emission{ 0.0F };
    float shininess{ 0.0F };
    float dissolve{ 1.0F };

    int textureID{ -1 };

    // glTF alphaMode MASK: the base-colour alpha cutoff. A negative value means
    // "not a MASK material" (OPAQUE/BLEND) - the raster shaders discard a fragment
    // only when alphaCutoff >= 0 and the sampled base-colour alpha falls below it,
    // so OPAQUE materials (the default, and every OBJ material) never discard and
    // are bit-unchanged. Trailing float: scalar block layout keeps C++ and the
    // in-shader buffer-reference struct in sync without shifting the vec3 members.
    float alphaCutoff{ -1.0F };

    // glTF KHR_texture_transform for the base-colour texture: the shaders sample
    // at the UV transformed by the top two rows of the T*R*S 3x3 matrix (the
    // third row is always [0,0,1] and is omitted). Identity rows (1,0,0)/(0,1,0)
    // leave the UV unchanged, so materials without the extension (and every OBJ
    // material) are bit-identical. Trailing vec3s, same scalar-layout rationale
    // as alphaCutoff.
    glm::vec3 uv_transform_row0{ 1.0F, 0.0F, 0.0F };
    glm::vec3 uv_transform_row1{ 0.0F, 1.0F, 0.0F };

    // glTF pbrMetallicRoughness.metallicFactor [0,1]. Trailing scalar, same
    // scalar-layout rationale as alphaCutoff. Every OBJ material and every
    // glTF material without pbr_metallic_roughness defaults to 0.0
    // (dielectric), so pre-existing scenes are bit-unchanged.
    float metallic{ 0.0F };

    // glTF pbrMetallicRoughness.roughnessFactor [0,1]. A negative value means
    // "no authored roughness - derive it from `shininess`" (same sentinel
    // convention as alphaCutoff), which is what every OBJ material and every
    // pre-existing scene gets, so they stay bit-unchanged. Trailing scalar,
    // same scalar-layout rationale as alphaCutoff.
    float roughness{ -1.0F };

    // glTF emissiveTexture, dedup'd into the same textureImages/imageSlot
    // budget as textureID (see GltfLoader.cpp's fromGltfMaterial/parseCpu).
    // -1 = "no emissive texture" (same sentinel convention as textureID);
    // every OBJ material and every glTF material without an emissiveTexture
    // defaults to -1, so pre-existing scenes are bit-unchanged. Trailing
    // scalar, same scalar-layout rationale as alphaCutoff.
    int emissiveTextureID{ -1 };

    // glTF normalTexture, dedup'd into the same textureImages/imageSlot
    // budget as textureID/emissiveTextureID (see GltfLoader.cpp's
    // fromGltfMaterial/parseCpu). -1 = "no normal texture" (same sentinel
    // convention as textureID); every OBJ material and every glTF material
    // without a normalTexture defaults to -1, so pre-existing scenes are
    // bit-unchanged. Trailing scalar, same scalar-layout rationale as
    // alphaCutoff.
    int normalTextureID{ -1 };

    // glTF normalTexture.scale: scales the X and Y components of the sampled
    // tangent-space normal before it is renormalized (glTF 2.0 SS3.9.3).
    // Default 1.0 = unscaled, matching every OBJ material and every glTF
    // material without a normalTexture (cgltf leaves the field at its
    // zero-initialized default in that case, so the loader must guard on the
    // texture pointer rather than trusting it). Trailing scalar, same
    // scalar-layout rationale as alphaCutoff.
    float normalScale{ 1.0F };

    // glTF pbrMetallicRoughness.metallicRoughnessTexture, dedup'd into the same
    // textureImages/imageSlot budget as textureID/emissiveTextureID/normalTextureID
    // (see GltfLoader.cpp's fromGltfMaterial/parseCpu). Uploaded linear, like the
    // normal slot (glTF 2.0 SS3.9.2: G = roughness, B = metallic, not colour data).
    // -1 = "no metallic-roughness texture" (same sentinel convention as textureID);
    // every OBJ material and every glTF material without this texture defaults to
    // -1, so pre-existing scenes are bit-unchanged. Trailing scalar, same
    // scalar-layout rationale as alphaCutoff.
    int metallicRoughnessTextureID{ -1 };

    int get_textureID() const { return textureID; }
};

// Designated-initializer construction (GltfLoader.cpp, ObjLoader.cpp) and the
// offsetof-based layout gates (pushConstantSuite.cpp, buildIntegritySuite.cpp)
// both require ObjMaterial to stay an aggregate with standard layout - a
// future positional constructor or a base class would silently break both.
static_assert(std::is_aggregate_v<ObjMaterial>, "ObjMaterial must stay an aggregate for designated-initializer construction");
static_assert(std::is_standard_layout_v<ObjMaterial>, "ObjMaterial must stay standard-layout for the offsetof layout gates");

#endif
