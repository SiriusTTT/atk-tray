cmake_minimum_required(VERSION 3.15)
project(atk-tray LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(atk-tray WIN32 src/main.cpp)

target_link_libraries(atk-tray PRIVATE
    setupapi
    hid
    user32
    gdi32
    shell32
    advapi32
)

if(MSVC)
    target_link_options(atk-tray PRIVATE /ENTRY:wWinMainCRTStartup)
endif()
