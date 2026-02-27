module;

#include "../../shared/util/FileLocationHolder.hpp"

#include <fstream>
#include <iostream>
#include <string>

module kataglyphis.opengl.file;

import kataglyphis.shared.util.file_reader;

File::File(const std::string &file_location) : Kataglyphis::Shared::FileLocationHolder(file_location) {}

auto File::read() -> std::string
{
    if (!Kataglyphis::Shared::fileExists(get_file_location())) {
        std::cerr << "Failed to read " << get_file_location() << ". File does not exist." << '\n';
        return "";
    }

    std::string const content = Kataglyphis::Shared::readTextFile(get_file_location());
    return content;
}

File::~File() = default;
