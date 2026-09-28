cmake_minimum_required(VERSION 3.21)

project(SmoothScrollTableExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 5.15.2 REQUIRED COMPONENTS Widgets)
find_package(VirtualItemViews CONFIG REQUIRED)
find_package(SmoothScrollbar CONFIG REQUIRED)

add_executable(smooth_scroll_table main.cpp)
target_link_libraries(smooth_scroll_table PRIVATE
    Qt5::Widgets
    VirtualItemViews::VirtualItemViews
    SmoothScrollbar::SmoothScrollbar)

if(MSVC)
    target_compile_options(smooth_scroll_table PRIVATE /utf-8)
endif()
