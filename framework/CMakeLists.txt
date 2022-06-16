
file(GLOB FRAMEWORK_HEADERS "*.h")

set(FRAMEWORK_SRC
    #Vulkan
    vulkan/utils.cpp
    vulkan/device.cpp
    vulkan/buffer.cpp
    vulkan/texture.cpp
    vulkan/texture2d.cpp
    vulkan/texturecube.cpp
    vulkan/swapchain.cpp
    vulkan/window.cpp

    #UI Render
    ../external/imgui/imgui.cpp
    ../external/imgui/imgui_draw.cpp
    ../external/imgui/imgui_tables.cpp
    ../external/imgui/imgui_widgets.cpp
    ../external/imgui/ImGuizmo.cpp
    ../external/imgui/ImSequencer.cpp
    gui/render.cpp

    #glTF
    gltf/model.cpp
    gltf/render.cpp

    #skybox
    skybox/skybox.cpp
)

add_library(framework STATIC ${FRAMEWORK_SRC} ${FRAMEWORK_HEADERS})

if(WIN32)
    target_include_directories(framework PUBLIC  ${GLFW3_INCLUDE_DIR})
    target_link_libraries(framework  draco glfw ${Vulkan_LIBRARY} ${WINLIBS})
else(WIN32)
    target_include_directories(framework PUBLIC ${GLFW3_INCLUDE_DIR})
    target_link_libraries(framework glfw ${Vulkan_LIBRARY} ${CMAKE_THREAD_LIBS_INIT} )
endif(WIN32)