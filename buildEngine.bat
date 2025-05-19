mkdir build
cd build
cmake --preset x64-Clang-Windows-Debug ../
cmake --build . --target GraphicsEngine --verbose --clean-first
pause