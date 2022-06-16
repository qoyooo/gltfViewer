cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
cmake_policy(VERSION 3.8)

set(NAME glTFViewer)

project(${NAME})

string(LENGTH "${CMAKE_SOURCE_DIR}/" ROOT_PATH_SIZE)
add_definitions(-DROOT_PATH_SIZE=${ROOT_PATH_SIZE})

# Enable DRACO uncompress function
add_definitions(-DTINYGLTF_ENABLE_DRACO)

include_directories(external)
include_directories(external/draco/src)
include_directories(external/glm)
include_directories(external/gli)
include_directories(external/imgui)
include_directories(external/tinygltf)
include_directories(platform)
include_directories(framework)
include_directories(${CMAKE_BINARY_DIR})

# Use FindVulkan module added with CMAKE 3.7
if (NOT CMAKE_VERSION VERSION_LESS 3.7.0)
    message(STATUS "Using module to find Vulkan")
    find_package(Vulkan)
endif()

IF(UNIX AND NOT APPLE)
    set(LINUX TRUE)
ENDIF()

IF(WIN32)
    IF (NOT Vulkan_FOUND)
        find_library(Vulkan_LIBRARY NAMES vulkan-1 vulkan PATHS ${CMAKE_SOURCE_DIR}/libs/vulkan)
        IF (Vulkan_LIBRARY)
            set(Vulkan_FOUND ON)
            MESSAGE("Using bundled Vulkan library version")
        ENDIF()
    ENDIF()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
ELSEIF(LINUX)
    IF (NOT Vulkan_FOUND)
        find_library(Vulkan_LIBRARY NAMES vulkan HINTS "$ENV{VULKAN_SDK}/lib" "${CMAKE_SOURCE_DIR}/libs/vulkan" REQUIRED)
        IF (Vulkan_LIBRARY)
            set(Vulkan_FOUND ON)
            MESSAGE("Using bundled Vulkan library version")
        ENDIF()
    ENDIF()
    find_package(Threads REQUIRED)
ELSEIF(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_MACOS_MVK")
ENDIF(WIN32)

IF (NOT Vulkan_FOUND)
    message(FATAL_ERROR "Could not find Vulkan library!")
ELSE()
    message(STATUS ${Vulkan_LIBRARY})
ENDIF()

# Set preprocessor defines
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX -D_USE_MATH_DEFINES")

# Clang specific stuff
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch-enum")
endif()

if(RESOURCE_INSTALL_DIR)
    add_definitions(-DVK_EXAMPLE_DATA_DIR=\"${RESOURCE_INSTALL_DIR}/\")
    install(DIRECTORY data/ DESTINATION ${RESOURCE_INSTALL_DIR}/)
else()
    add_definitions(-DVK_EXAMPLE_DATA_DIR=\"${CMAKE_SOURCE_DIR}/data/\")
endif()

# Compiler specific stuff
IF(MSVC)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
    add_definitions(/std:c++17)
    add_definitions(/MP)
ELSEIF(APPLE)
    add_definitions(-std=c++17)
ENDIF(MSVC)

IF(WIN32)
    # Nothing here (yet)
ELSEIF(APPLE)
    link_libraries(${Vulkan_LIBRARY} "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework AppKit" "-framework QuartzCore")
ELSEIF(LINUX)
    link_libraries(${Vulkan_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})
ENDIF()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/")

add_subdirectory(external/glfw)
add_subdirectory(external/draco)
add_subdirectory(platform)
add_subdirectory(framework)
add_subdirectory(src)