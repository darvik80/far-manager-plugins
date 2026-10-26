cmake_minimum_required(VERSION 3.20)
project(far-plugins LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(FAR_SDK_DIR ${CMAKE_SOURCE_DIR}/sdk)

include(cmake/LuaFarPlugin.cmake)

add_subdirectory(plugins/markdown-viewer)
add_subdirectory(plugins/email-viewer)
add_subdirectory(plugins/ssh-client)
add_subdirectory(plugins/secret-vault)
add_subdirectory(plugins/pretty-viewer)
add_subdirectory(plugins/html-viewer)
