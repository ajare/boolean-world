#pragma once

// Platform settings - based off OGRE3D (www.ogre3d.org)
#define BW__PLATFORM_WINDOWS 1
#define BW__PLATFORM_LINUX 2
#define BW__PLATFORM_APPLE 3

#define BW__COMPILER_MSVC 1
#define BW__COMPILER_GNUC 2
#define BW__COMPILER_BORL 3

// Find compiler information
#if defined(_MSC_VER)
#define BW__COMPILER BW__COMPILER_MSVC
#define BW__COMP_VER _MSC_VER
#elif defined(__GNUC__)
#define BW__COMPILER BW__COMPILER_GNUC
#define BW__COMP_VER (((__GNUC__) * 100) +    \
                      (__GNUC_MINOR__ * 10) + \
                      __GNUC_PATCHLEVEL__)
#elif defined(__BORLANDC__)
#define BW__COMPILER BW__COMPILER_BORL
#define BW__COMP_VER __BCPLUSPLUS__
#else
#pragma error "Unknown compiler."
#endif

// Set platform
#if defined(_WIN32)
#define BW__PLATFORM BW__PLATFORM_WINDOWS
#elif defined(__APPLE_CC__)
#define BW__PLATFORM BW__PLATFORM_APPLE
#else
#define BW__PLATFORM BW__PLATFORM_LINUX
#endif

// DLL Export
#if BW__PLATFORM == BW__PLATFORM_WINDOWS
#if defined(BW__DLL_EXPORT)
#define BW_API __declspec(dllexport)
#else
#define BW_API
#endif
#elif BW__PLATFORM == BW__PLATFORM_LINUX
#if defined(BW__DLL_EXPORT)
#define BW_API __attribute__((visibility("default")))
#else
#define BW_API
#endif
#endif

// Ok, because only occurs on non-public STL members
#if BW__PLATFORM == BW__PLATFORM_WINDOWS
#pragma warning(disable : 4251)
#endif

#if _MSC_VER >= 1930
#include <format>
#define STR_FORMAT std::format
#else
#include <fmt/format.h>
#define STR_FORMAT fmt::format
#endif

#define BW_UNUSED(x) (void)(x)