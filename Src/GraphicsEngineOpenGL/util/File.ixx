module;

#include "../../shared/util/FileLocationHolder.hpp"

#include <string>

export module kataglyphis.opengl.file;

export class File : private Kataglyphis::Shared::FileLocationHolder
{
  public:
    explicit File(const std::string &file_location);
    File(const File &) = default;
    auto operator=(const File &) -> File & = default;
    File(File &&) = default;
    auto operator=(File &&) -> File & = default;

    std::string read();

    ~File();

  private:
};
