#include "scene/ObjMaterial.hpp"

ObjMaterial::ObjMaterial() {
  this->ambient = glm::vec3(0.1f, 0.1f, 0.1f);
  this->diffuse = glm::vec3(0.7f, 0.7f, 0.7f);
  this->specular = glm::vec3(1.0f, 1.0f, 1.0f);
  this->transmittance = glm::vec3(0.0f, 0.0f, 0.0f);
  this->emission = glm::vec3(0.0f, 0.0f, 0.10);
  this->shininess = 0.f;
  this->ior = 1.0f;
  this->dissolve = 1.f;
  this->illum = 0;
  this->textureID = -1;
}

ObjMaterial::ObjMaterial(glm::vec3 ambient, glm::vec3 diffuse,
                         glm::vec3 specular, glm::vec3 transmittance,
                         glm::vec3 emission, float shininess, float ior,
                         float dissolve, int illum, int textureID) {
  this->ambient = ambient;
  this->diffuse = diffuse;
  this->specular = specular;
  this->transmittance = transmittance;
  this->emission = emission;
  this->shininess = shininess;
  this->ior = ior;
  this->dissolve = dissolve;
  this->illum = illum;
  this->textureID = textureID;
}

ObjMaterial::~ObjMaterial() {}
