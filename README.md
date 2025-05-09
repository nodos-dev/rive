# Nodos plugin for Rive

This folder contains the Nodos plugin for rendering [Rive](https://rive.app) assets.

## Build Instructions
1. Download latest Nodos release from [nodos.dev](https://nodos.dev)
2. Clone the repository under Nodos workspace Module directory
```bash
git clone https://github.com/nodos-dev/rive.git --recurse-submodules Module/rive
```
3. Generate project files from workspace root directory using CMake:
```bash
cmake -S ./Toolchain/CMake -B Build
```
4. Build the project:
```bash
cmake --build Build
```

