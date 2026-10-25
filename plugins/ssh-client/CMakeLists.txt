cmake_minimum_required(VERSION 3.20)

set(PLUGIN_NAME SshClient)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "" FORCE)

add_subdirectory(
    ${CMAKE_SOURCE_DIR}/third_party/libssh2
    ${CMAKE_BINARY_DIR}/third_party/libssh2
)

if(TARGET libssh2_static)
    set_target_properties(libssh2_static PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

add_library(${PLUGIN_NAME} SHARED
    src/main.cpp
    src/ui.cpp
    src/ssh_config.cpp
    src/session_store.cpp
    src/sftp_client.cpp
    src/panel.cpp
    exports.def
)

target_include_directories(${PLUGIN_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/sdk
    ${CMAKE_SOURCE_DIR}/third_party/libssh2/include
    src
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(${PLUGIN_NAME} PRIVATE /DLIBSSH2_API=)

find_package(OpenSSL REQUIRED)
target_link_libraries(${PLUGIN_NAME} PRIVATE libssh2_static OpenSSL::Crypto ws2_32)

set_target_properties(${PLUGIN_NAME} PROPERTIES
    OUTPUT_NAME ${PLUGIN_NAME}
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
