#pragma once

#include "gpudirectfabric/backend.hpp"
#include "gpudirectfabric/enums.hpp"
#include "gpudirectfabric/ids.hpp"
#include "gpudirectfabric/observation.hpp"
#include "gpudirectfabric/path.hpp"
#include "gpudirectfabric/records.hpp"
#include "gpudirectfabric/result.hpp"
#include "gpudirectfabric/transfer.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gpudirectfabric {

// ---------------------------------------------------------------------------
// Operation parameter bundles (kept small and explicit).
// ---------------------------------------------------------------------------
struct CreateBufferParams {
  MemoryDomain domain = MemoryDomain::UNKNOWN;
  std::uint64_t size = 0;
  std::uint64_t alignment = 64;
  DeviceId device{};
  Locality locality = Locality::LOCAL;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  Authority authority{};
  std::string name;
};

struct RegisterParams {
  BufferId buffer{};
  EndpointId endpoint{};
  ProviderId provider{};
  Authority authority{};
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  std::vector<std::string> required_capabilities;  // capability requirements for this registration
};

struct EndpointAddParams {
  EndpointClass endpoint_class = EndpointClass::UNKNOWN;
  std::string name;
  std::string topology_hint;
  bool healthy = false;
  ProviderId provider{};
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  Authority authority{};
};

struct DeviceAddParams {
  std::string name;
  std::string compute_capability;
  bool peer_memory_supported = false;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  Authority authority{};
};

struct ProviderAddParams {
  std::string name;
  bool available = false;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
};

struct CapabilityPublishParams {
  std::string provider;
  std::string capability;
  std::string value;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  Authority authority{};
};

struct PlanRequestParams {
  BufferId source{};
  EndpointId destination{};
  Direction direction = Direction::GPU_TO_PEER;
  PathPolicy policy = PathPolicy::PREFER_DIRECT;
  std::uint64_t size = 0;
};

// ---------------------------------------------------------------------------
// Coordinator
// ---------------------------------------------------------------------------
// Owns all governed registries and enforces the authority, generation, and
// liveness invariants. Reads return value snapshots; the internal mutex is never
// held across a call that acquires it again (single-lock discipline).
class Coordinator {
 public:
  explicit Coordinator(CoordinatorEpoch epoch = CoordinatorEpoch{1});
  ~Coordinator();
  Coordinator(const Coordinator&) = delete;
  Coordinator& operator=(const Coordinator&) = delete;

  [[nodiscard]] CoordinatorEpoch epoch() const noexcept;

  // Advance the coordinator epoch: clears live worker authority, marks dynamic
  // registrations/plans as needing revalidation (coordinator restart semantics).
  Result<void> advance_epoch();

  // ---- Bounded resource limits (resource discipline) ----
  void set_limits(std::uint32_t max_buffers, std::uint32_t max_registrations,
                  std::uint32_t max_endpoints, std::uint32_t max_plans,
                  std::uint32_t max_attempts);

  // ---- Worker liveness ----
  Result<void> register_worker(WorkerId worker, WorkerBootId boot, CoordinatorEpoch epoch);
  Result<void> mark_worker_dead(WorkerId worker);
  Result<std::vector<std::pair<WorkerId, WorkerBootId>>> live_workers() const;

  // ---- Buffers ----
  Result<BufferRecord> create_buffer(const CreateBufferParams& p);
  Result<BufferRecord> advance_buffer_generation(BufferId id, const Authority& authority);
  Result<void> retire_buffer(BufferId id, const Authority& authority);
  Result<std::vector<BufferRecord>> buffers() const;
  Result<BufferRecord> get_buffer(BufferId id) const;

  // ---- Devices / endpoints / nics / storage ----
  Result<DeviceRecord> add_device(const DeviceAddParams& p);
  Result<std::vector<DeviceRecord>> devices() const;
  Result<EndpointRecord> add_endpoint(const EndpointAddParams& p);
  Result<EndpointRecord> advance_endpoint_generation(EndpointId id, const Authority& authority);
  Result<void> set_endpoint_health(EndpointId id, bool healthy, const Authority& authority);
  Result<std::vector<EndpointRecord>> endpoints() const;
  Result<NicRecord> add_nic(const NicRecord& rec);
  Result<std::vector<NicRecord>> nics() const;
  Result<StorageEndpointRecord> add_storage_endpoint(const StorageEndpointRecord& rec);
  Result<std::vector<StorageEndpointRecord>> storage_endpoints() const;

