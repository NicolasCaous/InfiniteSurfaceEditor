cmake_minimum_required(VERSION 3.8)
#include(FetchContent)

project("InfiniteSurfaceEditor")

add_executable(
    InfiniteSurfaceEditor
    "src/InfiniteSurfaceEditor.h"
    "src/InfiniteSurfaceEditor.cpp"
    "src/util/FileReader.h"
    "src/util/FileReader.cpp"
    "src/rendering/VulkanRenderer.h"
    "src/rendering/VulkanRenderer.cpp"
    "src/rendering/VulkanRendererUgly.h"
    "src/rendering/VulkanRendererUgly.cpp"
 "src/rendering/WindowEvents.cpp" "src/rendering/WindowEvents.h" "src/EventSystem.h" "src/EventSystem.cpp"  "src/util/SafeQueue.hpp")

#FetchContent_Declare(
#    fetch_vk_bootstrap
#    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
#    GIT_TAG        75ec283c3734e116f616d0e31d943ebec3066556
#)
#FetchContent_MakeAvailable(fetch_vk_bootstrap)
#target_link_libraries(InfiniteSurfaceEditor PRIVATE vk-bootstrap::vk-bootstrap)

find_package(SDL2 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

find_path(STB_INCLUDE_DIRS "stb_c_lexer.h")

include_directories("${Vulkan_INCLUDE_DIRS}")

target_link_libraries(InfiniteSurfaceEditor
    PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
target_link_libraries(InfiniteSurfaceEditor PRIVATE "${Vulkan_LIBRARIES}")
target_link_libraries(InfiniteSurfaceEditor PRIVATE tinyobjloader::tinyobjloader)
target_link_libraries(InfiniteSurfaceEditor PRIVATE glm::glm)

target_include_directories(InfiniteSurfaceEditor PRIVATE ${STB_INCLUDE_DIRS})

target_compile_definitions(InfiniteSurfaceEditor PRIVATE VULKAN_SHADER_DIR=${CMAKE_SOURCE_DIR}/src/rendering/shaders)

if(CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET InfiniteSurfaceEditor PROPERTY CXX_STANDARD 20)
endif()