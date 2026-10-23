# Reference programs deliberately link only the library and Qt Widgets.
# Each source has its own main(), with no demo launcher or optional packages.
function(viv_add_reference name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
    endif()
    set_property(DIRECTORY APPEND PROPERTY VIV_REFERENCE_TARGETS ${name})
endfunction()

viv_add_reference(ref_tree_basic tree/basic.cpp)
viv_add_reference(ref_tree_branch tree/branch.cpp)
viv_add_reference(ref_tree_binding_context tree/binding_context.cpp)
viv_add_reference(ref_tree_table_rows tree_table/row_widgets.cpp)
viv_add_reference(ref_tree_table_cells tree_table/cell_widgets.cpp)
viv_add_reference(ref_tree_table_binding_context tree_table/binding_context.cpp)
viv_add_reference(ref_flow_basic flow/basic.cpp)
viv_add_reference(ref_masonry_basic masonry/basic.cpp)

get_property(_viv_reference_targets DIRECTORY PROPERTY VIV_REFERENCE_TARGETS)
add_custom_target(viv_reference_examples DEPENDS ${_viv_reference_targets})
