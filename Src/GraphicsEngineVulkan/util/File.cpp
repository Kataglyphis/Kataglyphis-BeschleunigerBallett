module;

#include "../../shared/util/FileLocationHolder.hpp"

#include "spdlog/spdlog.h"

#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

module kataglyphis.vulkan.file;

import kataglyphis.shared.util.file_reader;

Kataglyphis::File::File(const std::string &file_location) : Kataglyphis::Shared::FileLocationHolder(file_location) {}

auto Kataglyphis::File::read() -> std::string
{
    if (!Kataglyphis::Shared::fileExists(get_file_location())) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, std::string("Failed to read ") + get_file_location() + ". File does not exist.");
        return "";
    }

    std::string const content = Kataglyphis::Shared::readTextFile(get_file_location());
    return content;
}

auto Kataglyphis::File::readCharSequence() -> std::vector<char>
{
    if (!Kataglyphis::Shared::fileExists(get_file_location())) {
        spdlog::default_logger_raw()->log(
          spdlog::level::err, std::string("Failed to open a file on location: ") + get_file_location() + "!");
        return {};
    }

    std::vector<char> const file_buffer = Kataglyphis::Shared::readBinaryFile(get_file_location());

    return file_buffer;
}

auto Kataglyphis::File::getBaseDir() -> std::string { return Kataglyphis::Shared::getBaseDir(get_file_location()); }

Kataglyphis::File::~File() = default;
