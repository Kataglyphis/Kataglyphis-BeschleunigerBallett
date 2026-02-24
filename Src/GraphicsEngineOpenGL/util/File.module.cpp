module;

#include <fstream>
#include <print>
#include <string>

module kataglyphis.opengl.file;

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

auto File::makePathsWithBlanksPossible(const std::string &file_location_with_possible_blanks) -> std::string
{

    std::string new_file_location = file_location_with_possible_blanks;
    const std::string quotationMark = std::string("\"");
    new_file_location.insert(0, quotationMark);
    new_file_location += (quotationMark);

    return {};
}
