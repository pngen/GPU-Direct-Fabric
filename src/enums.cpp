#include "gpudirectfabric/enums.hpp"

#include <cstring>

namespace gpudirectfabric {

// ---- MemoryDomain ----
#define GDF_MD_X(X) X(HOST_PAGEABLE) X(HOST_PINNED) X(CUDA_DEVICE) X(CUDA_MANAGED) X(PEER_DEVICE) \
  X(REGISTERED_GPU_MEMORY) X(REGISTERED_HOST_MEMORY) X(STORAGE_VISIBLE_BUFFER) X(NIC_VISIBLE_BUFFER) X(UNKNOWN)
#define GDF_MD_CASE(x) case MemoryDomain::x: return #x;
#define GDF_MD_IF(x) if (s == #x) { out = MemoryDomain::x; return true; }
const char* to_string(MemoryDomain v) noexcept { switch (v) { GDF_MD_X(GDF_MD_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, MemoryDomain& out) noexcept { GDF_MD_X(GDF_MD_IF) return false; }
#undef GDF_MD_X
#undef GDF_MD_CASE
#undef GDF_MD_IF

// ---- Provenance ----
#define GDF_PV_X(X) X(CUDA_RUNTIME) X(CUDA_DRIVER) X(NVML) X(OPERATING_SYSTEM) X(RDMA_PROVIDER) X(VERBS) \
  X(DMA_BUF) X(STORAGE_BACKEND) X(NVIDIA_DRIVER) X(BENCHMARK) X(PERSISTED) X(DERIVED) X(SYNTHETIC_FIXTURE)
#define GDF_PV_CASE(x) case Provenance::x: return #x;
#define GDF_PV_IF(x) if (s == #x) { out = Provenance::x; return true; }
const char* to_string(Provenance v) noexcept { switch (v) { GDF_PV_X(GDF_PV_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, Provenance& out) noexcept { GDF_PV_X(GDF_PV_IF) return false; }
#undef GDF_PV_X
#undef GDF_PV_CASE
#undef GDF_PV_IF

// ---- Support ----
#define GDF_SP_X(X) X(REAL) X(SYNTHETIC) X(UNSUPPORTED) X(UNKNOWN)
#define GDF_SP_CASE(x) case Support::x: return #x;
#define GDF_SP_IF(x) if (s == #x) { out = Support::x; return true; }
const char* to_string(Support v) noexcept { switch (v) { GDF_SP_X(GDF_SP_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, Support& out) noexcept { GDF_SP_X(GDF_SP_IF) return false; }
#undef GDF_SP_X
#undef GDF_SP_CASE
#undef GDF_SP_IF

// ---- RegistrationState ----
#define GDF_RS_X(X) X(UNREGISTERED) X(REGISTERING) X(REGISTERED) X(REVALIDATION_REQUIRED) X(DEREGISTERING) \
  X(RETIRED) X(FAILED) X(UNSUPPORTED)
#define GDF_RS_CASE(x) case RegistrationState::x: return #x;
#define GDF_RS_IF(x) if (s == #x) { out = RegistrationState::x; return true; }
const char* to_string(RegistrationState v) noexcept { switch (v) { GDF_RS_X(GDF_RS_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, RegistrationState& out) noexcept { GDF_RS_X(GDF_RS_IF) return false; }
#undef GDF_RS_X
#undef GDF_RS_CASE
#undef GDF_RS_IF

// ---- BufferLifecycle ----
#define GDF_BL_X(X) X(ACTIVE) X(RETIRED)
#define GDF_BL_CASE(x) case BufferLifecycle::x: return #x;
#define GDF_BL_IF(x) if (s == #x) { out = BufferLifecycle::x; return true; }
const char* to_string(BufferLifecycle v) noexcept { switch (v) { GDF_BL_X(GDF_BL_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, BufferLifecycle& out) noexcept { GDF_BL_X(GDF_BL_IF) return false; }
#undef GDF_BL_X
#undef GDF_BL_CASE
#undef GDF_BL_IF

// ---- PathEligibility ----
#define GDF_PE_X(X) X(DIRECT_ALLOWED) X(DIRECT_ALLOWED_DEGRADED) X(DIRECT_UNSUPPORTED) X(REGISTRATION_REQUIRED) \
  X(REGISTRATION_STALE) X(ENDPOINT_STALE) X(DEVICE_STALE) X(TOPOLOGY_INCOMPATIBLE) X(PROVIDER_UNAVAILABLE) \
  X(FALLBACK_REQUIRED) X(NO_VALID_PATH) X(REVALIDATION_REQUIRED) X(INSUFFICIENT_EVIDENCE) X(POLICY_REJECTED) \
  X(INVALID_BUFFER) X(UNSUPPORTED)
#define GDF_PE_CASE(x) case PathEligibility::x: return #x;
#define GDF_PE_IF(x) if (s == #x) { out = PathEligibility::x; return true; }
const char* to_string(PathEligibility v) noexcept { switch (v) { GDF_PE_X(GDF_PE_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, PathEligibility& out) noexcept { GDF_PE_X(GDF_PE_IF) return false; }
#undef GDF_PE_X
#undef GDF_PE_CASE
#undef GDF_PE_IF

// ---- PathPolicy ----
#define GDF_PP_X(X) X(DIRECT_ONLY) X(PREFER_DIRECT) X(ALLOW_STAGING) X(BEST_AVAILABLE)
#define GDF_PP_CASE(x) case PathPolicy::x: return #x;
#define GDF_PP_IF(x) if (s == #x) { out = PathPolicy::x; return true; }
const char* to_string(PathPolicy v) noexcept { switch (v) { GDF_PP_X(GDF_PP_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, PathPolicy& out) noexcept { GDF_PP_X(GDF_PP_IF) return false; }
#undef GDF_PP_X
#undef GDF_PP_CASE
#undef GDF_PP_IF

// ---- PathClass ----
#define GDF_PC_X(X) X(GPU_TO_GPU_DIRECT) X(GPU_TO_NIC_DIRECT) X(NIC_TO_GPU_DIRECT) X(GPU_TO_STORAGE_DIRECT) \
  X(STORAGE_TO_GPU_DIRECT) X(GPU_TO_HOST_PINNED) X(HOST_PINNED_TO_GPU) X(GPU_TO_HOST_STAGED_TO_NIC) \
  X(NIC_TO_HOST_STAGED_TO_GPU) X(GPU_TO_HOST_STAGED_TO_STORAGE) X(STORAGE_TO_HOST_STAGED_TO_GPU)
#define GDF_PC_CASE(x) case PathClass::x: return #x;
#define GDF_PC_IF(x) if (s == #x) { out = PathClass::x; return true; }
const char* to_string(PathClass v) noexcept { switch (v) { GDF_PC_X(GDF_PC_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, PathClass& out) noexcept { GDF_PC_X(GDF_PC_IF) return false; }
#undef GDF_PC_X
#undef GDF_PC_CASE
#undef GDF_PC_IF

// ---- EndpointClass ----
#define GDF_EC_X(X) X(NIC) X(STORAGE) X(PEER_DEVICE) X(HOST) X(UNKNOWN)
#define GDF_EC_CASE(x) case EndpointClass::x: return #x;
#define GDF_EC_IF(x) if (s == #x) { out = EndpointClass::x; return true; }
const char* to_string(EndpointClass v) noexcept { switch (v) { GDF_EC_X(GDF_EC_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, EndpointClass& out) noexcept { GDF_EC_X(GDF_EC_IF) return false; }
#undef GDF_EC_X
#undef GDF_EC_CASE
#undef GDF_EC_IF

// ---- TransferState ----
#define GDF_TS_X(X) X(PLANNED) X(AUTHORIZED) X(SUBMITTED) X(IN_FLIGHT) X(COMPLETED) X(VERIFIED) \
  X(FAILED) X(CANCELLED) X(STALE) X(REVALIDATION_REQUIRED)
#define GDF_TS_CASE(x) case TransferState::x: return #x;
#define GDF_TS_IF(x) if (s == #x) { out = TransferState::x; return true; }
const char* to_string(TransferState v) noexcept { switch (v) { GDF_TS_X(GDF_TS_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, TransferState& out) noexcept { GDF_TS_X(GDF_TS_IF) return false; }
#undef GDF_TS_X
#undef GDF_TS_CASE
#undef GDF_TS_IF

// ---- ErrorCode ----
#define GDF_EC2_X(X) X(OK) X(NOT_FOUND) X(UNSUPPORTED) X(INVALID_ARGUMENT) X(INVALID_BUFFER) \
  X(INVALID_MEMORY_DOMAIN) X(REGISTRATION_REQUIRED) X(REGISTRATION_STALE) X(REGISTRATION_FAILED) \
  X(STALE_BUFFER) X(STALE_ENDPOINT) X(STALE_GENERATION) X(STALE_BOOT) X(STALE_EPOCH) X(DIRECT_UNAVAILABLE) \
  X(PROVIDER_UNAVAILABLE) X(TOPOLOGY_INCOMPATIBLE) X(FALLBACK_REQUIRED) X(FALLBACK_DISALLOWED) \
  X(NO_VALID_PATH) X(REVALIDATION_REQUIRED) X(TRANSFER_FAILED) X(TRANSFER_STALE) X(INTEGRITY_FAILURE) \
  X(PROTOCOL_ERROR) X(PERSISTENCE_ERROR) X(RESOURCE_LIMIT) X(CANCELLED)
#define GDF_EC2_CASE(x) case ErrorCode::x: return #x;
#define GDF_EC2_IF(x) if (s == #x) { out = ErrorCode::x; return true; }
const char* to_string(ErrorCode v) noexcept { switch (v) { GDF_EC2_X(GDF_EC2_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, ErrorCode& out) noexcept { GDF_EC2_X(GDF_EC2_IF) return false; }
#undef GDF_EC2_X
#undef GDF_EC2_CASE
#undef GDF_EC2_IF

// ---- Direction ----
#define GDF_DR_X(X) X(GPU_TO_PEER) X(PEER_TO_GPU)
#define GDF_DR_CASE(x) case Direction::x: return #x;
#define GDF_DR_IF(x) if (s == #x) { out = Direction::x; return true; }
const char* to_string(Direction v) noexcept { switch (v) { GDF_DR_X(GDF_DR_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, Direction& out) noexcept { GDF_DR_X(GDF_DR_IF) return false; }
#undef GDF_DR_X
#undef GDF_DR_CASE
#undef GDF_DR_IF

// ---- Locality ----
#define GDF_LC_X(X) X(LOCAL) X(REMOTE) X(UNKNOWN)
#define GDF_LC_CASE(x) case Locality::x: return #x;
#define GDF_LC_IF(x) if (s == #x) { out = Locality::x; return true; }
const char* to_string(Locality v) noexcept { switch (v) { GDF_LC_X(GDF_LC_CASE) default: return "UNKNOWN"; } }
bool from_string(std::string_view s, Locality& out) noexcept { GDF_LC_X(GDF_LC_IF) return false; }
#undef GDF_LC_X
#undef GDF_LC_CASE
#undef GDF_LC_IF

}  // namespace gpudirectfabric