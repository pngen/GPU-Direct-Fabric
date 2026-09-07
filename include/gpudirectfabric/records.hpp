#pragma once

#include "gpudirectfabric/authority.hpp"
#include "gpudirectfabric/backend_handle.hpp"
#include "gpudirectfabric/enums.hpp"
#include "gpudirectfabric/ids.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gpudirectfabric {

// ---------------------------------------------------------------------------
// BufferRecord
// ---------------------------------------------------------------------------
struct BufferRecord {
  BufferId id{};
  BufferGeneration generation{};     // monotonic; advance invalidates dependents
  Authority authority{};             // worker incarnation that owns dynamic evidence
  MemoryDomain domain = MemoryDomain::UNKNOWN;
  std::uint64_t size = 0;
  std::uint64_t alignment = 1;
  DeviceId device{};
  DeviceGeneration device_generation{};
  Locality locality = Locality::UNKNOWN;
  RegistrationState registration_state = RegistrationState::UNREGISTERED;
  RegistrationId registration_id{};
  RegistrationGeneration registration_generation{};
  BufferLifecycle lifecycle = BufferLifecycle::ACTIVE;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  std::uint64_t freshness = 0;
  std::string name;
  std::shared_ptr<BackendHandle> handle;  // transient process-local evidence

  [[nodiscard]] bool refers_to_live_worker() const noexcept {
    return !authority.worker_owned() || authority.valid();
  }
};

// ---------------------------------------------------------------------------
// DeviceRecord
// ---------------------------------------------------------------------------
struct DeviceRecord {
  DeviceId id{};
  DeviceGeneration generation{};
  std::string name;
  std::string compute_capability;
  bool peer_memory_supported = false;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  Authority authority{};
  std::uint64_t freshness = 0;
};

// ---------------------------------------------------------------------------
// EndpointRecord
// ---------------------------------------------------------------------------
struct EndpointRecord {
  EndpointId id{};
  EndpointGeneration generation{};
  EndpointClass endpoint_class = EndpointClass::UNKNOWN;
  std::string name;
  std::string topology_hint;
  bool healthy = false;
  ProviderId provider{};
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  Authority authority{};
  std::uint64_t freshness = 0;
  std::shared_ptr<BackendHandle> handle;  // transient process-local evidence
};

// ---------------------------------------------------------------------------
// NicRecord
// ---------------------------------------------------------------------------
struct NicRecord {
  NicId id{};
  NicGeneration generation{};
  std::string name;
  std::string locator;
  bool rdma_capable = false;
  bool direct_path_capable = false;
  ProviderId provider{};
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  Authority authority{};
  std::uint64_t freshness = 0;
};

// ---------------------------------------------------------------------------
// StorageEndpointRecord
// ---------------------------------------------------------------------------
struct StorageEndpointRecord {
  StorageEndpointId id{};
  StorageGeneration generation{};
  std::string name;
  std::string backend_name;
  std::string path;
  bool direct_path_capable = false;
  ProviderId provider{};
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  Authority authority{};
  std::uint64_t freshness = 0;
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
struct Registration {
  RegistrationId id{};
  RegistrationGeneration generation{};
  BufferId buffer{};
  BufferGeneration buffer_generation{};
  EndpointId endpoint{};
  EndpointGeneration endpoint_generation{};
  ProviderId provider{};
  Authority authority{};
  RegistrationState state = RegistrationState::UNREGISTERED;
  Support support = Support::UNKNOWN;
  Provenance provenance = Provenance::SYNTHETIC_FIXTURE;
  bool duplicated = false;               // idempotent re-registration flag
  std::uint64_t freshness = 0;
  std::vector<std::string> required_capabilities;  // snapshot at registration time
  std::shared_ptr<BackendHandle> handle;           // transient process-local evidence
};

}  // namespace gpudirectfabric
