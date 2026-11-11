# One executable; every capability owns its model, adapter and widgets.
add_executable(list_capabilities
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
    07_row_spacing/example.cpp
    07_row_spacing/row_spacing.ui
    08_visual_states/example.cpp
    08_visual_states/visual_states.ui
    09_state_content/example.cpp
    09_state_content/state_content.ui
    10_selection/example.cpp
    10_selection/selection.ui
    11_item_events/example.cpp
    11_item_events/item_events.ui
    12_scrolling/example.cpp
    12_scrolling/scrolling.ui
    13_virtualization/example.cpp
    13_virtualization/virtualization.ui
    14_frozen_rows/example.cpp
    14_frozen_rows/frozen_rows.ui
    15_drag_drop/example.cpp
    15_drag_drop/drag_drop.ui
    16_drag_animation/example.cpp
    16_drag_animation/drag_animation.ui
    17_pinning/example.cpp
    17_pinning/pinning.ui
    18_diagnostics/example.cpp
    18_diagnostics/diagnostics.ui
    20_borders/example.cpp
    20_borders/borders.ui
    21_auto_height/example.cpp
    21_auto_height/auto_height.ui
    22_colors/example.cpp
    22_colors/colors.ui
    23_editors/example.cpp
    23_editors/editors.ui
    23_editors/popup.ui
)

set_target_properties(list_capabilities PROPERTIES AUTOUIC ON)

target_link_libraries(list_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(_viv_demo_smooth_enabled AND TARGET SmoothScrollbar::SmoothScrollbar)
    target_sources(list_capabilities PRIVATE 19_smooth_scrolling/example.cpp 19_smooth_scrolling/smooth_scrolling.ui)
    target_link_libraries(list_capabilities PRIVATE SmoothScrollbar::SmoothScrollbar)
    target_compile_definitions(list_capabilities PRIVATE VIV_LIST_HAS_SMOOTH_SCROLLBAR=1)
endif()
if(MSVC)
    target_compile_options(list_capabilities PRIVATE /utf-8)
endif()
