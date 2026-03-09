module;

#include <glad/glad.h>
#include <memory>

export module kataglyphis.opengl.random_numbers;

export class RandomNumbers
{
  public:
    RandomNumbers();

    void read() const;

    ~RandomNumbers();

  private:
    GLuint random_number_id{};
    std::shared_ptr<GLfloat[]> random_number_data;

    void generate_random_numbers();
};
