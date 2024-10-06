# cmake 
cmake_minimum_required(VERSION 3.30)
project(stow)

# compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# find relevant packages
find_package(Freetype REQUIRED)
find_package(X11 REQUIRED)

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

# install
install(FILES ${CMAKE_BINARY_DIR}/stow 
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE WORLD_EXECUTE
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)
