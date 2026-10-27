add_library(nk_client STATIC
    nk_http.cpp
    nk_json.cpp
    nk_settings.cpp
    nk_secure.cpp
    nk_api.cpp
)

target_include_directories(nk_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(nk_client PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(nk_client PUBLIC winhttp crypt32 advapi32)

set_target_properties(nk_client PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
