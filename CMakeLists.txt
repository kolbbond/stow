# cmake
cmake_minimum_required(VERSION 3.30)
project(stow)

# Require C++20 for designated initializers
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to RelWithDebInfo
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Keep asserts active in Release/RelWithDebInfo (remove NDEBUG)
string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
string(REPLACE "/DNDEBUG" "" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

# compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# output paths - flatten multi-config output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE PATH "" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib CACHE PATH "" FORCE)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib CACHE PATH "" FORCE)

# Override per-config output directories to avoid Debug/Release subdirs
foreach(CONFIG_TYPE DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} CACHE PATH "" FORCE)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} CACHE PATH "" FORCE)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY} CACHE PATH "" FORCE)
endforeach()

# Platform detection and library configuration
if(WIN32)
    message(STATUS "Building for Windows")
    set(STOW_PLATFORM_LIBS user32 gdi32)
    set(STOW_PLATFORM_SOURCES "")
else()
    message(STATUS "Building for POSIX/Linux")
    find_package(Freetype REQUIRED)
    find_package(X11 REQUIRED)
    set(STOW_PLATFORM_LIBS
        freetype
        X11
        Xft
        Xext
        Xfixes
        util
    )
    set(STOW_PLATFORM_SOURCES "")
endif()

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

# stow library - public API in include/, platform code private in src/
add_library(stow STATIC
    src/overlay.cpp
    src/x11/screen.cpp
)
add_library(stow::stow ALIAS stow)

target_include_directories(stow
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${FREETYPE_INCLUDE_DIRS}
)

# PRIVATE is the point: consumers link stow::stow and inherit no X11.
target_link_libraries(stow PRIVATE ${STOW_PLATFORM_LIBS})
if(X11_Xrandr_FOUND)
    target_link_libraries(stow PRIVATE ${X11_Xrandr_LIB})
endif()
if(X11_Xinerama_FOUND)
    target_link_libraries(stow PRIVATE ${X11_Xinerama_LIB})
endif()
target_compile_definitions(stow PRIVATE ${STOW_COMPILE_DEFINITIONS})

# Transitional: stow/shud and the older tests still include X11 headers
# directly, so they need the private include dir and the X11 libs. Later tasks
# port them onto the public API and this target goes away.
add_library(stowlegacy INTERFACE)
target_include_directories(stowlegacy INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FREETYPE_INCLUDE_DIRS}
)
target_link_libraries(stowlegacy INTERFACE stow::stow ${STOW_PLATFORM_LIBS})
if(X11_Xrandr_FOUND)
    target_link_libraries(stowlegacy INTERFACE ${X11_Xrandr_LIB})
endif()
if(X11_Xinerama_FOUND)
    target_link_libraries(stowlegacy INTERFACE ${X11_Xinerama_LIB})
endif()
target_compile_definitions(stowlegacy INTERFACE ${STOW_COMPILE_DEFINITIONS})

# main stow executable (the library now owns the target name "stow")
add_executable(stow_bin src/stow.cpp)
set_target_properties(stow_bin PROPERTIES OUTPUT_NAME stow)
target_link_libraries(stow_bin PRIVATE stowlegacy)

# shud - grid-based overlay HUD
add_executable(shud src/shud.cpp)
target_link_libraries(shud PRIVATE stowlegacy)

# add test
enable_testing()
add_subdirectory(test)

# install
install(TARGETS stow_bin shud DESTINATION bin)
