#pragma once

// GPUDirectFabric export macro.
//
// GPU Direct Fabric is delivered as static archives for all first-party targets,
// so GDF_API expands to nothing. If a consumer chooses to build a shared library
// from these sources, it may define GDF_STATIC_DEFINE to keep symbols internal or
// adopt the DLL import/export pattern below.

#if defined(GDF_STATIC_DEFINE)
#  define GDF_API
#  define GDF_NO_EXPORT
#else
#  if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(GDF_BUILD_SHARED)
#      define GDF_API __declspec(dllexport)
#      define GDF_NO_EXPORT
#    else
#      define GDF_API
#      define GDF_NO_EXPORT
#    endif
#  else
#    if defined(__GNUC__) && defined(GDF_BUILD_SHARED)
#      define GDF_API __attribute__((visibility("default")))
#      define GDF_NO_EXPORT __attribute__((visibility("hidden")))
#    else
#      define GDF_API
#      define GDF_NO_EXPORT
#    endif
#  endif
#endif

#define GDF_VERSION_MAJOR 1
#define GDF_VERSION_MINOR 0
#define GDF_VERSION_PATCH 0
#define GDF_VERSION_STRING "1.0.0"

// The API namespace.
#define GDF_NAMESPACE gpudirectfabric

#define GDF_EXPAND(x) x
#define GDF_STR_(x) #x
#define GDF_STR(x) GDF_STR_(x)
