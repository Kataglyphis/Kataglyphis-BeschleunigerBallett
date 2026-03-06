#ifndef KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP
#define KATAGLYPHIS_SHARED_SCENE_OBJ_MATERIAL_HPP

#ifdef __cplusplus
#include <glm/glm.hpp>
#define KTG_VEC3 glm::vec3
#else
#define KTG_VEC3 vec3
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

#ifdef __cplusplus

    ObjMaterial()
      : ambient(0.1F, 0.1F, 0.1F), diffuse(0.7F, 0.7F, 0.7F), specular(1.0F, 1.0F, 1.0F),
        transmittance(0.0F, 0.0F, 0.0F), emission(0.0F, 0.0F, 0.10F), shininess(0.0F), ior(1.0F), dissolve(1.0F),
        illum(0), textureID(-1)
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
      int textureID)
      : ambient(ambient), diffuse(diffuse), specular(specular), transmittance(transmittance), emission(emission),
        shininess(shininess), ior(ior), dissolve(dissolve), illum(illum), textureID(textureID)
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
#endif
};

#undef KTG_VEC3

#endif
