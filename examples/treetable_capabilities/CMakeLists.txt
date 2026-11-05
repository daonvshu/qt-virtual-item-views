# One process; each chapter owns its model, adapter and widgets.
add_executable(treetable_capabilities
    main.cpp
    01_basic/example.cpp
    01_basic/basic.ui
    02_row_widgets/example.cpp
    02_row_widgets/row_widgets.ui
    03_cell_widgets/example.cpp
    03_cell_widgets/cell_widgets.ui
    04_widget_types/example.cpp
    04_widget_types/widget_types.ui
    05_binding_context/example.cpp
    05_binding_context/binding_context.ui
    06_model_updates/example.cpp
    06_model_updates/model_updates.ui
    07_row_heights/example.cpp
    07_row_heights/row_heights.ui
    08_expansion/example.cpp
    08_expansion/expansion.ui
    09_root_index/example.cpp
    09_root_index/root_index.ui
    10_indentation/example.cpp
    10_indentation/indentation.ui
    11_branch_renderer/example.cpp
    11_branch_renderer/branch_renderer.ui
    12_columns/example.cpp
    12_columns/columns.ui
    13_stretch_columns/example.cpp
    13_stretch_columns/stretch_columns.ui
    14_spacing/example.cpp
    14_spacing/spacing.ui
    15_visual_states/example.cpp
    15_visual_states/visual_states.ui
    16_state_content/example.cpp
    16_state_content/state_content.ui
    17_selection/example.cpp
    17_selection/selection.ui
    18_item_events/example.cpp
    18_item_events/item_events.ui
    19_scrolling/example.cpp
    19_scrolling/scrolling.ui
    20_virtualization/example.cpp
    20_virtualization/virtualization.ui
    21_frozen_columns/example.cpp
    21_frozen_columns/frozen_columns.ui
    22_panes/example.cpp
    22_panes/panes.ui
    23_headers/example.cpp
    23_headers/headers.ui
    24_custom_header/example.cpp
    24_custom_header/custom_header.ui
    24_custom_header/styled_headers.cpp
    24_custom_header/custom_header_states.ui
    25_header_persistence/example.cpp
    25_header_persistence/header_persistence.ui
    26_sorting_filtering/example.cpp
    26_sorting_filtering/sorting_filtering.ui
    27_row_reordering/example.cpp
    27_row_reordering/row_reordering.ui
    28_spans/example.cpp
    28_spans/spans.ui
    29_drag_indicator/example.cpp
    29_drag_indicator/drag_indicator.ui
    30_drag_animation/example.cpp
    30_drag_animation/drag_animation.ui
    31_lazy_loading/example.cpp
    31_lazy_loading/lazy_loading.ui
    32_pinning/example.cpp
    32_pinning/pinning.ui
    33_diagnostics/example.cpp
    33_diagnostics/diagnostics.ui
    35_borders/example.cpp
    35_borders/borders.ui
    36_auto_height/auto_height.ui
    36_auto_height/example.cpp
    37_colors/example.cpp
    37_colors/colors.ui
)

set_target_properties(treetable_capabilities PROPERTIES AUTOUIC ON)
target_link_libraries(treetable_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(_viv_demo_smooth_enabled AND TARGET SmoothScrollbar::SmoothScrollbar)
    target_sources(treetable_capabilities PRIVATE 34_smooth_scrolling/example.cpp 34_smooth_scrolling/smooth_scrolling.ui)
    target_link_libraries(treetable_capabilities PRIVATE SmoothScrollbar::SmoothScrollbar)
    target_compile_definitions(treetable_capabilities PRIVATE VIV_TREETABLE_HAS_SMOOTH_SCROLLBAR=1)
endif()
if(MSVC)
    target_compile_options(treetable_capabilities PRIVATE /utf-8)
endif()
