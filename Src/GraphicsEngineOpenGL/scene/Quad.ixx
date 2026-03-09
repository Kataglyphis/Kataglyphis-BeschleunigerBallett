module;

#include <glad/glad.h>

export module kataglyphis.opengl.quad;

export class Quad
{
  public:
    Quad();

    void render() const;

    ~Quad();

  private:
    GLuint q_vao{}, q_vbo{};

    float vertices[20] = {

        // positions		           //tex coords
        -1.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        -1.0f,
        -1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        0.0f,
        1.0f,
        0.0f

    };
};
