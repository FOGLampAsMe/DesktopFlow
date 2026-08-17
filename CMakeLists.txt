cmake_minimum_required(VERSION 3.21)

project(DesktopFlow VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Prefer Qt 6 for new installations, while keeping the current Qt 5.15
# environment usable during the migration.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(DESKTOPFLOW_SOURCES
    src/main.cpp
    src/App.cpp
    src/App.h
    src/RegionSelector.cpp
    src/RegionSelector.h
    src/ScreenRecorder.cpp
    src/ScreenRecorder.h
)

add_executable(DesktopFlow WIN32 ${DESKTOPFLOW_SOURCES})
target_include_directories(DesktopFlow PRIVATE src)
target_compile_definitions(DesktopFlow PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)
target_link_libraries(DesktopFlow PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    user32
    gdi32
    d3d11
    dxgi
    ole32
    mfplat
    mfreadwrite
    mfuuid
)

if (MSVC)
    target_compile_options(DesktopFlow PRIVATE /W4 /permissive-)
else()
    target_compile_options(DesktopFlow PRIVATE -Wall -Wextra -Wpedantic)
endif()
