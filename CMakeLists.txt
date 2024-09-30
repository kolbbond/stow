# cmake 
cmake_minimum_required(VERSION 3.30)
project(stow)

# find relevant packages
find_package(Freetype REQUIRED)
find_package(X11 REQUIRED)

# add sources
set(source_list 
    src/stow.c
)

add_executable(main
    ${source_list}
)

target_link_libraries(main
    freetype
    X11
    Xft
)

target_include_directories(main PUBLIC
    include
    ${FREETYPE_INCLUDE_DIRS}
    
)

