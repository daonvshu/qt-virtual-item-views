#pragma once

#include <QtGlobal>

/// Symbol visibility of every public class and free function
/// (see docs/abi.md).
///
/// Three cases, driven by the build system:
///  - the library itself defines `VIRTUALITEMVIEWS_LIBRARY` while it is built as
///    a shared library, so its symbols are exported,
///  - a consumer of a *shared* build sees neither macro and gets the matching
///    `import` decoration (no macro to pass, `find_package(VirtualItemViews)`
///    takes care of it),
///  - a consumer of a *static* build gets `VIRTUALITEMVIEWS_STATIC` (carried as
///    an `INTERFACE_COMPILE_DEFINITIONS` of the exported target), because a
///    static library must not be imported with `__declspec(dllimport)`.
#if defined(VIRTUALITEMVIEWS_STATIC)
#  define VIRTUALITEMVIEWS_EXPORT
#elif defined(VIRTUALITEMVIEWS_LIBRARY)
#  define VIRTUALITEMVIEWS_EXPORT Q_DECL_EXPORT
#else
#  define VIRTUALITEMVIEWS_EXPORT Q_DECL_IMPORT
#endif

#if defined(_MSC_VER) && defined(VIRTUALITEMVIEWS_LIBRARY)
// The exported classes keep their Qt containers and std::function members
// private and never hand their symbols across the boundary (they are only
// touched inside the library), so C4251 is noise here. Scoped to the library
// build, so including these headers in a consumer stays unaffected. The layout
// rules behind that claim are in docs/abi.md.
#  pragma warning(disable : 4251)
#endif
