# PERMUP Installation Guide

## Build Requirements
- CMake 3.10+
- C++17 compiler (g++ 7+ or clang 5+)
- Linux with PTY support
- Libraries: libpam, libutil

## Compilation
```bash
mkdir build
cd build
cmake ..
make