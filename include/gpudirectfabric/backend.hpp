#pragma once

#include "gpudirectfabric/backend_handle.hpp"
#include "gpudirectfabric/enums.hpp"
#include "gpudirectfabric/ids.hpp"
#include "gpudirectfabric/observation.hpp"
#include "gpudirectfabric/records.hpp"
#include "gpudirectfabric/result.hpp"
#include "gpudirectfabric/transfer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gpudirectfabric {

// Describes what the coordinator is asking a backend to register.
struct RegistrationSpec {
  BufferId buffer{};
  MemoryDomain domain = MemoryDomain::UNKNOWN;
  BufferGeneration buffer_generation{};
  EndpointId endpoint{};
  EndpointClass endpoint_class = EndpointClass::UNKNOWN;
  EndpointGeneration endpoint_generation{};
  std::uint64_t size = 0;
  std::uint64_t alignment = 1;
  Authority authority{};
  Support support = Support::UNKNOWN;  // classification of the underlying resources
};

struct RegisterResult {
  RegistrationId id{};
  RegistrationGeneration generation{};
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  std::shared_ptr<BackendHandle> handle;  // transient process-local evidence
};

// The narrow data-plane execution request. It references a plan and the governed
// accelerator-memory buffer's transient handle; nothing here is durable.
struct TransferRun {
  TransferPlan plan{};
  BufferId accelerator_buffer{};
  std::uint64_t expected_bytes = 0;
  std::shared_ptr<BackendHandle> accelerator_handle;  // transient
};

struct TransferOutcome {
  TransferState state = TransferState::FAILED;
  std::uint64_t bytes = 0;   // completed work (not enqueued)
  bool integrity_checked = false;
  bool integrity_passed = false;
  std::string method;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  std::string detail;
};

// A backend contributes capability + device/endpoint discovery evidence and, for
// the data plane, only the accelerator-memory side of a governed transfer. The
// vendor-neutral core never depends on any concrete backend at compile time.
class Backend {
 public:
  virtual ~Backend() = default;

  virtual std::string name() const = 0;
  virtual Support support() const noexcept = 0;
  virtual Provenance provenance() const noexcept = 0;
  virtual bool available() const noexcept = 0;

  // Static platform capability knowledge (what the platform can support in
  // principle) is separated from dynamic observations.
  virtual std::vector<CapabilityObservation> capabilities() const = 0;
  virtual std::vector<DeviceRecord> devices() const = 0;
  virtual std::vector<EndpointRecord> endpoints() const = 0;
  virtual std::vector<NicRecord> nics() const = 0;
  virtual std::vector<StorageEndpointRecord> storage_endpoints() const = 0;
  virtual std::vector<ProviderInfo> providers() const = 0;

  virtual Result<RegisterResult> register_buffer(const RegistrationSpec& spec) = 0;
  virtual Result<void> deregister_buffer(const Registration& reg) = 0;

  // Narrow data-plane execution. Returns a completed outcome for synchronous
  // backends; asynchronous backends may return an IN_FLIGHT outcome plus a token.
  virtual Result<TransferOutcome> run_transfer(const TransferRun& run) = 0;
};

// Registry of optional backends. Core is vendor-neutral; backends are injected.
class BackendRegistry {
 public:
  BackendRegistry() = default;
  BackendRegistry(const BackendRegistry&) = delete;
  BackendRegistry& operator=(const BackendRegistry&) = delete;

  void add(std::shared_ptr<Backend> backend);
  [[nodiscard]] const Backend* find_by_name(const std::string& name) const noexcept;
  [[nodiscard]] std::vector<std::string> names() const;

  std::vector<std::shared_ptr<Backend>> backends_;  // owned, indexed
};

}  // namespace gpudirectfabric
