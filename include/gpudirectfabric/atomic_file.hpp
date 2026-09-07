#pragma once

namespace gpudirectfabric {
namespace detail {

// Atomically replace dst with src (the source is a fully-written temp file).
// Returns false on failure. Windows uses MoveFileEx(REPLACE_EXISTING).
bool atomic_replace_file(const char* src, const char* dst) noexcept;

}  // namespace detail
}  // namespace gpudirectfabric
