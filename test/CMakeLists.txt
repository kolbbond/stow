# tests for stow
# test XWindow
# test concurrent process
# testing
message(STATUS "configuring tests")

# add the test files
set(test_list 
    test_xwindow.cpp
    test_process.cpp
    test_stow.cpp
)


# walk over source files
foreach(srcfile ${test_list})
  string(REPLACE ".cpp" "" name ${srcfile})
  add_executable(${name} ${srcfile})
  target_link_libraries(${name} 
    freetype
    X11
    Xft
    Xext
    Xfixes

    stowlib
    
)

target_include_directories(${name} PUBLIC
    include
    ${FREETYPE_INCLUDE_DIRS}
    
)

  add_test(${name} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${name})
endforeach()

message(STATUS "end of test creation")

