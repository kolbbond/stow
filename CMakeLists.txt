# cmake
cmake_minimum_required(VERSION 3.30)
project(stow)

# compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# output paths
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE PATH "" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib CACHE PATH "" FORCE)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib CACHE PATH "" FORCE)

# find relevant packages
find_package(Freetype REQUIRED)
find_package(X11 REQUIRED)

# Check for optional X11 extensions
set(STOW_COMPILE_DEFINITIONS "")

if(X11_Xrandr_FOUND)
    message(STATUS "XRandR found - multi-monitor support enabled")
    list(APPEND STOW_COMPILE_DEFINITIONS HAVE_XRANDR)
else()
    message(STATUS "XRandR not found - falling back to Xinerama or single monitor")
endif()

if(X11_Xinerama_FOUND)
    message(STATUS "Xinerama found - multi-monitor fallback available")
    list(APPEND STOW_COMPILE_DEFINITIONS HAVE_XINERAMA)
else()
    message(STATUS "Xinerama not found")
endif()

# add library
add_library(stowlib INTERFACE)
target_include_directories(stowlib INTERFACE
    include
    ${FREETYPE_INCLUDE_DIRS}
)
target_link_libraries(stowlib INTERFACE
    freetype
    X11
    Xft
    Xext
    Xfixes
)

# Add optional libraries
if(X11_Xrandr_FOUND)
    target_link_libraries(stowlib INTERFACE ${X11_Xrandr_LIB})
endif()

if(X11_Xinerama_FOUND)
    target_link_libraries(stowlib INTERFACE ${X11_Xinerama_LIB})
endif()

# Add compile definitions
target_compile_definitions(stowlib INTERFACE ${STOW_COMPILE_DEFINITIONS})

# main stow executable
add_executable(stow src/stow.cpp)
target_link_libraries(stow stowlib)

# shud - grid-based overlay HUD
add_executable(shud src/shud.cpp)
target_link_libraries(shud stowlib)

# add test
enable_testing()
add_subdirectory(test)

# install
install(TARGETS stow shud DESTINATION bin)
