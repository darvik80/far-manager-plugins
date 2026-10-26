cmake_minimum_required(VERSION 3.20)

add_luafar_plugin(HtmlViewer
    LUA_FILES
        lua/html_convert.lua
        lua/plugin.lua
)
