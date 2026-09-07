#pragma once

#include "gpudirectfabric/authority.hpp"
#include "gpudirectfabric/enums.hpp"
#include "gpudirectfabric/ids.hpp"
#include "gpudirectfabric/path.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gpudirectfabric {

// ---------------------------------------------------------------------------
// Transfer plan
// ---------------------------------------------------------------------------
struct TransferStage {
  PathClass path_class = PathClass::GPU_TO_HOST_PINNED;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  BufferId src{};          // source buffer of this stage (may be a staging buffer)
  BufferId dst{};          // destination buffer of this stage (temporary staging buffer)
  EndpointId endpoint{};   // terminal endpoint for the last hop (if any)
  bool direct = false;
  std::string description;
};

struct TransferPlan {
  TransferPlanId id{};
  PathGeneration path_generation{};
  TransferGeneration generation{};
  BufferId source{};
  BufferGeneration source_generation{};
  EndpointId destination{};
  EndpointGeneration destination_generation{};
  RegistrationId registration_id{};
  RegistrationGeneration registration_generation{};
  BufferGeneration registration_buffer_generation{};
  PathPolicy policy = PathPolicy::PREFER_DIRECT;
  std::uint64_t expected_bytes = 0;
  PathEligibility eligibility = PathEligibility::UNSUPPORTED;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  std::uint32_t copy_count = 0;
  std::vector<TransferStage> stages;
  std::vector<RequirementViolation> fallback_reasons;  // why the direct path failed
  std::string explanation;
  CoordinatorEpoch epoch{};
  Authority authority{};
  bool authoritative = true;  // false once superseded by a newer generation
};

// ---------------------------------------------------------------------------
// Transfer attempt
// ---------------------------------------------------------------------------
struct TransferAttempt {
  TransferAttemptId id{};
  TransferPlanId plan_id{};
  TransferGeneration plan_generation{};
  TransferState state = TransferState::PLANNED;
  std::uint64_t bytes = 0;          // completed work (not enqueued)
  CoordinatorEpoch epoch{};
  Authority authority{};
  bool completion_committed = false;
  std::uint64_t submitted_bytes = 0;
  std::string backend_token;
  bool integrity_checked = false;
  bool integrity_passed = false;
  std::string method;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  std::string detail;
};

// ---------------------------------------------------------------------------
// Transfer receipt
// ---------------------------------------------------------------------------
struct TransferReceipt {
  TransferAttemptId attempt_id{};
  TransferGeneration plan_generation{};
  TransferState state = TransferState::FAILED;
  std::uint64_t bytes = 0;
  bool integrity_verified = false;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  std::string method;
  CoordinatorEpoch epoch{};
  std::string detail;
};

// ---------------------------------------------------------------------------
// Verification receipt
// ---------------------------------------------------------------------------
struct VerificationReceipt {
  VerificationId id{};
  TransferAttemptId attempt_id{};
  bool passed = false;
  std::uint64_t bytes_checked = 0;
  Support support = Support::UNKNOWN;
  std::string method;
  std::string expected;
  std::string actual;
};

}  // namespace gpudirectfabric