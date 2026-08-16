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
        stowlegacy
    )
    target_include_directories(${name} PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${FREETYPE_INCLUDE_DIRS}
    )
endforeach()

# test_overlay is a consumer of the public API only - it builds with nothing
# but include/ and libstow.a. If it ever needs stowlegacy, the API has a hole.
foreach(apitest test_overlay test_screen test_cursor)
    add_executable(${apitest} ${apitest}.cpp)
    target_link_libraries(${apitest} PRIVATE stow::stow)
    target_include_directories(${apitest} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
add_test(NAME test_overlay     COMMAND test_overlay)
add_test(NAME test_screen      COMMAND test_screen)
add_test(NAME test_cursor      COMMAND test_cursor 3)

# Tests that need a real X display. Two properties:
#   SKIP_RETURN_CODE 77 - they report skipped, not failed, when $DISPLAY is
#     unset, so a headless run stays meaningful.
#   LABELS x11 - they map windows over the live desktop and take ~16s, which
#     is disruptive to whoever is using the machine. They are OPT-IN:
#       ctest              -> headless only (fast, no windows)   [default]
#       ctest -L x11       -> only the X11 ones
#       ctest -L '.*'      -> everything
set(STOW_X11_TESTS
    test_xwindow test_fullscreen test_clickthrough test_dashboard test_stow
    test_widgets test_monitor test_overlay test_screen test_cursor)

set_tests_properties(${STOW_X11_TESTS} PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS x11)

# Off by default so a routine `ctest` never takes over the desktop of whoever
# is sitting at the machine. Turn on deliberately:
#   cmake -S . -B build -DSTOW_TEST_X11=ON
option(STOW_TEST_X11 "Run the X11 tests, which map windows over the live desktop" OFF)
if(NOT STOW_TEST_X11)
    set_tests_properties(${STOW_X11_TESTS} PROPERTIES DISABLED TRUE)
    message(STATUS "X11 tests disabled (-DSTOW_TEST_X11=ON to enable)")
endif()

target_compile_definitions(test_hud_config PRIVATE
    HUD_TEST_INI="${CMAKE_CURRENT_SOURCE_DIR}/hud_test.ini")

message(STATUS "end of test creation")
