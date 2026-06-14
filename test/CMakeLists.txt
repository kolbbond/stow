# tests for stow
message(STATUS "configuring tests")

# add the test files
set(test_list
    test_xwindow.cpp
    test_process.cpp
    test_stow.cpp
    test_fullscreen.cpp
    test_grid.cpp
    test_monitor.cpp
    test_layout.cpp
    test_hud_config.cpp
    test_clickthrough.cpp
    test_widgets.cpp
)

# walk over source files
foreach(srcfile ${test_list})
    string(REPLACE ".cpp" "" name ${srcfile})
    add_executable(${name} ${srcfile})
    target_link_libraries(${name}
        stowlib
    )
    target_include_directories(${name} PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${FREETYPE_INCLUDE_DIRS}
    )
endforeach()

# register tests; the demo runners need args (a command / a config + timeout)
add_test(NAME test_xwindow     COMMAND test_xwindow)
add_test(NAME test_process     COMMAND test_process echo "stow process test")
add_test(NAME test_stow        COMMAND test_stow)
add_test(NAME test_fullscreen  COMMAND test_fullscreen)
add_test(NAME test_grid        COMMAND test_grid ${CMAKE_CURRENT_SOURCE_DIR}/hud_test.ini 3)
add_test(NAME test_monitor     COMMAND test_monitor)
add_test(NAME test_layout      COMMAND test_layout)
add_test(NAME test_hud_config  COMMAND test_hud_config)
add_test(NAME test_clickthrough COMMAND test_clickthrough)
add_test(NAME test_widgets     COMMAND test_widgets)

target_compile_definitions(test_hud_config PRIVATE
    HUD_TEST_INI="${CMAKE_CURRENT_SOURCE_DIR}/hud_test.ini")

message(STATUS "end of test creation")
