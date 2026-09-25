cmake_minimum_required(VERSION 3.16)

project(VirtualItemViewsConsumer
    DESCRIPTION "Consumes the installed VirtualItemViews package"
    LANGUAGES CXX)

# This project intentionally knows nothing about the library's sources or its
# build tree: it consumes exactly what `cmake --install` produced, so it is the
# check that the exported package (headers, library, CMake config, include dirs,
# compile definitions and Qt dependency) is complete. See README "安装与消费".
#
# Qt is not found here on purpose: the exported target declares Qt Core/Gui/
# Widgets as PUBLIC dependencies and VirtualItemViewsConfig.cmake re-finds them
# with find_dependency(), so a consumer only has to point CMAKE_PREFIX_PATH at
# the install prefix (and at the Qt kit).
find_package(VirtualItemViews REQUIRED)

add_executable(viv_consumer main.cpp)
target_link_libraries(viv_consumer PRIVATE VirtualItemViews::VirtualItemViews)

if(MSVC)
    # Sources carry UTF-8 comments; without this the compiler falls back to the
    # system code page.
    target_compile_options(viv_consumer PRIVATE /utf-8)
endif()
