// Copyright 2015 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Module Overview: Starboard Configuration module
//
// Provides a description of the current platform in lurid detail so that
// common code never needs to actually know what the current operating system
// and architecture are.
//
// It is both very pragmatic and canonical in that if any application code
// finds itself needing to make a platform decision, it should always define
// a Starboard Configuration feature instead. This implies the continued
// existence of very narrowly-defined configuration features, but it retains
// porting control in Starboard.

#ifndef STARBOARD_CONFIGURATION_H_
#define STARBOARD_CONFIGURATION_H_

#include "build/build_config.h"

#if !BUILDFLAG(IS_COBALT)
#error "IS_COBALT should be defined while building this file."
#endif

// --- Common Defines --------------------------------------------------------

// The minimum Starboard API version supported.
#define SB_MINIMUM_API_VERSION 18

// The maximum Starboard API version supported.
#define SB_MAXIMUM_API_VERSION 18

// --- Common Detected Features ----------------------------------------------

#if defined(__GNUC__)
#define SB_IS_COMPILER_GCC 1
#endif

// --- Common Helper Macros --------------------------------------------------

// Determines a compile-time capability of the system.
#define SB_CAN(SB_FEATURE) \
  ((defined SB_CAN_##SB_FEATURE) && SB_CAN_##SB_FEATURE)

// Determines at compile-time whether this platform has a standard feature or
// header available.
#define SB_HAS(SB_FEATURE) \
  ((defined SB_HAS_##SB_FEATURE) && SB_HAS_##SB_FEATURE)

// Determines at compile-time an inherent aspect of this platform.
#define SB_IS(SB_FEATURE) ((defined SB_IS_##SB_FEATURE) && SB_IS_##SB_FEATURE)

// Determines at compile-time whether this platform has a quirk.
#define SB_HAS_QUIRK(SB_FEATURE) \
  ((defined SB_HAS_QUIRK_##SB_FEATURE) && SB_HAS_QUIRK_##SB_FEATURE)

// Determines at compile-time the size of a data type, or 0 if the data type
// that was specified was invalid.
#define SB_SIZE_OF(DATATYPE) \
  ((defined SB_SIZE_OF_##DATATYPE) ? (SB_SIZE_OF_##DATATYPE) : (0))

// A constant expression that evaluates to the size_t size of a statically-sized
// array.
#define SB_ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

// A constant expression that evaluates to the int size of a statically-sized
// array.
#if defined(__cplusplus)
#define SB_ARRAY_SIZE_INT(array) static_cast<int>(SB_ARRAY_SIZE(array))
#else
#define SB_ARRAY_SIZE_INT(array) \
  ((int)(SB_ARRAY_SIZE(array)))  // NOLINT(readability/casting)
#endif

// Will cause a compiler error with |msg| if |expr| is false. |msg| must be a
// valid identifier, and must be a unique type in the scope of the declaration.
#if defined(__cplusplus)
extern "C++" {
namespace starboard {
template <bool>
struct CompileAssert {};
}  // namespace starboard
}  // extern "C++"

#define SB_COMPILE_ASSERT(expr, msg)                              \
  typedef ::starboard::CompileAssert<(                            \
      bool(expr))>              /* NOLINT(readability/casting) */ \
      msg[bool(expr) ? 1 : -1]  // NOLINT(readability/casting)
#else
#define SB_COMPILE_ASSERT(expr, msg) \
  extern char _COMPILE_ASSERT_##msg[(expr) ? 1 : -1]
#endif

// --- Platform Configuration ------------------------------------------------

// Include the platform-specific configuration. This macro is set by GN in
// starboard/build/config/BUILD.gn and passed in on the command line for all
// targets and all configurations.
#include STARBOARD_CONFIGURATION_INCLUDE

// --- Overridable Helper Macros ---------------------------------------------

// The following macros can be overridden in STARBOARD_CONFIGURATION_INCLUDE

// Tells the compiler a function is using a printf-style format string.
// |format_param| is the one-based index of the format string parameter;
// |dots_param| is the one-based index of the "..." parameter.
// For v*printf functions (which take a va_list), pass 0 for dots_param.
// (This is undocumented but matches what the system C headers do.)
// (Partially taken from base/compiler_specific.h)
#if !defined(SB_PRINTF_FORMAT)
#if SB_IS(COMPILER_GCC)
#define SB_PRINTF_FORMAT(format_param, dots_param) \
  __attribute__((format(printf, format_param, dots_param)))
#else
#define SB_PRINTF_FORMAT(format_param, dots_param)
#endif
#endif  // SB_PRINTF_FORMAT

// Deprecated: Per the Google C++ Style Guide, comment out unused parameter
// names (e.g., `void* /*context*/` or `Type /*param_name*/`), or use
// `[[maybe_unused]]` if conditionally unused.
// Trivially references a parameter that is otherwise unreferenced, preventing a
// compiler warning on some platforms.
#if !defined(SB_UNREFERENCED_PARAMETER)
#if SB_IS(COMPILER_MSVC)
#define SB_UNREFERENCED_PARAMETER(x) (x)
#else
#define SB_UNREFERENCED_PARAMETER(x) \
  do {                               \
    (void)(x);                       \
  } while (0)
#endif
#endif  // SB_UNREFERENCED_PARAMETER

// Causes the annotated (at the end) function to generate a warning if the
// result is not accessed.
#if !defined(SB_WARN_UNUSED_RESULT)
#if defined(COMPILER_GCC)
#define SB_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define SB_WARN_UNUSED_RESULT
#endif  // COMPILER_GCC
#endif  // SB_WARN_UNUSED_RESULT

// Declare numeric literals of signed 64-bit type.
#if !defined(SB_INT64_C)
#if defined(_MSC_VER)
#define SB_INT64_C(x) x##I64
#else  // defined(_MSC_VER)
#define SB_INT64_C(x) x##LL
#endif  // defined(_MSC_VER)
#endif  // SB_INT64_C

// Declare numeric literals of unsigned 64-bit type.
#if !defined(SB_UINT64_C)
#if defined(_MSC_VER)
#define SB_UINT64_C(x) x##UI64
#else  // defined(_MSC_VER)
#define SB_UINT64_C(x) x##ULL
#endif  // defined(_MSC_VER)
#endif  // SB_INT64_C

// SB_DEPRECATED(int Foo(int bar));
//   Annotates the function as deprecated, which will trigger a compiler
//   warning when referenced.
#if !defined(SB_DEPRECATED)
#if SB_IS(COMPILER_GCC)
#define SB_DEPRECATED(FUNC) FUNC __attribute__((deprecated))
#elif SB_IS(COMPILER_MSVC)
#define SB_DEPRECATED(FUNC) __declspec(deprecated) FUNC
#else
// Empty definition for other compilers.
#define SB_DEPRECATED(FUNC) FUNC
#endif
#endif  // SB_DEPRECATED

// SB_DEPRECATED_EXTERNAL(...) annotates the function as deprecated for
// external clients, but not deprecated for starboard.
#if defined(STARBOARD_IMPLEMENTATION)
#define SB_DEPRECATED_EXTERNAL(FUNC) FUNC
#else
#define SB_DEPRECATED_EXTERNAL(FUNC) SB_DEPRECATED(FUNC)
#endif

// Macro to annotate a function as noreturn, which signals to the compiler
// that the function cannot return.
#if !defined(SB_NORETURN)
#if SB_IS(COMPILER_GCC)
#define SB_NORETURN __attribute__((__noreturn__))
#elif SB_IS(COMPILER_MSVC)
#define SB_NORETURN __declspec(noreturn)
#else
// Empty definition for other compilers.
#define SB_NORETURN
#endif
#endif  // SB_NORETURN

// Specifies the alignment for a class, struct, union, enum, class/struct field,
// or stack variable.
#if !defined(SB_ALIGNAS)
#if SB_IS(COMPILER_GCC)
#define SB_ALIGNAS(byte_alignment) __attribute__((aligned(byte_alignment)))
#elif SB_IS(COMPILER_MSVC)
#define SB_ALIGNAS(byte_alignment) __declspec(align(byte_alignment))
#else
// Fallback to the C++11 form.
#define SB_ALIGNAS(byte_alignment) alignas(byte_alignment)
#endif
#endif  // !defined(SB_ALIGNAS)

// Returns the alignment required for any instance of the type indicated by
// |type|.
#if !defined(SB_ALIGNOF)
#if SB_IS(COMPILER_GCC)
#define SB_ALIGNOF(type) __alignof__(type)
#elif SB_IS(COMPILER_MSVC)
#define SB_ALIGNOF(type) (sizeof(type) - sizeof(type) + __alignof(type))
#else
// Fallback to the C++11 form.
#define SB_ALIGNOF(type) alignof(type)
#endif
#endif  // !defined(SB_ALIGNOF)

// --- Configuration Audits --------------------------------------------------

#if !defined(SB_API_VERSION)
#error "SB_API_VERSION was not defined by your platform."
#endif

#if SB_API_VERSION > SB_MAXIMUM_API_VERSION
#error "Your platform's SB_API_VERSION > SB_MAXIMUM_API_VERSION."
#endif

#if SB_API_VERSION < SB_MINIMUM_API_VERSION
#error "Your platform's SB_API_VERSION < SB_MINIMUM_API_VERSION."
#endif

#if !SB_IS(ARCH_ARM) && !SB_IS(ARCH_ARM64) && !SB_IS(ARCH_X86) && \
    !SB_IS(ARCH_X64)
#error "Your platform doesn't define a known architecture."
#endif

#if SB_IS(32_BIT) == SB_IS(64_BIT)
#error "Your platform must be exactly one of { 32-bit, 64-bit }."
#endif

#if !SB_IS(BIG_ENDIAN) && !SB_IS(LITTLE_ENDIAN) || \
    (SB_IS(BIG_ENDIAN) == SB_IS(LITTLE_ENDIAN))
#error "Your platform's endianness must be defined as big or little."
#endif

#if (SB_SIZE_OF(POINTER) != 4) && (SB_SIZE_OF(POINTER) != 8)
#error "Your platform's pointer sizes must be either 32 bit or 64 bit."
#endif

#if (SB_SIZE_OF(LONG) != 4) && (SB_SIZE_OF(LONG) != 8)
#error "Your platform's long size must be either 32 bit or 64 bit."
#endif

#if SB_IS(WCHAR_T_UTF16) == SB_IS(WCHAR_T_UTF32)
#error "You must define either SB_IS_WCHAR_T_UTF16 or SB_IS_WCHAR_T_UTF32."
#endif

#if defined(SB_IS_WCHAR_T_SIGNED) && defined(SB_IS_WCHAR_T_UNSIGNED)
#error "You can't define SB_IS_WCHAR_T_SIGNED and SB_IS_WCHAR_T_UNSIGNED."
#endif

// SB_C_FORCE_INLINE annotation for forcing a C function to be inlined.
#if defined(SB_C_FORCE_INLINE)
#error "Your platform must not define SB_C_FORCE_INLINE"
#else  // defined(SB_C_FORCE_INLINE)
#if SB_IS(COMPILER_GCC)
#define SB_C_FORCE_INLINE inline __attribute__((always_inline))
#elif SB_IS(COMPILER_MSVC)
#define SB_C_FORCE_INLINE __forceinline
#else  // Fallback to standard keyword with no enforcing
#define SB_C_FORCE_INLINE inline
#endif
#endif  // defined(SB_C_FORCE_INLINE)

// SB_EXPORT_PLATFORM annotates symbols as exported from shared libraries.
#if !defined(SB_EXPORT_PLATFORM)  // auto-configure
#if SB_IS(COMPILER_GCC)
#define SB_EXPORT_PLATFORM __attribute__((visibility("default")))
#elif SB_IS(COMPILER_MSVC)
#define SB_EXPORT_PLATFORM __declspec(dllexport)
#else  // Fallback to standard keyword with no enforcing
#error "Could not determine compiler, you must define SB_EXPORT_PLATFORM."
#endif
#endif  // !defined(SB_EXPORT_PLATFORM)

// SB_IMPORT_PLATFORM annotates symbols as imported from shared libraries.
#if !defined(SB_IMPORT_PLATFORM)  // auto-configure
#if SB_IS(COMPILER_GCC)
#define SB_IMPORT_PLATFORM
#elif SB_IS(COMPILER_MSVC)
#define SB_IMPORT_PLATFORM __declspec(dllimport)
#else  // Fallback to standard keyword with no enforcing
#error "Could not determine compiler, you must define SB_IMPORT_PLATFORM."
#endif
#endif  // !defined(SB_IMPORT_PLATFORM)

// Whether we use __PRETTY_FUNCTION__ or __FUNCTION__ for logging.
#if SB_IS(COMPILER_GCC)
#define SB_FUNCTION __PRETTY_FUNCTION__
#else
#define SB_FUNCTION __FUNCTION__
#endif

#endif  // STARBOARD_CONFIGURATION_H_
