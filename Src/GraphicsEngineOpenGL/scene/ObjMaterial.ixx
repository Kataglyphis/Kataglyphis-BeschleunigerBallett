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

		ObjMaterial(glm::vec3 ambient,
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

		glm::vec3 ambient = glm::vec3(0.1f, 0.1f, 0.1f);
		glm::vec3 diffuse = glm::vec3(0.7f, 0.7f, 0.7f);
		glm::vec3 specular = glm::vec3(1.0f, 1.0f, 1.0f);
		glm::vec3 transmittance = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::vec3 emission = glm::vec3(0.0f, 0.0f, 0.10);
		float shininess = 0.f;
		float ior = 1.0f;// index of refraction
		float dissolve = 1.f;// 1 == opaque; 0 == fully transparent

		int illum = 0;
		int textureID = -1;

		~ObjMaterial();

	private:
};
