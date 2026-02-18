#include "GameObject.hpp"
#include <memory>
#include "scene/Model.hpp"
#include <string>
#include <glm/ext/vector_float3.hpp>
#include <glad/glad.h>
#include "scene/Rotation.hpp"
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/matrix.hpp>
#include "scene/AABB.hpp"

GameObject::GameObject() : model(std::make_shared<Model>(Model())) {}

GameObject::GameObject(const std::string &model_path, glm::vec3 translation, GLfloat scale, Rotation rot)
    : model(std::make_shared<Model>()), scale_factor(scale), rot(rot), translation(translation)
{
    model->load_model_in_ram(model_path);
}

void GameObject::init(const std::string &model_path, glm::vec3 translation, GLfloat scale, Rotation rot)
{
    model = std::make_shared<Model>(Model());
    model->load_model_in_ram(model_path);
    this->translation = translation;
    this->scale_factor = scale;
    this->rot = rot;
}

auto GameObject::get_world_trafo() -> glm::mat4
{
    auto model_to_world = glm::mat4(1.0);
    model_to_world = glm::translate(model_to_world, translation);
    model_to_world = glm::scale(model_to_world, glm::vec3(scale_factor));
    model_to_world = glm::rotate(model_to_world, glm::radians(rot.degrees), rot.axis);

    return model_to_world;
}

auto GameObject::get_normal_world_trafo() -> glm::mat4
{
    glm::mat4 const world_trafo = get_world_trafo();
    return glm::transpose(glm::inverse(world_trafo));
}

void GameObject::render() { model->render(); }

auto GameObject::get_aabb() -> std::shared_ptr<AABB> { return model->get_aabb(); }

auto GameObject::get_model() -> std::shared_ptr<Model> { return model; }

void GameObject::translate(glm::vec3 translate) { this->translation = translate; }

void GameObject::rotate(Rotation rot) { this->rot = rot; }

void GameObject::scale(GLfloat scale_factor) { this->scale_factor = scale_factor; }

GameObject::~GameObject() = default;
