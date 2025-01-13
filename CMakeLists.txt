# cmake 
cmake_minimum_required(VERSION 3.30)
project(stow)

# compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# output paths
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin CACHE PATH "" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib CACHE PATH "" FORCE)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib CACHE PATH "" FORCE)

# find relevant packages
find_package(Freetype REQUIRED)
find_package(X11 REQUIRED)

# add library
add_library(stowlib INTERFACE)
target_include_directories(stowlib INTERFACE include)
target_link_libraries(stowlib INTERFACE
    freetype
    X11
    Xft
    Xext
    Xfixes
    
)

if(0)
# add sources
set(source_list 
    src/stow.c
)

add_executable(stow
    ${source_list}
)

target_link_libraries(stow
    freetype
    X11
    Xft
    Xext
    Xfixes
    
)

target_include_directories(stow PUBLIC
    include
    ${FREETYPE_INCLUDE_DIRS}
    
)
endif()

# add test
enable_testing()
add_subdirectory(test)

# install
install(FILES ${CMAKE_BINARY_DIR}/stow 
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE WORLD_EXECUTE
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)
