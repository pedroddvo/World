cmake_minimum_required(VERSION 3.30)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

project(world)

add_executable(world
    src/gfx.cpp
    src/main.cpp)

include(FetchContent)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG master)
FetchContent_Declare(vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG main)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.x)
FetchContent_MakeAvailable(glfw vk-bootstrap spdlog)

find_package(Vulkan REQUIRED)

target_link_libraries(world PRIVATE glfw vk-bootstrap spdlog Vulkan::Vulkan)
