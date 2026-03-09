#ifndef KATAGLYPHIS_SHARED_UTIL_FILE_LOCATION_HOLDER_HPP
#define KATAGLYPHIS_SHARED_UTIL_FILE_LOCATION_HOLDER_HPP

#include <string>

namespace Kataglyphis::Shared {
class FileLocationHolder
{
  public:
    explicit FileLocationHolder(std::string file_location) : file_location(std::move(file_location)) {}
    ~FileLocationHolder() = default;
    FileLocationHolder(FileLocationHolder const &) = default;
    FileLocationHolder(FileLocationHolder &&) = default;
    auto operator=(FileLocationHolder const &) -> FileLocationHolder & = default;
    auto operator=(FileLocationHolder &&) -> FileLocationHolder & = default;

  protected:
    auto get_file_location() const -> std::string const & { return file_location; }

  private:
    std::string file_location;
};
}// namespace Kataglyphis::Shared

#endif