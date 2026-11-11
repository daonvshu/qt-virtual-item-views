# One process; every chapter owns its data, adapter and row widgets.
add_executable(tree_capabilities
    main.cpp
    01_basic/example.cpp
    01_basic/basic.ui
    02_row_widgets/example.cpp
    02_row_widgets/row_widgets.ui
    03_widget_types/example.cpp
    03_widget_types/widget_types.ui
    04_binding_context/example.cpp
    04_binding_context/binding_context.ui
    05_model_updates/example.cpp
    05_model_updates/model_updates.ui
    06_item_heights/example.cpp
    06_item_heights/item_heights.ui
    07_expansion/example.cpp
    07_expansion/expansion.ui
    08_root_index/example.cpp
    08_root_index/root_index.ui
    09_indentation/example.cpp
    09_indentation/indentation.ui
    10_branch_icons/example.cpp
    10_branch_icons/branch_icons.ui
    11_branch_renderer/example.cpp
    11_branch_renderer/branch_renderer.ui
    12_row_spacing/example.cpp
    12_row_spacing/row_spacing.ui
    13_visual_states/example.cpp
    13_visual_states/visual_states.ui
    14_state_content/example.cpp
    14_state_content/state_content.ui
    15_selection/example.cpp
    15_selection/selection.ui
    16_item_events/example.cpp
    16_item_events/item_events.ui
    17_scrolling/example.cpp
    17_scrolling/scrolling.ui
    18_virtualization/example.cpp
    18_virtualization/virtualization.ui
    19_drag_drop/example.cpp
    19_drag_drop/drag_drop.ui
    20_drag_animation/example.cpp
    20_drag_animation/drag_animation.ui
    21_sorting_filtering/example.cpp
    21_sorting_filtering/sorting_filtering.ui
    22_lazy_loading/example.cpp
    22_lazy_loading/lazy_loading.ui
    23_pinning/example.cpp
    23_pinning/pinning.ui
    24_diagnostics/example.cpp
    24_diagnostics/diagnostics.ui
    26_borders/example.cpp
    26_borders/borders.ui
    27_auto_height/example.cpp
    27_auto_height/auto_height.ui
    28_colors/example.cpp
    28_colors/colors.ui
    29_editors/example.cpp
    29_editors/editors.ui
    29_editors/popup.ui
)

set_target_properties(tree_capabilities PROPERTIES AUTOUIC ON)

target_link_libraries(tree_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(_viv_demo_smooth_enabled AND TARGET SmoothScrollbar::SmoothScrollbar)
    target_sources(tree_capabilities PRIVATE 25_smooth_scrolling/example.cpp 25_smooth_scrolling/smooth_scrolling.ui)
    target_link_libraries(tree_capabilities PRIVATE SmoothScrollbar::SmoothScrollbar)
    target_compile_definitions(tree_capabilities PRIVATE VIV_TREE_HAS_SMOOTH_SCROLLBAR=1)
endif()
if(MSVC)
    target_compile_options(tree_capabilities PRIVATE /utf-8)
endif()
