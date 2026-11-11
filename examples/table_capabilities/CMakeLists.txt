# One executable; every chapter owns its model, adapter and widgets.
add_executable(table_capabilities
    main.cpp
    01_row_widgets/example.cpp
    01_row_widgets/row_widgets.ui
    02_cell_widgets/example.cpp
    02_cell_widgets/cell_widgets.ui
    03_widget_types/example.cpp
    03_widget_types/widget_types.ui
    04_binding_context/example.cpp
    04_binding_context/binding_context.ui
    05_model_updates/example.cpp
    05_model_updates/model_updates.ui
    06_row_heights/example.cpp
    06_row_heights/row_heights.ui
    07_columns/example.cpp
    07_columns/columns.ui
    08_stretch_columns/example.cpp
    08_stretch_columns/stretch_columns.ui
    09_spacing/example.cpp
    09_spacing/spacing.ui
    10_visual_states/example.cpp
    10_visual_states/visual_states.ui
    11_state_content/example.cpp
    11_state_content/state_content.ui
    12_selection/example.cpp
    12_selection/selection.ui
    13_item_events/example.cpp
    13_item_events/item_events.ui
    14_scrolling/example.cpp
    14_scrolling/scrolling.ui
    15_virtualization/example.cpp
    15_virtualization/virtualization.ui
    16_frozen_areas/example.cpp
    16_frozen_areas/frozen_areas.ui
    17_panes/example.cpp
    17_panes/panes.ui
    18_headers/example.cpp
    18_headers/headers.ui
    19_custom_header/example.cpp
    19_custom_header/custom_header.ui
    19_custom_header/styled_headers.cpp
    19_custom_header/custom_header_states.ui
    20_header_persistence/example.cpp
    20_header_persistence/header_persistence.ui
    21_sorting/example.cpp
    21_sorting/sorting.ui
    22_row_reordering/example.cpp
    22_row_reordering/row_reordering.ui
    23_spans/example.cpp
    23_spans/spans.ui
    24_drag_indicator/example.cpp
    24_drag_indicator/drag_indicator.ui
    25_drag_animation/example.cpp
    25_drag_animation/drag_animation.ui
    26_cell_drop/example.cpp
    26_cell_drop/cell_drop.ui
    27_pinning/example.cpp
    27_pinning/pinning.ui
    28_diagnostics/example.cpp
    28_diagnostics/diagnostics.ui
    30_borders/example.cpp
    30_borders/borders.ui
    31_auto_height/example.cpp
    31_auto_height/auto_height.ui
    32_colors/example.cpp
    32_colors/colors.ui
    33_editors/example.cpp
    33_editors/editors.ui
    33_editors/popup.ui
)

set_target_properties(table_capabilities PROPERTIES AUTOUIC ON)
target_link_libraries(table_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(_viv_demo_smooth_enabled AND TARGET SmoothScrollbar::SmoothScrollbar)
    target_sources(table_capabilities PRIVATE 29_smooth_scrolling/example.cpp 29_smooth_scrolling/smooth_scrolling.ui)
    target_link_libraries(table_capabilities PRIVATE SmoothScrollbar::SmoothScrollbar)
    target_compile_definitions(table_capabilities PRIVATE VIV_TABLE_HAS_SMOOTH_SCROLLBAR=1)
endif()
if(MSVC)
    target_compile_options(table_capabilities PRIVATE /utf-8)
endif()
