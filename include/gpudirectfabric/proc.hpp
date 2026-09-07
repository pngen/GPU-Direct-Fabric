#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gpudirectfabric {
namespace detail {

struct ProcHandle {
  std::uint64_t pid = 0;
  void* handle = nullptr;
  [[nodiscard]] bool valid() const { return pid != 0 && handle != nullptr; }
};

// Spawn exe with args (args exclude the exe path). Returns a handle + pid.
ProcHandle spawn_process(const std::string& exe, const std::vector<std::string>& args);

// Terminate a spawned process (real OS termination) and wait for it to exit.
bool terminate_process(const ProcHandle& p);

}  // namespace detail
}  // namespace gpudirectfabric
