# One process; each chapter owns its model, adapter and widgets.
add_executable(treetable_capabilities
    main.cpp
    01_basic/example.cpp
    02_row_widgets/example.cpp
    03_cell_widgets/example.cpp
    04_widget_types/example.cpp
    05_binding_context/example.cpp
    06_model_updates/example.cpp
    07_row_heights/example.cpp
    08_expansion/example.cpp
    09_root_index/example.cpp
    10_indentation/example.cpp
    11_branch_renderer/example.cpp
    12_columns/example.cpp
    13_stretch_columns/example.cpp
    14_spacing/example.cpp
    15_visual_states/example.cpp
    16_state_content/example.cpp
    17_selection/example.cpp
    18_item_events/example.cpp
    19_scrolling/example.cpp
    20_virtualization/example.cpp
    21_frozen_columns/example.cpp
    22_panes/example.cpp
    23_headers/example.cpp
    24_custom_header/example.cpp
    24_custom_header/styled_headers.cpp
    25_header_persistence/example.cpp
    26_sorting_filtering/example.cpp
    27_row_reordering/example.cpp
    28_spans/example.cpp
    29_drag_indicator/example.cpp
    30_drag_animation/example.cpp
    31_lazy_loading/example.cpp
    32_pinning/example.cpp
    33_diagnostics/example.cpp
    35_borders/example.cpp
    35_borders/example.ui
    36_auto_height/auto_height.ui
    36_auto_height/example.cpp
)

set_target_properties(treetable_capabilities PROPERTIES AUTOUIC ON)
target_link_libraries(treetable_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(_viv_demo_smooth_enabled AND TARGET SmoothScrollbar::SmoothScrollbar)
    target_sources(treetable_capabilities PRIVATE 34_smooth_scrolling/example.cpp)
    target_link_libraries(treetable_capabilities PRIVATE SmoothScrollbar::SmoothScrollbar)
    target_compile_definitions(treetable_capabilities PRIVATE VIV_TREETABLE_HAS_SMOOTH_SCROLLBAR=1)
endif()
if(MSVC)
    target_compile_options(treetable_capabilities PRIVATE /utf-8)
endif()
