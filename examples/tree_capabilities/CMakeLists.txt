# One process; every chapter owns its data, adapter and row widgets.
add_executable(tree_capabilities
    main.cpp
    01_basic/example.cpp
    02_row_widgets/example.cpp
    03_widget_types/example.cpp
    04_binding_context/example.cpp
    05_model_updates/example.cpp
    06_item_heights/example.cpp
    07_expansion/example.cpp
    08_root_index/example.cpp
    09_indentation/example.cpp
    10_branch_icons/example.cpp
    11_branch_renderer/example.cpp
    12_row_spacing/example.cpp
    13_visual_states/example.cpp
    14_state_content/example.cpp
    15_selection/example.cpp
    16_item_events/example.cpp
    17_scrolling/example.cpp
    18_virtualization/example.cpp
    19_frozen_rows/example.cpp
    20_drag_drop/example.cpp
    21_sorting_filtering/example.cpp
    22_lazy_loading/example.cpp
    23_pinning/example.cpp
    24_diagnostics/example.cpp
)
target_link_libraries(tree_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(_viv_demo_smooth_enabled AND TARGET SmoothScrollbar::SmoothScrollbar)
    target_sources(tree_capabilities PRIVATE 25_smooth_scrolling/example.cpp)
    target_link_libraries(tree_capabilities PRIVATE SmoothScrollbar::SmoothScrollbar)
    target_compile_definitions(tree_capabilities PRIVATE VIV_TREE_HAS_SMOOTH_SCROLLBAR=1)
endif()
if(MSVC)
    target_compile_options(tree_capabilities PRIVATE /utf-8)
endif()
