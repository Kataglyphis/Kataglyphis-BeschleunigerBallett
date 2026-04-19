#include <stdint.h>
#include <stddef.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size >= 3 && Data[0] == 'F' && Data[1] == 'U' && Data[2] == 'Z') {
    // Crash to prove the fuzzer works
    int *p = nullptr;
    *p = 42;
  }
  return 0;
}
