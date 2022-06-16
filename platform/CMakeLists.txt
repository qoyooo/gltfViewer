set(PLATFORM_SOURCE
    filedialog.cpp
    ../external/tinyfiledialogs/tinyfiledialogs.cpp
)

file(GLOB PLATFORM_HEADERS "*.h")

if(WIN32)
    add_library(platform STATIC ${PLATFORM_SOURCE} ${PLATFORM_HEADERS})
    target_link_libraries(platform ${WINLIBS})
elseif(APPLE)
    add_library(platform STATIC ${PLATFORM_SOURCE} ${PLATFORM_HEADERS})
    target_link_libraries(platform ${Vulkan_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})
endif()