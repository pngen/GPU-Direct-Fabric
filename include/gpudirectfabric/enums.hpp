#pragma once

#include <string>
#include <string_view>

namespace gpudirectfabric {

// ===========================================================================
// Memory domains
// ===========================================================================
enum class MemoryDomain {
  HOST_PAGEABLE,
  HOST_PINNED,
  CUDA_DEVICE,
  CUDA_MANAGED,
  PEER_DEVICE,
  REGISTERED_GPU_MEMORY,
  REGISTERED_HOST_MEMORY,
  STORAGE_VISIBLE_BUFFER,
  NIC_VISIBLE_BUFFER,
  UNKNOWN
};

const char* to_string(MemoryDomain v) noexcept;
bool from_string(std::string_view s, MemoryDomain& out) noexcept;

// ===========================================================================
// Provenance
// ===========================================================================
enum class Provenance {
  CUDA_RUNTIME,
  CUDA_DRIVER,
  NVML,
  OPERATING_SYSTEM,
  RDMA_PROVIDER,
  VERBS,
  DMA_BUF,
  STORAGE_BACKEND,
  NVIDIA_DRIVER,
  BENCHMARK,
  PERSISTED,
  DERIVED,
  SYNTHETIC_FIXTURE
};

const char* to_string(Provenance v) noexcept;
bool from_string(std::string_view s, Provenance& out) noexcept;

// ===========================================================================
// Support classification (always qualified as REAL / SYNTHETIC / UNSUPPORTED)
// ===========================================================================
enum class Support { REAL, SYNTHETIC, UNSUPPORTED, UNKNOWN };

const char* to_string(Support v) noexcept;
bool from_string(std::string_view s, Support& out) noexcept;

// ===========================================================================
// Registration lifecycle
// ===========================================================================
enum class RegistrationState {
  UNREGISTERED,
  REGISTERING,
  REGISTERED,
  REVALIDATION_REQUIRED,
  DEREGISTERING,
  RETIRED,
  FAILED,
  UNSUPPORTED
};

const char* to_string(RegistrationState v) noexcept;
bool from_string(std::string_view s, RegistrationState& out) noexcept;

// ===========================================================================
// Buffer lifecycle
// ===========================================================================
enum class BufferLifecycle { ACTIVE, RETIRED };

const char* to_string(BufferLifecycle v) noexcept;
bool from_string(std::string_view s, BufferLifecycle& out) noexcept;

// ===========================================================================
// Path eligibility outcomes
// ===========================================================================
enum class PathEligibility {
  DIRECT_ALLOWED,
  DIRECT_ALLOWED_DEGRADED,
  DIRECT_UNSUPPORTED,
  REGISTRATION_REQUIRED,
  REGISTRATION_STALE,
  ENDPOINT_STALE,
  DEVICE_STALE,
  TOPOLOGY_INCOMPATIBLE,
  PROVIDER_UNAVAILABLE,
  FALLBACK_REQUIRED,
  NO_VALID_PATH,
  REVALIDATION_REQUIRED,
  INSUFFICIENT_EVIDENCE,
  POLICY_REJECTED,
  INVALID_BUFFER,
  UNSUPPORTED
};

const char* to_string(PathEligibility v) noexcept;
bool from_string(std::string_view s, PathEligibility& out) noexcept;

// ===========================================================================
// Path policy
// ===========================================================================
enum class PathPolicy { DIRECT_ONLY, PREFER_DIRECT, ALLOW_STAGING, BEST_AVAILABLE };

const char* to_string(PathPolicy v) noexcept;
bool from_string(std::string_view s, PathPolicy& out) noexcept;

// ===========================================================================
// Path classes
// ===========================================================================
enum class PathClass {
  GPU_TO_GPU_DIRECT,
  GPU_TO_NIC_DIRECT,
  NIC_TO_GPU_DIRECT,
  GPU_TO_STORAGE_DIRECT,
  STORAGE_TO_GPU_DIRECT,
  GPU_TO_HOST_PINNED,
  HOST_PINNED_TO_GPU,
  GPU_TO_HOST_STAGED_TO_NIC,
  NIC_TO_HOST_STAGED_TO_GPU,
  GPU_TO_HOST_STAGED_TO_STORAGE,
  STORAGE_TO_HOST_STAGED_TO_GPU
};

const char* to_string(PathClass v) noexcept;
bool from_string(std::string_view s, PathClass& out) noexcept;

// ===========================================================================
// Endpoint classes
// ===========================================================================
enum class EndpointClass { NIC, STORAGE, PEER_DEVICE, HOST, UNKNOWN };

const char* to_string(EndpointClass v) noexcept;
bool from_string(std::string_view s, EndpointClass& out) noexcept;

// ===========================================================================
// Transfer lifecycle
// ===========================================================================
enum class TransferState {
  PLANNED,
  AUTHORIZED,
  SUBMITTED,
  IN_FLIGHT,
  COMPLETED,
  VERIFIED,
  FAILED,
  CANCELLED,
  STALE,
  REVALIDATION_REQUIRED
};

const char* to_string(TransferState v) noexcept;
bool from_string(std::string_view s, TransferState& out) noexcept;

// ===========================================================================
// Error codes (typed, never flattened to strings alone)
// ===========================================================================
enum class ErrorCode {
  OK,
  NOT_FOUND,
  UNSUPPORTED,
  INVALID_ARGUMENT,
  INVALID_BUFFER,
  INVALID_MEMORY_DOMAIN,
  REGISTRATION_REQUIRED,
  REGISTRATION_STALE,
  REGISTRATION_FAILED,
  STALE_BUFFER,
  STALE_ENDPOINT,
  STALE_GENERATION,
  STALE_BOOT,
  STALE_EPOCH,
  DIRECT_UNAVAILABLE,
  PROVIDER_UNAVAILABLE,
  TOPOLOGY_INCOMPATIBLE,
  FALLBACK_REQUIRED,
  FALLBACK_DISALLOWED,
  NO_VALID_PATH,
  REVALIDATION_REQUIRED,
  TRANSFER_FAILED,
  TRANSFER_STALE,
  INTEGRITY_FAILURE,
  PROTOCOL_ERROR,
  PERSISTENCE_ERROR,
  RESOURCE_LIMIT,
  CANCELLED
};

const char* to_string(ErrorCode v) noexcept;
bool from_string(std::string_view s, ErrorCode& out) noexcept;

// ===========================================================================
// Direction of a governed transfer (which side the accelerator memory sits on)
// ===========================================================================
enum class Direction { GPU_TO_PEER, PEER_TO_GPU };

const char* to_string(Direction v) noexcept;
bool from_string(std::string_view s, Direction& out) noexcept;

// ===========================================================================
// Locality
// ===========================================================================
enum class Locality { LOCAL, REMOTE, UNKNOWN };

const char* to_string(Locality v) noexcept;
bool from_string(std::string_view s, Locality& out) noexcept;

}  // namespace gpudirectfabric