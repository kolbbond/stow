# A standalone consumer of the installed package. It deliberately does NOT know
# where X11, freetype, or stow's src/ live - if this configures and builds, the
# API boundary is real rather than aspirational.
cmake_minimum_required(VERSION 3.30)
project(stow_consumer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(stow REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE stow::stow)
