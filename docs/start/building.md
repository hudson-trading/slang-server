### Building from Source


`slang-server` requires the following tools in order to build:
- [python 3](https://www.python.org/)
- [CMake](https://cmake.org/) (3.20 or later)
- C++20 compatible compiler. Minimum supported compiler versions:
    - GCC 11
    - clang 17
    - Xcode 16
    - MSVC support is tested only against the most recent update of VS 2022


```bash
# Clone the repository
git clone https://github.com/hudson-trading/slang-server.git
cd slang-server

# Pull dependencies (slang and reflect-cpp)
git submodule update --init --recursive

# Build with cmake using a C++20 compliant compiler
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --target slang_server
```

The binary will be at `build/bin/slang-server`.
