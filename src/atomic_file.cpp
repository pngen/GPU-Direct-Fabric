#include "gpudirectfabric/atomic_file.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gpudirectfabric {
namespace detail {
bool atomic_replace_file(const char* src, const char* dst) noexcept {
  return ::MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
}  // namespace detail
}  // namespace gpudirectfabric
#else
#include <cstdio>
#include <cstring>

namespace gpudirectfabric {
namespace detail {
bool atomic_replace_file(const char* src, const char* dst) noexcept {
  std::remove(dst);
  return std::rename(src, dst) == 0;
}
}  // namespace detail
}  // namespace gpudirectfabric
#endif
