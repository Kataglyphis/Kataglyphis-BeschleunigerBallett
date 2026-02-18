#include <memory>
#include <string>
#include "scene/texture/TextureWrappingMode.hpp"
#include <cstdio>
#include <glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION

#include "scene/texture/Texture.hpp"

#include "scene/texture/RepeatMode.hpp"

#include <utility>
#include <utility>
#include <print>

Texture::Texture()
  :

    textureID(0), width(0), height(0), bit_depth(0),
    // go with reapeat as standard ...
    wrapping_mode(std::make_shared<RepeatMode>()), file_location(std::string(""))

{}

Texture::Texture(const char *file_loc, std::shared_ptr<TextureWrappingMode> wrapping_mode)
  :

    textureID(0), width(0), height(0), bit_depth(0),
    // go with reapeat as standard ...
    wrapping_mode(std::move(std::move(wrapping_mode))), file_location(std::string(file_loc))

{}

auto Texture::load_texture_without_alpha_channel() -> bool
{
    stbi_set_flip_vertically_on_load(1);
    unsigned char *texture_data = stbi_load(file_location.c_str(), &width, &height, &bit_depth, 0);
    if (texture_data == nullptr) {
        std::println("Failed to find: {}", file_location);
        return false;
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    wrapping_mode->activate();

    // i think we won't need nearest option; so stick to linear
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //  INVALID ENUM changed to GL_LINEAR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, texture_data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(texture_data);

    return true;
}

auto Texture::load_texture_with_alpha_channel() -> bool
{
    stbi_set_flip_vertically_on_load(1);
    unsigned char *texture_data = stbi_load(file_location.c_str(), &width, &height, &bit_depth, 0);
    if (texture_data == nullptr) {
        std::println("Failed to find: {}", file_location);
        return false;
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    wrapping_mode->activate();

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
    glGenerateMipmap(GL_TEXTURE_2D);

    // to not interpolate between transparent and not trans coloars
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
    // GL_LINEAR_MIPMAP_LINEAR);
    //  INVALID ENUM changed to GL_LINEAR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(texture_data);

    return true;
}

auto Texture::load_SRGB_texture_without_alpha_channel() -> bool
{
    stbi_set_flip_vertically_on_load(1);
    unsigned char *texture_data = stbi_load(file_location.c_str(), &width, &height, &bit_depth, 0);
    if (texture_data == nullptr) {
        std::println("Failed to find: {}", file_location);
        return false;
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    wrapping_mode->activate();

    // i think we won't need nearest option; so stick to linear
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //  INVALID ENUM changed to GL_LINEAR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, texture_data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(texture_data);

    return true;
}

auto Texture::load_SRGB_texture_with_alpha_channel() -> bool
{
    stbi_set_flip_vertically_on_load(1);
    unsigned char *texture_data = stbi_load(file_location.c_str(), &width, &height, &bit_depth, 0);
    if (texture_data == nullptr) {
        std::println("Failed to find: {}", file_location);
        return false;
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    wrapping_mode->activate();

    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB_ALPHA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
    glGenerateMipmap(GL_TEXTURE_2D);

    // to not interpolate between transparent and not trans coloars
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
    // GL_LINEAR_MIPMAP_LINEAR);
    //  INVALID ENUM changed to GL_LINEAR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(texture_data);

    return true;
}

auto Texture::get_filename() const -> std::string { return file_location; }

void Texture::use_texture(unsigned int index) const
{
    glActiveTexture(GL_TEXTURE0 + index);
    glBindTexture(GL_TEXTURE_2D, textureID);
}

void Texture::unbind_texture(unsigned int index) { glBindTexture(GL_TEXTURE_2D, GL_TEXTURE0 + index); }

void Texture::clear_texture_context()
{
    glDeleteTextures(1, &textureID);
    textureID = 0;
    width = 0;
    height = 0;
    bit_depth = 0;
    file_location = std::string("");
}

auto Texture::get_id() const -> GLuint { return textureID; }

Texture::~Texture() { clear_texture_context(); }
