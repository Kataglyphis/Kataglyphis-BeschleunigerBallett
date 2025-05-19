#include "util/File.hpp"

#include <fstream>
#include <iostream>

File::File(const std::string& file_location) {
  this->file_location = file_location;
}

std::string File::read() {
  std::string content;
  std::string fileLocationWrappedInquotationMarks = makePathsWithBlanksPossible(file_location);
  std::ifstream file_stream(file_location, std::ios::in);

  if (!file_stream.is_open()) {
    printf("Failed to read %s. File does not exist.", file_location.c_str());
    return "";
  }

  std::string line = "";
  while (!file_stream.eof()) {
    std::getline(file_stream, line);
    content.append(line + "\n");
  }

  file_stream.close();
  return content;
}

File::~File() {}

// https:www.howtogeek.com/694949/how-to-escape-spaces-in-file-paths-on-the-windows-command-line/
// enclosure path with quotation mark 
       std::string File::makePathsWithBlanksPossible(
    const std::string& file_location_with_possible_blanks) {

    std::string new_file_location = file_location_with_possible_blanks;
         const std::string quotationMark = std::string("\"");
    new_file_location.insert(0, quotationMark);
         new_file_location+=(quotationMark);

  return std::string();
}
