#include "util/File.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <print>

File::File(const std::string &file_location) : file_location(file_location) {}

auto File::read() -> std::string
{
    std::string content;
    std::string const fileLocationWrappedInquotationMarks = makePathsWithBlanksPossible(file_location);
    std::ifstream file_stream(file_location, std::ios::in);

    if (!file_stream.is_open()) {
        std::print("Failed to read {}. File does not exist.", file_location);
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

File::~File() = default;

// https:www.howtogeek.com/694949/how-to-escape-spaces-in-file-paths-on-the-windows-command-line/
// enclosure path with quotation mark
auto File::makePathsWithBlanksPossible(const std::string &file_location_with_possible_blanks) -> std::string
{

    std::string new_file_location = file_location_with_possible_blanks;
    const std::string quotationMark = std::string("\"");
    new_file_location.insert(0, quotationMark);
    new_file_location += (quotationMark);

    return {};
}
