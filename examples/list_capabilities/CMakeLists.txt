# One executable; every capability owns its model, adapter and widgets.
add_executable(list_capabilities
    main.cpp
    01_basic/example.cpp
    02_row_widgets/example.cpp
    03_widget_types/example.cpp
    04_binding_context/example.cpp
    05_model_updates/example.cpp
    06_item_heights/example.cpp
    07_row_spacing/example.cpp
    08_visual_states/example.cpp
    09_state_content/example.cpp
    10_selection/example.cpp
    11_item_events/example.cpp
    12_scrolling/example.cpp
    13_virtualization/example.cpp
    14_frozen_rows/example.cpp
    15_drag_drop/example.cpp
    16_pinning/example.cpp
    17_diagnostics/example.cpp
)
target_link_libraries(list_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(_viv_demo_smooth_enabled AND TARGET SmoothScrollbar::SmoothScrollbar)
    target_sources(list_capabilities PRIVATE 18_smooth_scrolling/example.cpp)
    target_link_libraries(list_capabilities PRIVATE SmoothScrollbar::SmoothScrollbar)
    target_compile_definitions(list_capabilities PRIVATE VIV_LIST_HAS_SMOOTH_SCROLLBAR=1)
endif()
if(MSVC)
    target_compile_options(list_capabilities PRIVATE /utf-8)
endif()
