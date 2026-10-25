cmake_minimum_required(VERSION 3.20)

set(PLUGIN_NAME EmailViewer)

add_library(${PLUGIN_NAME} SHARED
    src/main.cpp
    src/imap_client.cpp
    src/html_converter.cpp
    src/ui.cpp
    src/guid.cpp
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

target_link_libraries(${PLUGIN_NAME} PRIVATE ws2_32 crypt32)

find_package(OpenSSL REQUIRED)
target_link_libraries(${PLUGIN_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)

set_target_properties(${PLUGIN_NAME} PROPERTIES
    OUTPUT_NAME ${PLUGIN_NAME}
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
