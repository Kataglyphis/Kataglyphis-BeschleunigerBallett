#pragma once

#include <string>

class DebugApp
{
  public:
    DebugApp();

    static bool areErrorPrintAll(const std::string &AdditionalArrayMessage = "Empty",
      const char *file = __FILE__,
      int line = __LINE__);

    static bool arePreError(const std::string &AdditionalArrayMessage = "Empty",
      const char *file = __FILE__,
      int line = __LINE__);

    ~DebugApp();

  private:
};
