#pragma once

#include "gpudirectfabric/authority.hpp"
#include "gpudirectfabric/enums.hpp"
#include "gpudirectfabric/ids.hpp"
#include "gpudirectfabric/observation.hpp"
#include "gpudirectfabric/records.hpp"
#include "gpudirectfabric/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gpudirectfabric {

// ---------------------------------------------------------------------------
// Providers
// ---------------------------------------------------------------------------
struct ProviderInfo {
  ProviderId id{};
  std::string name;
  bool available = false;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
};

// ---------------------------------------------------------------------------
// Requirement violation: a machine- and human-readable reason a requirement
// failed. Never flatten requirements into a single boolean.
// ---------------------------------------------------------------------------
struct RequirementViolation {
  std::string requirement;
  bool satisfied = false;
  std::string detail;
};

// ---------------------------------------------------------------------------
// Path candidate
// ---------------------------------------------------------------------------
struct PathCandidate {
  PathClass path_class = PathClass::GPU_TO_HOST_PINNED;
  bool direct = false;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  std::uint32_t stage_count = 1;
  std::string description;
};

// ---------------------------------------------------------------------------
// Path request
// ---------------------------------------------------------------------------
struct PathRequest {
  BufferId source{};          // the governed accelerator-memory buffer
  EndpointId destination{};   // the peer endpoint and/or the peer-side memory buffer
  Direction direction = Direction::GPU_TO_PEER;  // which side the data originates on
  PathPolicy policy = PathPolicy::PREFER_DIRECT;
  std::uint64_t size = 0;
};

// ---------------------------------------------------------------------------
// Path evidence snapshot (built by the coordinator, read-only for the planner)
// ---------------------------------------------------------------------------
struct PathEvidence {
  BufferRecord source;
  EndpointRecord destination;
  DeviceRecord device;
  std::vector<Registration> registrations;
  std::vector<CapabilityObservation> capabilities;
  std::vector<DeviceRecord> devices;
  std::vector<EndpointRecord> endpoints;
  std::vector<ProviderInfo> providers;
  std::vector<std::pair<WorkerId, WorkerBootId>> live_workers;  // current incarnations
  CoordinatorEpoch epoch{};
};

// ---------------------------------------------------------------------------
// Path decision
// ---------------------------------------------------------------------------
struct PathDecision {
  PathEligibility eligibility = PathEligibility::UNSUPPORTED;  // FINAL outcome (fallback-aware)
  PathEligibility direct_eligibility = PathEligibility::UNSUPPORTED;  // raw direct outcome
  PathClass direct_class{};         // the class evaluated for the direct attempt
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  bool degraded = false;
  bool fallback_available = false;
  bool fallback_disallowed = false;
  BufferGeneration source_generation{};               // generations the decision was based on
  EndpointGeneration destination_generation{};
  std::vector<RequirementViolation> violations;       // direct-path requirements that failed
  std::vector<PathCandidate> direct_candidates;       // ordered direct candidates
  std::vector<PathCandidate> fallback_candidates;     // ordered fallback candidates (empty if none)
  std::optional<PathCandidate> selected_fallback;
  std::optional<PathId> selected_path;
  std::string summary;
  std::string direct_rejection;
};

// ---------------------------------------------------------------------------
// Path planner: computes a deterministic PathDecision over a PathEvidence
// snapshot. Pure: same evidence + same policy => same decision.
// ---------------------------------------------------------------------------
class PathPlanner {
 public:
  [[nodiscard]] Result<PathDecision> evaluate(const PathRequest& request,
                                              const PathEvidence& evidence) const noexcept;
};

// Free helpers so the CLI and tests can call them.
[[nodiscard]] const char* path_direction_suffix(PathClass path_class) noexcept;

}  // namespace gpudirectfabric