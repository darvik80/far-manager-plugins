cmake_minimum_required(VERSION 3.20)

set(PLUGIN_NAME PrettyViewer)

add_library(${PLUGIN_NAME} SHARED
    src/main.cpp
    src/format.cpp
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

add_executable(PrettyViewerTest
    src/format_test.cpp
    src/format.cpp
)

target_include_directories(PrettyViewerTest PRIVATE src)
target_compile_definitions(PrettyViewerTest PRIVATE UNICODE _UNICODE)
if(MSVC)
    set_target_properties(PrettyViewerTest PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()
