# One process; every chapter owns its model, adapter, widgets and UI.
add_executable(flow_capabilities
    main.cpp
    01_basic/example.cpp
    01_basic/basic.ui
    02_cell_sizes/example.cpp
    02_cell_sizes/cell_sizes.ui
    03_spacing_grid/example.cpp
    03_spacing_grid/spacing_grid.ui
    04_visual_states/example.cpp
    04_visual_states/visual_states.ui
    05_selection_events/example.cpp
    05_selection_events/selection_events.ui
    06_model_root/example.cpp
    06_model_root/model_root.ui
    07_scrolling/example.cpp
    07_scrolling/scrolling.ui
    08_drag_drop/example.cpp
    08_drag_drop/drag_drop.ui
    09_images/example.cpp
    09_images/images.ui
    10_colors/example.cpp
    10_colors/colors.ui
    11_auto_height/example.cpp
    11_auto_height/auto_height.ui
)
set_target_properties(flow_capabilities PROPERTIES AUTOUIC ON)
target_link_libraries(flow_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
# Optional image integration is confined to chapter 09.
target_link_libraries(flow_capabilities PRIVATE viv_demo_images)
if(MSVC)
    target_compile_options(flow_capabilities PRIVATE /utf-8)
endif()
