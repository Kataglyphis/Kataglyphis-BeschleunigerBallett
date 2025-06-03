#pragma once

#include "Model.hpp"
#include "Rotation.hpp"

class GameObject
{
  public:
    GameObject();

    GameObject(const std::string &model_path, glm::vec3 translation, GLfloat scale, Rotation rot);

    void init(const std::string &model_path, glm::vec3 translation, GLfloat scale, Rotation rot);

    glm::mat4 get_world_trafo();
    glm::mat4 get_normal_world_trafo();

    std::shared_ptr<AABB> get_aabb();
    std::shared_ptr<Model> get_model();

    void translate(glm::vec3 translate);
    void scale(GLfloat scale_factor);
    void rotate(Rotation rot);

    void render();

    ~GameObject();

  private:
    std::shared_ptr<Model> model;

    GLfloat scale_factor;
    Rotation rot;
    glm::vec3 translation;
};
