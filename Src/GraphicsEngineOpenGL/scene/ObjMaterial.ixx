module;

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

export module kataglyphis.opengl.obj_material;

export class ObjMaterial
{
  public:
    ObjMaterial() : shininess(0.f), ior(1.0f), dissolve(1.f), illum(0), textureID(-1)
    {
        this->ambient = glm::vec3(0.1F, 0.1F, 0.1F);
        this->diffuse = glm::vec3(0.7F, 0.7F, 0.7F);
        this->specular = glm::vec3(1.0F, 1.0F, 1.0F);
        this->transmittance = glm::vec3(0.0F, 0.0F, 0.0F);
        this->emission = glm::vec3(0.0F, 0.0F, 0.10F);
    }

    ObjMaterial(glm::vec3 p_ambient,
      glm::vec3 p_diffuse,
      glm::vec3 p_specular,
      glm::vec3 p_transmittance,
      glm::vec3 p_emission,
      float p_shininess,
      float p_ior,
      float p_dissolve,
      int p_illum,
      int p_textureID)
      : ambient(p_ambient), diffuse(p_diffuse), specular(p_specular), transmittance(p_transmittance),
        emission(p_emission), shininess(p_shininess), ior(p_ior), dissolve(p_dissolve), illum(p_illum),
        textureID(p_textureID)
    {}

    glm::vec3 get_ambient() const { return ambient; };
    glm::vec3 get_diffuse() const { return diffuse; };
    glm::vec3 get_specular() const { return specular; };
    glm::vec3 get_transmittance() const { return transmittance; };
    glm::vec3 get_emission() const { return emission; };

    float get_shininess() const { return shininess; };
    float get_ior() const { return ior; };
    float get_dissolve() const { return dissolve; };

    int get_illum() const { return illum; };
    int get_textureID() const { return textureID; };

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

    ~ObjMaterial();

  private:
};
