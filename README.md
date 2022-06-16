# glTFViewer

#### Description
glTF Model Viewer, a PBR(Physically-Based Rendering) example implementation with image based lighting in Vulkan using glTF 2.0 models. The lighting equation is based on the reference glTF PBR implementation from Khronos.

This project is based on  [Vulkan-glTF-PBR](https://github.com/qoyooo/Vulkan-glTF-PBR).

#### Software Architecture
Software architecture description

#### Dependencies

* [glfw](https://github.com/glfw/glfw) : A multi-platform library for OpenGL, OpenGL ES, Vulkan, window and input.
* [imgui](https://github.com/ocornut/imgui) : Dear ImGui: Bloat-free Immediate Mode Graphical User interface for C++ with minimal dependencies.
* [imguizmo](https://github.com/CedricGuillemet/ImGuizmo) : Immediate mode 3D gizmo for scene editing and other controls based on Dear Imgui.
* [spdlog](https://github.com/gabime/spdlog) : Fast C++ logging library.
* [stb](https://github.com/nothings/stb) : Single-file public domain (or MIT licensed) libraries for C/C++.
* [tinygltf](https://github.com/syoyo/tinygltf) : Header only C++11 tiny glTF 2.0 library


#### Installation

1. Download and install [Vulkan SDK](https://vulkan.lunarg.com/)
2. Clone the repository: this repository contains submodules for some of the external dependencies, so when doing a fresh clone you need to clone recursively:
        ```
        git clone --recursive https://gitee.com/xuanyishenzhen/glTFViewer.git
        ```
        or
        ```
        git clone https://gitee.com/xuanyishenzhen/glTFViewer.git
        git submodule init
        git submodule update
        ```

3. Building & Running
* MacOS
    ```
    cd glTFViewer
    mkdir build
    cd build
    cmake ..

    make

    ./glTFViewer
    ```
    Attention: it must be in 'build' directory to run glTFViewer, because the shader and resources use relative path.

* Window

    ```
    cd glTFViewer
    mkdir build
    cd build
    cmake -G "Visual Studio 16 2019" ..

    msbuild glTFViewer.sln

    ./Debug/glTFViewer.exe
    ```
    Attention: it must be in 'build' directory to run glTFViewer, because the shader and resources use relative path.


