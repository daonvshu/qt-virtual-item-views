add_executable(bench_listview bench_listview.cpp)
target_link_libraries(bench_listview PRIVATE VirtualItemViews Qt${QT_VERSION_MAJOR}::Widgets)
if(MSVC)
    target_compile_options(bench_listview PRIVATE /utf-8)
endif()

# The benchmark is a standalone program: it is deliberately not registered with
# CTest because its numbers are machine dependent.
