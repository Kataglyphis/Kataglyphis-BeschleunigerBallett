module;

#include "spdlog/spdlog.h"

#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

module kataglyphis.vulkan.file;

Kataglyphis::File::File(const std::string &file_location) : file_location(file_location) {}

auto Kataglyphis::File::read() -> std::string
{
    std::string content;
    std::ifstream file_stream(file_location, std::ios::in);

    if (!file_stream.is_open()) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, std::string("Failed to read ") + file_location + ". File does not exist.");
        return "";
    }

    std::string line;
    while (!file_stream.eof()) {
        std::getline(file_stream, line);
        content.append(line + "\n");
    }

    file_stream.close();
    return content;
}

auto Kataglyphis::File::readCharSequence() -> std::vector<char>
{
    std::ifstream file(file_location, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, std::string("Failed to open a file on location: ") + file_location + "!");
    }

    size_t const file_size = static_cast<size_t>(file.tellg());
    std::vector<char> file_buffer(file_size);

    file.seekg(0);
    file.read(file_buffer.data(), static_cast<std::streamsize>(file_size));

    file.close();

    return file_buffer;
}

auto Kataglyphis::File::getBaseDir() -> std::string
{
    if (file_location.find_last_of("/\\") != std::string::npos) {
        return file_location.substr(0, file_location.find_last_of("/\\"));
    }
    return "";
}

Kataglyphis::File::~File() = default;
