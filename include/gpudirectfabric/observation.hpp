#pragma once

#include "gpudirectfabric/authority.hpp"
#include "gpudirectfabric/enums.hpp"
#include "gpudirectfabric/ids.hpp"

#include <string>

namespace gpudirectfabric {

// A capability observation records whether a specific capability is currently
// observed and under what support classification and provenance. It is always a
// point-in-time observation, never a durable claim on its own.
struct CapabilityObservation {
  CapabilityObservationId id{};
  std::string provider;         // e.g. "cuda", "verbs", "synthetic"
  std::string capability;       // e.g. "peer_memory", "gpu_registered_memory", "direct_nic"
  std::string value;            // e.g. "true", "false", "sm_120"
  Support support = Support::UNKNOWN;  // always explicit
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  Authority authority{};
  uint64_t freshness = 0;       // monotone, managed by the coordinator

  [[nodiscard]] bool enabled() const noexcept { return value == "true"; }
};

}  // namespace gpudirectfabric
