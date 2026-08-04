#ifndef KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP
#define KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP

#include <glm/glm.hpp>

struct ObjMaterial
{
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    glm::vec3 transmittance;
    glm::vec3 emission;
    float shininess;
    float ior;
    float dissolve;

    int illum;
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

    ObjMaterial()
      : ambient(0.1F, 0.1F, 0.1F), diffuse(0.7F, 0.7F, 0.7F), specular(1.0F, 1.0F, 1.0F),
        transmittance(0.0F, 0.0F, 0.0F), emission(0.0F, 0.0F, 0.10F), shininess(0.0F), ior(1.0F), dissolve(1.0F),
        illum(0), textureID(-1), alphaCutoff(-1.0F), uv_transform_row0(1.0F, 0.0F, 0.0F),
        uv_transform_row1(0.0F, 1.0F, 0.0F)
    {}

    ObjMaterial(glm::vec3 ambient,
      glm::vec3 diffuse,
      glm::vec3 specular,
      glm::vec3 transmittance,
      glm::vec3 emission,
      float shininess,
      float ior,
      float dissolve,
      int illum,
      int textureID,
      float alphaCutoff = -1.0F,
      glm::vec3 uv_transform_row0 = glm::vec3(1.0F, 0.0F, 0.0F),
      glm::vec3 uv_transform_row1 = glm::vec3(0.0F, 1.0F, 0.0F))
      : ambient(ambient), diffuse(diffuse), specular(specular), transmittance(transmittance), emission(emission),
        shininess(shininess), ior(ior), dissolve(dissolve), illum(illum), textureID(textureID), alphaCutoff(alphaCutoff),
        uv_transform_row0(uv_transform_row0), uv_transform_row1(uv_transform_row1)
    {}

    int get_textureID() const { return textureID; }
};

#endif
