module;

#include <filesystem>
#include <system_error>
#include <fstream>
#include <string>
#include <vector>

export module kataglyphis.shared.util.file_reader;

export namespace Kataglyphis::Shared {

// The error_code overload is REQUIRED, not stylistic. std::filesystem::exists
// without it throws on any error the OS reports for the query itself - most
// commonly permission denied, which Windows returns for paths such as
// C:\Windows\System32\config\SAM. Exceptions are disabled project-wide
// (/EHs-, -fno-exceptions), so that throw is a terminate, not something a
// caller could ever handle: a shader path pointing somewhere unreadable would
// take down the engine instead of logging "file does not exist".
//
// Found by shader_file_reader_fuzz_test on its first run.
//
// A path we cannot even query is, for every caller here, indistinguishable
// from one that is not there - they all fall back to "return empty and log".
inline bool fileExists(const std::string &file_location)
{
    std::error_code ignored;
    return std::filesystem::exists(file_location, ignored);
}

inline std::string readTextFile(const std::string &file_location)
{
    std::string content;
    std::ifstream file_stream(file_location, std::ios::in);

    if (!file_stream.is_open()) { return {}; }

    std::string line;
    while (std::getline(file_stream, line)) { content.append(line).append("\n"); }

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
