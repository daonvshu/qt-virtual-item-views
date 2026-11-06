# One process; chapters intentionally do not share model or adapter implementations.
add_executable(wrap_capabilities main.cpp browser.ui
    01_basic/example.cpp
    01_basic/basic.ui
    02_geometry/example.cpp
    02_geometry/geometry.ui
    03_visual_states/example.cpp
    03_visual_states/visual_states.ui
    04_binding_events/example.cpp
    04_binding_events/binding_events.ui
    05_model_root/example.cpp
    05_model_root/model_root.ui
    06_scrolling/example.cpp
    06_scrolling/scrolling.ui
    07_drag_drop/example.cpp
    07_drag_drop/drag_drop.ui
    08_auto_height/example.cpp
    08_auto_height/auto_height.ui
    09_wrap_layout/example.cpp
    09_wrap_layout/wrap_layout.ui
    10_wrap_widget/example.cpp
    10_wrap_widget/wrap_widget.ui
)
set_target_properties(wrap_capabilities PROPERTIES AUTOUIC ON)
target_link_libraries(wrap_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(MSVC)
    target_compile_options(wrap_capabilities PRIVATE /utf-8)
endif()
