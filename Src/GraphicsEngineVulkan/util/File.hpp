#pragma once
#include <string>
#include <vector>

/** @defgroup FileUtilities File Utilities
 *  Functions and utilities for performing file operations.
 *  @{
 */
namespace Kataglyphis {
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
/** @} */// End of File utilities group
