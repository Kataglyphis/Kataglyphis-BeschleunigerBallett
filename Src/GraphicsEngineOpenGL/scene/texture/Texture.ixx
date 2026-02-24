module;

#include <glad/glad.h>
#include <stb_image.h>
#include <string.h>

#include <memory>
#include <string>

#include "hostDevice/GlobalValues.hpp"

export module kataglyphis.opengl.texture;

import kataglyphis.opengl.texture_wrapping_mode;
import kataglyphis.opengl.repeat_mode;
import kataglyphis.opengl.clamp_to_edge_mode;

export class Texture
{
  public:
    Texture();
    Texture(const char *file_loc, std::shared_ptr<TextureWrappingMode> wrapping_mode);

    bool load_texture_without_alpha_channel();
    bool load_texture_with_alpha_channel();

    bool load_SRGB_texture_without_alpha_channel();
    bool load_SRGB_texture_with_alpha_channel();

    std::string get_filename() const;
    GLuint get_id() const;

    void use_texture(unsigned int index) const;
    static void unbind_texture(unsigned int index);

    void clear_texture_context();

    ~Texture();

  private:
    GLuint textureID;
    int width, height, bit_depth;

    std::shared_ptr<TextureWrappingMode> wrapping_mode;

    std::string file_location;
};
