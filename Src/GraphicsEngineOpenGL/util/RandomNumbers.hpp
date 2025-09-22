#pragma once
#include <glad/glad.h>
// #include <GLFW/glfw3.h>

#include <memory>

class RandomNumbers
{
  public:
    RandomNumbers();

    void read();

    ~RandomNumbers();

  private:
    GLuint random_number_id;
    std::shared_ptr<GLfloat[]> random_number_data;

    void generate_random_numbers();
};
