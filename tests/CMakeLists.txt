find_package(Qt${QT_VERSION_MAJOR} ${VIRTUALITEMVIEWS_QT_MINIMUM_VERSION} REQUIRED COMPONENTS Test)

add_library(viv_test_support STATIC
    support/vivtestfixtures.h
    support/vivtestfixtures.cpp)
target_link_libraries(viv_test_support PUBLIC VirtualItemViews Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(viv_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
if(MSVC)
    target_compile_options(viv_test_support PRIVATE /utf-8)
endif()

function(viv_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE viv_test_support)
    add_test(NAME ${name} COMMAND ${name})
    # Widgets are created by the tests, so run them with the offscreen platform
    # plugin for reproducible, headless results.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

viv_add_test(tst_sizeindex unit/tst_sizeindex.cpp)
viv_add_test(tst_scrollmapper unit/tst_scrollmapper.cpp)
viv_add_test(tst_widgetrecycler unit/tst_widgetrecycler.cpp)
viv_add_test(tst_listlayout unit/tst_listlayout.cpp)
viv_add_test(tst_headergeometry unit/tst_headergeometry.cpp)
viv_add_test(tst_treevisibilityindex unit/tst_treevisibilityindex.cpp)
viv_add_test(tst_virtualitemview unit/tst_virtualitemview.cpp)
viv_add_test(tst_virtuallistview unit/tst_virtuallistview.cpp)
viv_add_test(tst_virtualtableview unit/tst_virtualtableview.cpp)
viv_add_test(tst_tablecellmode unit/tst_tablecellmode.cpp)
viv_add_test(tst_virtualtreeview unit/tst_virtualtreeview.cpp)
viv_add_test(tst_modelmutationfuzz fuzz/tst_modelmutationfuzz.cpp)

if(VIRTUALITEMVIEWS_BUILD_GUI_TESTS)
    viv_add_test(tst_listviewinteraction gui/tst_listviewinteraction.cpp)
    viv_add_test(tst_tableviewinteraction gui/tst_tableviewinteraction.cpp)
endif()
