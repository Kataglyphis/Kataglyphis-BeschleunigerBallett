#ifndef KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP
#define KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP

#include <glm/glm.hpp>

struct ObjMaterial
{
    glm::vec3 diffuse;
    glm::vec3 emission;
    float shininess;
    float dissolve;

    int textureID;

    // glTF alphaMode MASK: the base-colour alpha cutoff. A negative value means
    // "not a MASK material" (OPAQUE/BLEND) - the raster shaders discard a fragment
    // only when alphaCutoff >= 0 and the sampled base-colour alpha falls below it,
    // so OPAQUE materials (the default, and every OBJ material) never discard and
    // are bit-unchanged. Trailing float: scalar block layout keeps C++ and the
    // in-shader buffer-reference struct in sync without shifting the vec3 members.
    float alphaCutoff;

    // glTF KHR_texture_transform for the base-colour texture: the shaders sample
    // at the UV transformed by the top two rows of the T*R*S 3x3 matrix (the
    // third row is always [0,0,1] and is omitted). Identity rows (1,0,0)/(0,1,0)
    // leave the UV unchanged, so materials without the extension (and every OBJ
    // material) are bit-identical. Trailing vec3s, same scalar-layout rationale
    // as alphaCutoff.
    glm::vec3 uv_transform_row0;
    glm::vec3 uv_transform_row1;

    // glTF pbrMetallicRoughness.metallicFactor [0,1]. Trailing scalar, same
    // scalar-layout rationale as alphaCutoff. Every OBJ material and every
    // glTF material without pbr_metallic_roughness defaults to 0.0
    // (dielectric), so pre-existing scenes are bit-unchanged.
    float metallic;

    // glTF pbrMetallicRoughness.roughnessFactor [0,1]. A negative value means
    // "no authored roughness - derive it from `shininess`" (same sentinel
    // convention as alphaCutoff), which is what every OBJ material and every
    // pre-existing scene gets, so they stay bit-unchanged. Trailing scalar,
    // same scalar-layout rationale as alphaCutoff.
    float roughness;

    // No authored Ke/emissive_factor means no emitted radiance: the shading
    // paths add material.emission unattenuated after shadowing
    // (rasterizer.slang:84-86), so any non-zero default is a scene-wide glow
    // nothing authored.
    ObjMaterial()
      : diffuse(0.7F, 0.7F, 0.7F), emission(0.0F), shininess(0.0F), dissolve(1.0F), textureID(-1),
        alphaCutoff(-1.0F), uv_transform_row0(1.0F, 0.0F, 0.0F), uv_transform_row1(0.0F, 1.0F, 0.0F), metallic(0.0F),
        roughness(-1.0F)
    {}

    ObjMaterial(glm::vec3 diffuse,
      glm::vec3 emission,
      float shininess,
      float dissolve,
      int textureID,
      float alphaCutoff = -1.0F,
      glm::vec3 uv_transform_row0 = glm::vec3(1.0F, 0.0F, 0.0F),
      glm::vec3 uv_transform_row1 = glm::vec3(0.0F, 1.0F, 0.0F),
      float metallic = 0.0F,
      float roughness = -1.0F)
      : diffuse(diffuse), emission(emission), shininess(shininess), dissolve(dissolve), textureID(textureID),
        alphaCutoff(alphaCutoff), uv_transform_row0(uv_transform_row0), uv_transform_row1(uv_transform_row1),
        metallic(metallic), roughness(roughness)
    {}

    int get_textureID() const { return textureID; }
};

#endif
