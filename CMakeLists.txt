cmake_minimum_required(VERSION 2.8)

project(angrylion-rdp-plus)

set(CMAKE_CXX_STANDARD 14)
set(COMPILE_DEFINITIONS -O3 -Wall -Wextra -Winline)

find_package(Git REQUIRED)
find_package(PythonInterp 3.2 REQUIRED)

# RDP core
set(PATH_CORE "core")
set(PATH_VERSION_IN "${PATH_CORE}/version.h.in")
set(PATH_VERSION_OUT "${PATH_CORE}/version.h")

add_custom_command(
    OUTPUT ${PATH_VERSION_OUT}
    COMMAND
        ${PYTHON_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/make_version.py"
        "${CMAKE_SOURCE_DIR}/${PATH_VERSION_IN}"
        "${CMAKE_SOURCE_DIR}/${PATH_VERSION_OUT}"
    COMMENT "Generate Git version"
)

file(GLOB SOURCES_CORE "${PATH_CORE}/*.c" "${PATH_CORE}/*.cpp")
add_library(core STATIC ${SOURCES_CORE} ${PATH_VERSION_OUT})

# RDP retracer
set(PATH_RETRACER "retrace")

find_package(SDL2 REQUIRED)
include_directories(${PATH_CORE})
file(GLOB SOURCES_RETRACE "${PATH_RETRACER}/*.c")
add_executable(retrace ${SOURCES_RETRACE})

target_link_libraries(retrace core pthread SDL2)