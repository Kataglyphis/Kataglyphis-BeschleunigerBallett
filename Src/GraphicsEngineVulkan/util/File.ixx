module;

#include "../../shared/util/FileLocationHolder.hpp"

#include <string>
#include <vector>

export module kataglyphis.vulkan.file;

export namespace Kataglyphis {
class File : private Kataglyphis::Shared::FileLocationHolder
{
  public:
    explicit File(const std::string &file_location);

    std::string read();
    std::vector<char> readCharSequence();
    std::string getBaseDir();

    ~File();

  private:
};
}// namespace Kataglyphis
