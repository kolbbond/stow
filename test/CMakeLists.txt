# tests for stow
# test Window abstraction
# test concurrent process
# testing
message(STATUS "configuring tests")

# Platform-specific test files
if(WIN32)
    set(test_list
        test_process.cpp
        test_stow.cpp
    )
else()
    # Linux/POSIX tests include all X11-specific tests
    set(test_list
        test_xwindow.cpp
        test_process.cpp
        test_stow.cpp
        test_fullscreen.cpp
        test_grid.cpp
    )
endif()

# walk over source files
foreach(srcfile ${test_list})
    string(REPLACE ".cpp" "" name ${srcfile})
    add_executable(${name} ${srcfile})
    target_link_libraries(${name}
        stowlib
    )

    if(NOT WIN32)
        target_include_directories(${name} PUBLIC
            include
            ${FREETYPE_INCLUDE_DIRS}
        )
    endif()

    add_test(${name} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${name})
endforeach()

message(STATUS "end of test creation")
