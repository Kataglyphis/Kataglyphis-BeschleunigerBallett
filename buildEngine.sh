cmake --list-presets=all ../
cmake -B build --preset linux-debug-clang
cmake --build ./build --preset linux-debug-clang