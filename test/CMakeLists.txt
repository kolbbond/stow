# tests for stow
message(STATUS "configuring tests")

# add the test files
set(test_list
    test_xwindow.cpp
    test_process.cpp
    test_stow.cpp
    test_fullscreen.cpp
    test_dashboard.cpp
    test_monitor.cpp
    test_layout.cpp
    test_layout_unit.cpp
    test_grid_unit.cpp
    test_color_unit.cpp
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
add_test(NAME test_dashboard   COMMAND test_dashboard ${CMAKE_CURRENT_SOURCE_DIR}/hud_test.ini 2)
add_test(NAME test_monitor     COMMAND test_monitor)
add_test(NAME test_layout      COMMAND test_layout)
add_test(NAME test_layout_unit COMMAND test_layout_unit)
add_test(NAME test_grid_unit   COMMAND test_grid_unit)
add_test(NAME test_color_unit  COMMAND test_color_unit)
add_test(NAME test_hud_config  COMMAND test_hud_config)
add_test(NAME test_clickthrough COMMAND test_clickthrough)
add_test(NAME test_widgets     COMMAND test_widgets)

# Tests that need a real X display report 77 (skip) instead of failing when
# $DISPLAY is unset, so a headless run stays meaningful.
set_tests_properties(
    test_xwindow test_fullscreen test_clickthrough test_dashboard test_stow
    test_widgets test_monitor
    PROPERTIES SKIP_RETURN_CODE 77)

target_compile_definitions(test_hud_config PRIVATE
    HUD_TEST_INI="${CMAKE_CURRENT_SOURCE_DIR}/hud_test.ini")

message(STATUS "end of test creation")
