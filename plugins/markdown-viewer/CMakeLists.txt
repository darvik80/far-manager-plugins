cmake_minimum_required(VERSION 3.20)

set(PLUGIN_NAME MarkdownViewer)
set(PLUGIN_GUID "E4B1428A-16D3-4F8C-9B2A-7D5F1C3E9A4B")

add_library(${PLUGIN_NAME} SHARED
    src/main.cpp
    src/markdown_parser.cpp
    src/viewer.cpp
    exports.def
)

target_include_directories(${PLUGIN_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/sdk
    src
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set_target_properties(${PLUGIN_NAME} PROPERTIES
    OUTPUT_NAME ${PLUGIN_NAME}
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
