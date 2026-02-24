module;

#include <string>
#include <vector>

export module kataglyphis.vulkan.file;

export namespace Kataglyphis {
class File
{
  public:
    explicit File(const std::string &file_location);

    std::string read();
    std::vector<char> readCharSequence();
    std::string getBaseDir();

    ~File();

  private:
    std::string file_location;
};
}// namespace Kataglyphis
