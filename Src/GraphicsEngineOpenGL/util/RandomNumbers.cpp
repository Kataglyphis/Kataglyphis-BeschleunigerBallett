#include "util/RandomNumbers.hpp"

#include "hostDevice/bindings.hpp"
#include "hostDevice/host_device_shared.hpp"

#include "hostDevice/GlobalValues.hpp"
#include <random>

RandomNumbers::RandomNumbers()
{
    generate_random_numbers();

    glGenTextures(1, &random_number_id);
    glBindTexture(GL_TEXTURE_2D, random_number_id);
    // i think we won't need nearest option; so stick to linear
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // assuming full HD will be maximum resolution
    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA32F, MAX_RESOLUTION_X, MAX_RESOLUTION_Y, 0, GL_RGBA, GL_FLOAT, random_number_data.get());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void RandomNumbers::read()
{
    glActiveTexture(GL_TEXTURE0 + (GLenum)RANDOM_NUMBERS_SLOT);
    glBindTexture(GL_TEXTURE_2D, random_number_id);
}

void RandomNumbers::generate_random_numbers()
{
    random_number_data = std::shared_ptr<GLfloat[]>(new GLfloat[MAX_RESOLUTION_X * MAX_RESOLUTION_Y * 4]);
    std::mt19937_64 gen64(25121995);
    std::uniform_real_distribution<float> dis(0, 1);

    for (int i = 0; i < MAX_RESOLUTION_X; i++) {
        for (int k = 0; k < MAX_RESOLUTION_Y; k++) {
            const GLfloat random_offset[4] = { dis(gen64), dis(gen64), dis(gen64), dis(gen64) };

            GLuint index = (MAX_RESOLUTION_Y * i + k) * 4;

            *(random_number_data.get() + index) = random_offset[0];
            *(random_number_data.get() + index + 1) = random_offset[1];
            *(random_number_data.get() + index + 2) = random_offset[2];
            *(random_number_data.get() + index + 3) = random_offset[3];
        }
    }
}

RandomNumbers::~RandomNumbers() { glDeleteTextures(1, &random_number_id); }