  // ---- Providers / capabilities ----
  Result<ProviderInfo> add_provider(const ProviderAddParams& p);
  Result<std::vector<ProviderInfo>> providers() const;
  Result<CapabilityObservation> publish_capability(const CapabilityPublishParams& p);
  Result<void> revoke_capability(const std::string& provider, const std::string& capability,
                                 const Authority& authority);
  Result<std::vector<CapabilityObservation>> capabilities() const;

  // ---- Registration (transactional) ----
  Result<Registration> register_buffer(const RegisterParams& p, Backend* backend);
  Result<void> deregister_buffer(RegistrationId id, const Authority& authority);
  Result<std::vector<Registration>> registrations() const;

  // ---- Path evaluation + planning ----
  Result<PathDecision> evaluate_path(const PlanRequestParams& p);
  Result<TransferPlan> plan_transfer(const PlanRequestParams& p, Backend* backend);

  // ---- Transfer lifecycle ----
  Result<TransferAttempt> begin_transfer(TransferPlanId plan, const Authority& authority,
                                         Backend* backend);
  Result<TransferReceipt> complete_transfer(TransferAttemptId attempt, const Authority& authority);
  Result<TransferReceipt> cancel_transfer(TransferAttemptId attempt, const Authority& authority);
  Result<VerificationReceipt> verify_transfer(TransferAttemptId attempt, bool passed,
                                              std::uint64_t bytes_checked);
  Result<std::vector<TransferPlan>> plans() const;
  Result<std::vector<TransferAttempt>> attempts() const;
  Result<std::vector<TransferReceipt>> receipts() const;
  Result<TransferReceipt> latest_receipt(TransferAttemptId attempt) const;

  // ---- Persistence ----
  Result<void> persist_to_file(const std::string& path);
  static Result<void> recover_from_file(const std::string& path, Coordinator& out);

 private:
  friend class CoordinatorPersistence;

  struct State {
    CoordinatorEpoch epoch{1};
    std::uint64_t next_id = 1;
    std::uint64_t freshness = 1;
    std::unordered_map<std::uint64_t, BufferRecord> buffers;            // key = id.value
    std::unordered_map<std::uint64_t, DeviceRecord> devices;            // key = id.value
    std::unordered_map<std::uint64_t, EndpointRecord> endpoints;        // key = id.value
    std::unordered_map<std::uint64_t, NicRecord> nics;                  // key = id.value (keyed by nic id)
    std::unordered_map<std::uint64_t, StorageEndpointRecord> storage;   // key = id.value
    std::unordered_map<std::uint64_t, Registration> registrations;      // key = id.value
    std::unordered_map<std::uint64_t, ProviderInfo> providers;          // key = id.value
    std::unordered_map<std::uint64_t, CapabilityObservation> capabilities;
    std::unordered_map<std::uint64_t, TransferPlan> plans;              // key = id.value
    std::unordered_map<std::uint64_t, TransferAttempt> attempts;        // key = id.value
    std::unordered_map<std::uint64_t, TransferReceipt> receipts;        // key = attempt id.value
    std::unordered_map<std::uint64_t, VerificationReceipt> verifications;
    std::unordered_map<std::uint64_t, std::pair<WorkerId, WorkerBootId>> workers;
    std::unordered_map<std::uint64_t, std::uint64_t> worker_max_boot;  // survives death (fencing)

    // limits
    std::uint32_t max_buffers = 8192;
    std::uint32_t max_registrations = 8192;
    std::uint32_t max_endpoints = 4096;
    std::uint32_t max_plans = 4096;
    std::uint32_t max_attempts = 4096;
  };

  State state_;
  mutable std::mutex mutex_;

  [[nodiscard]] std::uint64_t next_id_locked();
  [[nodiscard]] std::uint64_t next_freshness_locked();
  void invalidate_buffer_dependents_locked(BufferId buffer);
  void invalidate_endpoint_dependents_locked(EndpointId endpoint);
  [[nodiscard]] bool worker_live_locked(const Authority& a) const;
  [[nodiscard]] PathEvidence build_evidence_locked(const PlanRequestParams& p) const;
};

}  // namespace gpudirectfabric