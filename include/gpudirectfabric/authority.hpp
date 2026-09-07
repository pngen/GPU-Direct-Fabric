#pragma once

#include "gpudirectfabric/ids.hpp"

#include <compare>

namespace gpudirectfabric {

// An explicit execution/evidence authority. Dynamic evidence that originates
// from a worker is fenced by the worker incarnation (WorkerId + WorkerBootId).
// The coordinator epoch fences evidence across coordinator restarts.
struct Authority {
  WorkerId worker{};            // nil when the coordinator itself owns the evidence
  WorkerBootId boot{};          // incarnation of the worker (0 means "not worker-owned")
  CoordinatorEpoch epoch{};     // coordinator epoch under which evidence was produced

  [[nodiscard]] constexpr bool worker_owned() const noexcept { return worker.valid(); }
  [[nodiscard]] bool valid() const noexcept { return boot.value() != 0; }

  friend constexpr bool operator==(const Authority& a, const Authority& b) noexcept = default;
  friend constexpr auto operator<=>(const Authority& a, const Authority& b) noexcept = default;
};

// A coordinator-originated (process-local) authority token for buffers it creates.
constexpr Authority coordinator_authority() noexcept { return Authority{}; }

}  // namespace gpudirectfabric
