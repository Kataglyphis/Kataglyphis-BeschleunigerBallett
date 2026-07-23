#ifndef KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP
#define KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP

#ifdef __cplusplus
#include <glm/glm.hpp>
#define KTG_VEC3 glm::vec3
#define KTG_VEC2 glm::vec2
#else
#define KTG_VEC3 vec3
#define KTG_VEC2 vec2
#endif

struct ObjMaterial
{
    KTG_VEC3 ambient;
    KTG_VEC3 diffuse;
    KTG_VEC3 specular;
    KTG_VEC3 transmittance;
    KTG_VEC3 emission;
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
    // at uv * uv_scale + uv_offset. Identity (scale 1, offset 0) leaves the UV
    // unchanged, so materials without the extension (and every OBJ material) are
    // bit-identical. Trailing vec2s, same scalar-layout rationale as alphaCutoff.
    KTG_VEC2 uv_scale;
    KTG_VEC2 uv_offset;

#ifdef __cplusplus

    ObjMaterial()
      : ambient(0.1F, 0.1F, 0.1F), diffuse(0.7F, 0.7F, 0.7F), specular(1.0F, 1.0F, 1.0F),
        transmittance(0.0F, 0.0F, 0.0F), emission(0.0F, 0.0F, 0.10F), shininess(0.0F), ior(1.0F), dissolve(1.0F),
        illum(0), textureID(-1), alphaCutoff(-1.0F), uv_scale(1.0F, 1.0F), uv_offset(0.0F, 0.0F)
    {}

    ObjMaterial(KTG_VEC3 ambient,
      KTG_VEC3 diffuse,
      KTG_VEC3 specular,
      KTG_VEC3 transmittance,
      KTG_VEC3 emission,
      float shininess,
      float ior,
      float dissolve,
      int illum,
      int textureID,
      float alphaCutoff = -1.0F,
      KTG_VEC2 uv_scale = KTG_VEC2(1.0F, 1.0F),
      KTG_VEC2 uv_offset = KTG_VEC2(0.0F, 0.0F))
      : ambient(ambient), diffuse(diffuse), specular(specular), transmittance(transmittance), emission(emission),
        shininess(shininess), ior(ior), dissolve(dissolve), illum(illum), textureID(textureID), alphaCutoff(alphaCutoff),
        uv_scale(uv_scale), uv_offset(uv_offset)
    {}

    KTG_VEC3 get_ambient() const { return ambient; }
    KTG_VEC3 get_diffuse() const { return diffuse; }
    KTG_VEC3 get_specular() const { return specular; }
    KTG_VEC3 get_transmittance() const { return transmittance; }
    KTG_VEC3 get_emission() const { return emission; }

    float get_shininess() const { return shininess; }
    float get_ior() const { return ior; }
    float get_dissolve() const { return dissolve; }

    int get_illum() const { return illum; }
    int get_textureID() const { return textureID; }
    float get_alphaCutoff() const { return alphaCutoff; }
    KTG_VEC2 get_uv_scale() const { return uv_scale; }
    KTG_VEC2 get_uv_offset() const { return uv_offset; }
#endif
};

#undef KTG_VEC3
#undef KTG_VEC2

#endif
