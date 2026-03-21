module;

#include "../../shared/util/FileLocationHolder.hpp"

#include <string>

export module kataglyphis.opengl.file;

export class File : private Kataglyphis::Shared::FileLocationHolder
{
  public:
    explicit File(const std::string &file_location);
    File(const File &) = default;
    File &operator=(const File &) = default;
    File(File &&) = default;
    File &operator=(File &&) = default;

    std::string read();

    ~File();

  private:
};
