cmake_minimum_required(VERSION 3.16)

project(VirtualItemViews
    VERSION 1.0.0
    DESCRIPTION "Virtualized QWidget item views for Qt Widgets"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Executables land in <build>/bin and libraries in <build>/lib, for the static
# and the shared build alike: a shared library is then found next to the
# executables (Windows does not look into a sibling directory), so CTest and a
# plain `bin/table_spans` run need no PATH juggling. Multi-config generators add
# their own per-config subdirectory below these.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(VIRTUALITEMVIEWS_BUILD_SHARED "Build VirtualItemViews as a shared library" OFF)
option(VIRTUALITEMVIEWS_BUILD_EXAMPLES "Build VirtualItemViews examples" ON)
option(VIRTUALITEMVIEWS_BUILD_TESTS "Build VirtualItemViews tests" ON)
option(VIRTUALITEMVIEWS_BUILD_BENCHMARKS "Build VirtualItemViews benchmarks" ON)
option(VIRTUALITEMVIEWS_BUILD_GUI_TESTS "Build tests that synthesize mouse/keyboard input" ON)

# ---------------------------------------------------------------------------
# Qt: Qt 6.2+ is preferred, Qt 5.15+ is supported as well.
#
# Qt5Config.cmake does not define QT_VERSION_MAJOR, so the major version is
# derived from the targets that were actually found.
# ---------------------------------------------------------------------------
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets)

if(NOT DEFINED QT_VERSION_MAJOR)
    if(TARGET Qt6::Core)
        set(QT_VERSION_MAJOR 6)
    elseif(TARGET Qt5::Core)
        set(QT_VERSION_MAJOR 5)
    else()
        message(FATAL_ERROR "Neither Qt6::Core nor Qt5::Core is available.")
    endif()
endif()

set(VIRTUALITEMVIEWS_QT_PACKAGE "Qt${QT_VERSION_MAJOR}")
set(VIRTUALITEMVIEWS_QT_TARGETS
    "${VIRTUALITEMVIEWS_QT_PACKAGE}::Core"
    "${VIRTUALITEMVIEWS_QT_PACKAGE}::Gui"
    "${VIRTUALITEMVIEWS_QT_PACKAGE}::Widgets")

if(QT_VERSION_MAJOR EQUAL 6)
    set(VIRTUALITEMVIEWS_QT_MINIMUM_VERSION 6.2)
else()
    set(VIRTUALITEMVIEWS_QT_MINIMUM_VERSION 5.15)
endif()

find_package(${VIRTUALITEMVIEWS_QT_PACKAGE} ${VIRTUALITEMVIEWS_QT_MINIMUM_VERSION}
    REQUIRED COMPONENTS Core Gui Widgets)

message(STATUS "VirtualItemViews: building against ${VIRTUALITEMVIEWS_QT_PACKAGE} "
               "(${VIRTUALITEMVIEWS_QT_PACKAGE}_VERSION = ${${VIRTUALITEMVIEWS_QT_PACKAGE}_VERSION})")

set(VIRTUALITEMVIEWS_PUBLIC_HEADERS
    include/virtualitemviews/accessibility.h
    include/virtualitemviews/branchindicator.h
    include/virtualitemviews/global.h
    include/virtualitemviews/headergeometry.h
    include/virtualitemviews/headerwidgetadapter.h
    include/virtualitemviews/itempane.h
    include/virtualitemviews/layoutpolicy.h
    include/virtualitemviews/listlayout.h
    include/virtualitemviews/materializeditem.h
    include/virtualitemviews/nativeheaderview.h
    include/virtualitemviews/scrollmapper.h
    include/virtualitemviews/sizeindex.h
    include/virtualitemviews/tablewidgetadapter.h
    include/virtualitemviews/tablespan.h
    include/virtualitemviews/tablepane.h
    include/virtualitemviews/treevisibilityindex.h
    include/virtualitemviews/types.h
    include/virtualitemviews/virtualitemview.h
    include/virtualitemviews/virtuallistview.h
    include/virtualitemviews/virtualtableview.h
    include/virtualitemviews/virtualheaderview.h
    include/virtualitemviews/virtualtreeview.h
    include/virtualitemviews/widgetadapter.h
    include/virtualitemviews/widgetrecycler.h)

