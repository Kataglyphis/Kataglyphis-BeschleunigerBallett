#pragma once
#include <string>

class File
{
  public:
    explicit File(const std::string &file_location);

    std::string read();

    ~File();

  private:
    std::string file_location;

    std::string makePathsWithBlanksPossible(const std::string &file_location_with_possible_blanks);
};
