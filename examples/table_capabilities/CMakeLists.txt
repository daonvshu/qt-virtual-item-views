# One executable; every chapter owns its model, adapter and widgets.
add_executable(table_capabilities
    main.cpp
    01_row_widgets/example.cpp
    02_cell_widgets/example.cpp
    03_widget_types/example.cpp
    04_binding_context/example.cpp
    05_model_updates/example.cpp
    06_row_heights/example.cpp
    07_columns/example.cpp
    08_stretch_columns/example.cpp
    09_spacing/example.cpp
    10_visual_states/example.cpp
    11_state_content/example.cpp
    12_selection/example.cpp
    13_item_events/example.cpp
    14_scrolling/example.cpp
    15_virtualization/example.cpp
    16_frozen_areas/example.cpp
    17_panes/example.cpp
    18_headers/example.cpp
    19_custom_header/example.cpp
    19_custom_header/styled_headers.cpp
    20_header_persistence/example.cpp
    21_sorting/example.cpp
    22_row_reordering/example.cpp
    23_spans/example.cpp
    24_drag_indicator/example.cpp
    25_drag_animation/example.cpp
    26_cell_drop/example.cpp
    27_pinning/example.cpp
    28_diagnostics/example.cpp
    30_borders/example.cpp
    30_borders/example.ui
)

set_target_properties(table_capabilities PROPERTIES AUTOUIC ON)
target_link_libraries(table_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(_viv_demo_smooth_enabled AND TARGET SmoothScrollbar::SmoothScrollbar)
    target_sources(table_capabilities PRIVATE 29_smooth_scrolling/example.cpp)
    target_link_libraries(table_capabilities PRIVATE SmoothScrollbar::SmoothScrollbar)
    target_compile_definitions(table_capabilities PRIVATE VIV_TABLE_HAS_SMOOTH_SCROLLBAR=1)
endif()
if(MSVC)
    target_compile_options(table_capabilities PRIVATE /utf-8)
endif()
