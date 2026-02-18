#include "scene/ObjMaterial.hpp"

ObjMaterial::ObjMaterial() : shininess(0.f), ior(1.0f), dissolve(1.f), illum(0), textureID(-1)
{
    this->ambient = glm::vec3(0.1F, 0.1F, 0.1F);
    this->diffuse = glm::vec3(0.7F, 0.7F, 0.7F);
    this->specular = glm::vec3(1.0F, 1.0F, 1.0F);
    this->transmittance = glm::vec3(0.0F, 0.0F, 0.0F);
    this->emission = glm::vec3(0.0F, 0.0F, 0.10);
}

ObjMaterial::ObjMaterial(glm::vec3 ambient,
  glm::vec3 diffuse,
  glm::vec3 specular,
  glm::vec3 transmittance,
  glm::vec3 emission,
  float shininess,
  float ior,
  float dissolve,
  int illum,
  int textureID)
  : ambient(ambient), diffuse(diffuse), specular(specular), transmittance(transmittance), emission(emission),
    shininess(shininess), ior(ior), dissolve(dissolve), illum(illum), textureID(textureID)
{}

ObjMaterial::~ObjMaterial() = default;
