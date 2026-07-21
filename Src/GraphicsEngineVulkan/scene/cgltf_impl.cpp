// The single translation unit that compiles cgltf's implementation. Kept out of
// the GltfLoader module TU on purpose: a `#define CGLTF_IMPLEMENTATION` inside a
// C++20 module's global fragment attaches all of cgltf's definitions to that
// module, which the standard leaves murky and clang handles inconsistently.
// A plain non-module TU is the well-trodden single-header pattern - GltfLoader
// includes the declarations, this file provides the definitions, the linker
// joins them.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
