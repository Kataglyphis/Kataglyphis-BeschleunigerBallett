/** @defgroup FileUtilities File Utilities
 *  Functions and utilities for performing file operations.
 *  @{
 */

#include "util/File.hpp"
#include "spdlog/spdlog.h"

#include <fstream>
#include <iostream>

File::File(const std::string &file_location) { this->file_location = file_location; }

std::string File::read()
{
    std::string content;
    std::ifstream file_stream(file_location, std::ios::in);

    if (!file_stream.is_open()) {
        spdlog::error("Failed to read %s. File does not exist.", file_location.c_str());
        return "";
    }

    std::string line = "";
    while (!file_stream.eof()) {
        std::getline(file_stream, line);
        content.append(line + "\n");
    }

    file_stream.close();
    return content;
}

std::vector<char> File::readCharSequence()
{
    // open stream from given file
    // std::ios::binary tells stream to read file as binary
    // std::ios:ate tells stream to start reading from end of file
    std::ifstream file(file_location, std::ios::binary | std::ios::ate);

    // check if file stream sucessfully opened
    if (!file.is_open()) { 
        spdlog::error("Failed to open a file on location: {}!", file_location); 
    }

    size_t file_size = (size_t)file.tellg();
    std::vector<char> file_buffer(file_size);

    // move read position to start of file
    file.seekg(0);

    // read the file data into the buffer (stream "file_size" in total)
    file.read(file_buffer.data(), file_size);

    file.close();

    return file_buffer;
}

std::string File::getBaseDir()
{
    if (file_location.find_last_of("/\\") != std::string::npos)
        return file_location.substr(0, file_location.find_last_of("/\\"));
    return "";
}

File::~File() {}

/** @} */ // End of File utilities group