set(VIRTUALITEMVIEWS_SOURCES
    ${VIRTUALITEMVIEWS_PUBLIC_HEADERS}
    src/core/scrollmapper.cpp
    src/core/virtualitemview.cpp
    src/layout/headergeometry.cpp
    src/index/sizeindex.cpp
    src/index/treevisibilityindex.cpp
    src/layout/listlayout.cpp
    src/layout/tablespan.cpp
    src/layout/tablepane.cpp
    src/recycler/widgetrecycler.cpp
    src/widgets/accessibility.cpp
    src/widgets/branchindicator.cpp
    src/widgets/columnhost.cpp
    src/widgets/nativeheaderview.cpp
    src/widgets/virtuallistview.cpp
    src/widgets/virtualtreeview.cpp
    src/widgets/virtualheaderview.cpp
    src/widgets/virtualtableview.cpp)

if(VIRTUALITEMVIEWS_BUILD_SHARED)
    set(VIRTUALITEMVIEWS_LIBRARY_TYPE SHARED)
else()
    set(VIRTUALITEMVIEWS_LIBRARY_TYPE STATIC)
endif()

add_library(VirtualItemViews ${VIRTUALITEMVIEWS_LIBRARY_TYPE} ${VIRTUALITEMVIEWS_SOURCES})
add_library(VirtualItemViews::VirtualItemViews ALIAS VirtualItemViews)

target_compile_features(VirtualItemViews PUBLIC cxx_std_17)
target_include_directories(VirtualItemViews
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(VirtualItemViews PUBLIC ${VIRTUALITEMVIEWS_QT_TARGETS})

if(VIRTUALITEMVIEWS_BUILD_SHARED)
    set_target_properties(VirtualItemViews PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
    # The library itself exports its symbols; consumers of the shared build need
    # no extra define (they get the matching import decoration from global.h).
    target_compile_definitions(VirtualItemViews PRIVATE VIRTUALITEMVIEWS_LIBRARY)
else()
    # A static library must not be consumed with __declspec(dllimport): the
    # define travels with the exported target, so find_package() consumers and
    # in-tree targets (tests, examples, benchmarks) agree with the library.
    target_compile_definitions(VirtualItemViews PUBLIC VIRTUALITEMVIEWS_STATIC)
endif()

if(MSVC)
    target_compile_options(VirtualItemViews PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(VirtualItemViews PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(VIRTUALITEMVIEWS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VIRTUALITEMVIEWS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(VIRTUALITEMVIEWS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Install / export rules
# ---------------------------------------------------------------------------
set(VIRTUALITEMVIEWS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/VirtualItemViews")

# Pre-1.0 nothing was API stable, so a consumer asking for 0.9 must not be handed 0.10.
# From 1.0 on the major version is the compatibility unit for the *source* API, which is
# also what SOVERSION follows. This is a CMake package-version matching policy only: 1.x
# promises source compatibility, not binary ABI (docs/abi.md §1), so a consumer that
# links binaries has to recompile when it upgrades.
if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(VIRTUALITEMVIEWS_PACKAGE_COMPATIBILITY SameMinorVersion)
else()
    set(VIRTUALITEMVIEWS_PACKAGE_COMPATIBILITY SameMajorVersion)
endif()

install(TARGETS VirtualItemViews
    EXPORT VirtualItemViewsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/virtualitemviews
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT VirtualItemViewsTargets
    NAMESPACE VirtualItemViews::
    DESTINATION ${VIRTUALITEMVIEWS_INSTALL_CMAKEDIR})

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/VirtualItemViewsConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ${VIRTUALITEMVIEWS_PACKAGE_COMPATIBILITY})

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VirtualItemViewsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VirtualItemViewsConfig.cmake"
    INSTALL_DESTINATION ${VIRTUALITEMVIEWS_INSTALL_CMAKEDIR})

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VirtualItemViewsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/VirtualItemViewsConfigVersion.cmake"
    DESTINATION ${VIRTUALITEMVIEWS_INSTALL_CMAKEDIR})
