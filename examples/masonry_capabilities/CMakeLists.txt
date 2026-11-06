# One process; chapters deliberately keep their models, adapters and widgets local.
add_executable(masonry_capabilities
    main.cpp
    browser.ui
    01_basic/example.cpp
    01_basic/basic.ui
    02_columns/example.cpp
    02_columns/columns.ui
    03_spacing_grid/example.cpp
    03_spacing_grid/spacing_grid.ui
    04_height_modes/example.cpp
    04_height_modes/height_modes.ui
    05_height_updates/example.cpp
    05_height_updates/height_updates.ui
    06_visual_states/example.cpp
    06_visual_states/visual_states.ui
    07_binding_events/example.cpp
    07_binding_events/binding_events.ui
    08_selection/example.cpp
    08_selection/selection.ui
    09_model_root/example.cpp
    09_model_root/model_root.ui
    10_scrolling/example.cpp
    10_scrolling/scrolling.ui
    11_drag_drop/example.cpp
    11_drag_drop/drag_drop.ui
    12_images/example.cpp
    12_images/images.ui
    13_borders_colors/example.cpp
    13_borders_colors/borders_colors.ui
    14_auto_height/example.cpp
    14_auto_height/auto_height.ui
    07_binding_events/card.ui
)
set_target_properties(masonry_capabilities PROPERTIES AUTOUIC ON)
target_link_libraries(masonry_capabilities PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
# Only chapter 12 uses the optional image backend.
target_link_libraries(masonry_capabilities PRIVATE viv_demo_images)
if(MSVC)
    target_compile_options(masonry_capabilities PRIVATE /utf-8)
endif()
