# 3d-game-engine
![License](https://img.shields.io/badge/license-MIT-blue.svg)

This is a simple game engine based on Vulkan, designed for building 3D games using an Entity Component System (ECS) architecture.

## Features

- Vulkan rendering backend for high-performance graphics
- Entity Component System (ECS) architecture for flexible and modular game development
- Support for 3D models, textures, and shaders
- Physics simulation and collision detection
- Input handling for keyboard, mouse, and gamepad
- Scene management and resource loading
- Cross-platform compatibility

## Getting Started

### Prerequisites

- C++17 compiler
- Vulkan SDK

### Building the Engine

1. Clone the repository
2. Run `cmake` to generate build files
```shell
cd 3d-game-engine
cmake -DCMAKE_TOOLCHAIN_FILE=<path/to/vcpkg>/scripts/buildsystems/vcpkg.cmake -S . -B build
```
3. Build the project
```shell
cmake --build build
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
```
MIT License

Copyright (c) 2024 Miao Keda

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
