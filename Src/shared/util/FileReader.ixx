module;

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

export module kataglyphis.shared.util.file_reader;

export namespace Kataglyphis::Shared {

inline bool fileExists(const std::string &file_location)
{
    return std::filesystem::exists(file_location);
}

inline std::string readTextFile(const std::string &file_location)
{
    std::string content;
    std::ifstream file_stream(file_location, std::ios::in);

    if (!file_stream.is_open()) { return {}; }

    std::string line;
    while (std::getline(file_stream, line)) {
        content.append(line).append("\n");
    }

    return content;
}

inline std::vector<char> readBinaryFile(const std::string &file_location)
{
    std::ifstream file(file_location, std::ios::binary | std::ios::ate);

    if (!file.is_open()) { return {}; }

    std::streampos const end_pos = file.tellg();
    if (end_pos < 0) { return {}; }

    auto const file_size = static_cast<size_t>(end_pos);
    std::vector<char> file_buffer(file_size);

    file.seekg(0);
    file.read(file_buffer.data(), static_cast<std::streamsize>(file_size));
    return file_buffer;
}

inline std::string getBaseDir(const std::string &file_location)
{
    if (file_location.find_last_of("/\\") != std::string::npos) {
        return file_location.substr(0, file_location.find_last_of("/\\"));
    }
    return {};
}

}// namespace Kataglyphis::Shared
