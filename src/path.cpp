#include "gpudirectfabric/path.hpp"

#include <cstdint>
#include <string>

namespace gpudirectfabric {

namespace {

bool is_gpu_memory(MemoryDomain d) noexcept {
  return d == MemoryDomain::CUDA_DEVICE || d == MemoryDomain::CUDA_MANAGED ||
         d == MemoryDomain::REGISTERED_GPU_MEMORY;
}

bool is_host_pinned(MemoryDomain d) noexcept {
  return d == MemoryDomain::HOST_PINNED || d == MemoryDomain::REGISTERED_HOST_MEMORY;
}

const char* provider_for(EndpointClass cls) noexcept {
  switch (cls) {
    case EndpointClass::NIC: return "rdma";
    case EndpointClass::STORAGE: return "storage";
    case EndpointClass::PEER_DEVICE: return "cuda";
    default: return "host";
  }
}

const char* cap_for(EndpointClass cls) noexcept {
  switch (cls) {
    case EndpointClass::NIC: return "direct_nic";
    case EndpointClass::STORAGE: return "direct_storage";
    case EndpointClass::PEER_DEVICE: return "peer_memory";
    default: return "direct_host";
  }
}

PathClass classify_direct(const PathRequest& req, const PathEvidence& ev) noexcept {
  const auto cls = ev.destination.endpoint_class;
  if (req.direction == Direction::GPU_TO_PEER) {
    switch (cls) {
      case EndpointClass::NIC: return PathClass::GPU_TO_NIC_DIRECT;
      case EndpointClass::STORAGE: return PathClass::GPU_TO_STORAGE_DIRECT;
      case EndpointClass::PEER_DEVICE: return PathClass::GPU_TO_GPU_DIRECT;
      default: return PathClass::GPU_TO_NIC_DIRECT;
    }
  } else {
    switch (cls) {
      case EndpointClass::NIC: return PathClass::NIC_TO_GPU_DIRECT;
      case EndpointClass::STORAGE: return PathClass::STORAGE_TO_GPU_DIRECT;
      case EndpointClass::PEER_DEVICE: return PathClass::GPU_TO_GPU_DIRECT;
      default: return PathClass::NIC_TO_GPU_DIRECT;
    }
  }
}

bool authority_live(const Authority& a, const PathEvidence& ev) noexcept {
  if (a.epoch.value() != ev.epoch.value()) return false;
  if (!a.worker_owned()) return true;
  for (const auto& [w, b] : ev.live_workers) {
    if (w == a.worker && b == a.boot) return true;
  }
  return false;
}

const EndpointRecord* latest_endpoint(const PathEvidence& ev, EndpointId id) noexcept {
  const EndpointRecord* best = nullptr;
  for (const auto& e : ev.endpoints) {
    if (e.id == id && (best == nullptr || e.generation.value() > best->generation.value())) best = &e;
  }
  return best;
}

const DeviceRecord* latest_device(const PathEvidence& ev, DeviceId id) noexcept {
  const DeviceRecord* best = nullptr;
  for (const auto& d : ev.devices) {
    if (d.id == id && (best == nullptr || d.generation.value() > best->generation.value())) best = &d;
  }
  return best;
}

const Registration* find_registration(const PathEvidence& ev, BufferId buffer, EndpointId endpoint) noexcept {
  const Registration* latest = nullptr;
  for (const auto& r : ev.registrations) {
    if (r.buffer == buffer && r.endpoint == endpoint) {
      if (latest == nullptr || r.generation.value() > latest->generation.value()) latest = &r;
    }
  }
  return latest;
}

bool host_staging_available(const PathEvidence& ev) noexcept {
  for (const auto& c : ev.capabilities) {
    if (c.capability == "host_staging" && c.value == "true" && c.support != Support::UNSUPPORTED &&
        c.authority.epoch.value() == ev.epoch.value()) {
      return true;
    }
  }
  return false;
}

bool provider_available(const PathEvidence& ev, const char* provider) noexcept {
  for (const auto& p : ev.providers) {
    if (p.name == provider && p.available && p.support != Support::UNSUPPORTED) return true;
  }
  return false;
}

const CapabilityObservation* capability_for(const PathEvidence& ev, const char* provider,
                                           const char* capability) noexcept {
  for (const auto& c : ev.capabilities) {
    if (c.provider == provider && c.capability == capability) return &c;
  }
  return nullptr;
}

void add_fallback_candidates(PathDecision& d, const PathRequest& req, const PathEvidence& ev) noexcept {
  const auto cls = ev.destination.endpoint_class;
  PathCandidate c;
  c.direct = false;
  c.stage_count = 2;
  c.support = Support::SYNTHETIC;  // pinned-host staging is synthesized here unless real evidence exists
  c.provenance = Provenance::SYNTHETIC_FIXTURE;
  c.description = "pinned-host staging fallback";
  if (req.direction == Direction::GPU_TO_PEER) {
    switch (cls) {
      case EndpointClass::NIC: c.path_class = PathClass::GPU_TO_HOST_STAGED_TO_NIC; break;
      case EndpointClass::STORAGE: c.path_class = PathClass::GPU_TO_HOST_STAGED_TO_STORAGE; break;
      default: c.path_class = PathClass::GPU_TO_HOST_PINNED; break;
    }
  } else {
    switch (cls) {
      case EndpointClass::NIC: c.path_class = PathClass::NIC_TO_HOST_STAGED_TO_GPU; break;
      case EndpointClass::STORAGE: c.path_class = PathClass::STORAGE_TO_HOST_STAGED_TO_GPU; break;
      default: c.path_class = PathClass::HOST_PINNED_TO_GPU; break;
    }
  }
  d.fallback_candidates.push_back(c);
  d.selected_fallback = c;
  d.fallback_available = true;
}

}  // namespace

Result<PathDecision> PathPlanner::evaluate(const PathRequest& request,
                                           const PathEvidence& ev) const noexcept {
  PathDecision d;
  d.direct_class = classify_direct(request, ev);
  d.source_generation = ev.source.generation;
  d.destination_generation = ev.destination.generation;

  const auto add_v = [&](std::string req, bool sat, std::string detail) {
    d.violations.push_back(RequirementViolation{std::move(req), sat, std::move(detail)});
  };

  PathEligibility direct = PathEligibility::UNSUPPORTED;

  // ---- Source buffer ----
  const BufferRecord& src = ev.source;
  if (!src.id.valid()) {
    add_v("source_buffer", false, "source buffer id is nil");
    direct = PathEligibility::INVALID_BUFFER;
  } else if (src.lifecycle != BufferLifecycle::ACTIVE) {
    add_v("source_buffer", false, "source buffer is retired");
    direct = PathEligibility::INVALID_BUFFER;
  } else {
    add_v("source_buffer", true, "source buffer present and active");
  }

  // ---- Size ----
  if (direct == PathEligibility::UNSUPPORTED) {
    if (request.size > src.size) {
      add_v("size", false, "request size exceeds buffer capacity");
      direct = PathEligibility::NO_VALID_PATH;
    } else {
      add_v("size", true, "request size within buffer capacity");
    }
  }

  // ---- Destination ----
  const EndpointRecord& dest = ev.destination;
  if (direct == PathEligibility::UNSUPPORTED) {
    if (!dest.id.valid() || dest.endpoint_class == EndpointClass::UNKNOWN) {
      add_v("destination_endpoint", false, "destination endpoint missing or unclassified");
      direct = PathEligibility::NO_VALID_PATH;
    } else {
      add_v("destination_endpoint", true, "destination endpoint present");
      d.provenance = dest.provenance;
    }
  }
  if (direct == PathEligibility::UNSUPPORTED) {
    if (dest.support == Support::UNSUPPORTED) {
      add_v("destination_supported", false,
            "destination endpoint classified unsupported for direct access");
      direct = PathEligibility::DIRECT_UNSUPPORTED;
      d.support = Support::UNSUPPORTED;
      d.direct_rejection = "destination endpoint does not expose a supported direct path";
    } else {
      add_v("destination_supported", true, "destination endpoint classified as supported");
    }
  }
  if (direct == PathEligibility::UNSUPPORTED) {
    if (!dest.healthy) {
      add_v("destination_healthy", false, "destination endpoint is not healthy");
      direct = PathEligibility::ENDPOINT_STALE;
      d.direct_rejection = "destination endpoint is unhealthy";
    } else {
      add_v("destination_healthy", true, "destination endpoint healthy");
    }
  }
  if (direct == PathEligibility::UNSUPPORTED) {
    const EndpointRecord* latest = latest_endpoint(ev, dest.id);
    if (latest != nullptr && latest->generation != dest.generation) {
      add_v("destination_generation", false, "destination endpoint generation is stale");
      direct = PathEligibility::ENDPOINT_STALE;
      d.direct_rejection = "destination endpoint generation advanced since snapshot";
    } else {
      add_v("destination_generation", true, "destination endpoint generation current");
    }
  }

  // ---- Source domain ----
  const bool gpu_src = is_gpu_memory(src.domain);
  const bool pinned_src = is_host_pinned(src.domain);
  if (direct == PathEligibility::UNSUPPORTED) {
    if (!gpu_src && !pinned_src) {
      add_v("source_memory_domain", false, "source buffer is not accelerator or pinned memory");
      direct = PathEligibility::DIRECT_UNSUPPORTED;
      d.direct_rejection = "source memory domain is not eligible for a governed path";
    } else if (!gpu_src) {
      add_v("source_is_accelerator", false,
            "governed source is host memory; direct accelerator path requires accelerator memory");
      direct = PathEligibility::DIRECT_UNSUPPORTED;
      d.direct_rejection = "governed buffer is host memory, not accelerator memory";
    } else {
      add_v("source_is_accelerator", true, "source is accelerator memory");
      d.support = src.support;
    }
  }

  // ---- Device ----
  if (direct == PathEligibility::UNSUPPORTED) {
    if (!ev.device.id.valid()) {
      add_v("source_device", false, "no device associated with the source");
      direct = PathEligibility::DEVICE_STALE;
    } else {
      const DeviceRecord* dev_latest = latest_device(ev, ev.device.id);
      if (dev_latest != nullptr && dev_latest->generation != ev.device.generation) {
        add_v("source_device", false, "device generation is stale");
        direct = PathEligibility::DEVICE_STALE;
        d.direct_rejection = "device generation advanced since snapshot";
      } else {
        add_v("source_device", true, "device current");
      }
    }
  }

  // ---- Provider availability ----
  const char* provider = provider_for(dest.endpoint_class);
  if (direct == PathEligibility::UNSUPPORTED) {
    if (!provider_available(ev, provider)) {
      add_v("provider_available", false, std::string("provider '") + provider + "' is unavailable");
      direct = PathEligibility::PROVIDER_UNAVAILABLE;
      d.support = Support::UNSUPPORTED;
      d.direct_rejection = std::string("no available provider for ") + provider + " direct path";
    } else {
      add_v("provider_available", true, std::string("provider '") + provider + "' available");
    }
  }

  // ---- Direct capability ----
  const char* req_cap = cap_for(dest.endpoint_class);
  if (direct == PathEligibility::UNSUPPORTED) {
    const CapabilityObservation* cap = capability_for(ev, provider, req_cap);
    if (cap == nullptr || cap->value != "true" || cap->support == Support::UNSUPPORTED) {
      add_v("direct_capability", false, std::string("direct capability '") + req_cap + "' not evidenced as supported");
      direct = PathEligibility::INSUFFICIENT_EVIDENCE;
      d.support = (cap != nullptr) ? cap->support : Support::UNSUPPORTED;
      d.direct_rejection = std::string("direct capability '") + req_cap + "' absent or unsupported";
    } else {
      add_v("direct_capability", true, std::string("direct capability '") + req_cap + "' supported");
      d.support = cap->support;
      d.provenance = cap->provenance;
    }
  }

  // ---- Registration ----
  if (direct == PathEligibility::UNSUPPORTED) {
    const Registration* reg = find_registration(ev, src.id, dest.id);
    if (reg == nullptr) {
      add_v("registration_current", false, "no current registration for buffer+endpoint");
      direct = PathEligibility::REGISTRATION_REQUIRED;
      d.direct_rejection = "buffer is not registered for the destination endpoint";
    } else if (reg->state != RegistrationState::REGISTERED) {
      add_v("registration_current", false, "registration is not in REGISTERED state");
      direct = PathEligibility::REGISTRATION_STALE;
      d.direct_rejection = "registration state is not current";
    } else if (reg->buffer_generation != src.generation || reg->endpoint_generation != dest.generation) {
      add_v("registration_current", false, "registration generation is stale");
      direct = PathEligibility::REGISTRATION_STALE;
      d.direct_rejection = "registration generation no longer matches buffer/endpoint";
    } else if (!authority_live(reg->authority, ev)) {
      add_v("registration_authority", false, "registration authority is not live");
      direct = PathEligibility::REVALIDATION_REQUIRED;
      d.direct_rejection = "registration authority is stale (worker or epoch advanced)";
    } else {
      add_v("registration_current", true, "registration current and authorized");
      d.support = reg->support;
      d.provenance = reg->provenance;
    }
  }

  // ---- Alignment ----
  if (direct == PathEligibility::UNSUPPORTED) {
    if (src.alignment < 64 || (request.size % src.alignment) != 0) {
      add_v("alignment", false, "alignment not supported by direct path");
      direct = PathEligibility::DIRECT_UNSUPPORTED;
      d.direct_rejection = "buffer alignment is unsupported for the direct path";
    } else {
      add_v("alignment", true, "alignment supported");
      direct = (d.support == Support::REAL) ? PathEligibility::DIRECT_ALLOWED
                                            : PathEligibility::DIRECT_ALLOWED_DEGRADED;
    }
  }

  // ---- Outcome ----
  d.direct_eligibility = direct;
  if (direct == PathEligibility::DIRECT_ALLOWED || direct == PathEligibility::DIRECT_ALLOWED_DEGRADED) {
    // Direct selected.
    PathCandidate cand;
    cand.path_class = d.direct_class;
    cand.direct = true;
    cand.stage_count = 1;
    cand.support = d.support;
    cand.provenance = d.provenance;
    cand.description = std::string("direct ") + to_string(d.direct_class) + " (" + to_string(d.support) + ")";
    d.direct_candidates.push_back(cand);
    d.eligibility = direct;
  } else {
    // Direct failed; evaluate policy. A fallback is only valid when the
    // destination endpoint itself is present, classified and healthy, since
    // staging still traverses that same endpoint.
    const bool dest_usable = ev.destination.id.valid() &&
                             ev.destination.healthy &&
                             ev.destination.endpoint_class != EndpointClass::UNKNOWN;
    if (request.policy == PathPolicy::DIRECT_ONLY) {
      d.eligibility = direct;
      d.fallback_disallowed = host_staging_available(ev) && dest_usable;
    } else if (dest_usable && host_staging_available(ev)) {
      d.eligibility = PathEligibility::FALLBACK_REQUIRED;
      add_fallback_candidates(d, request, ev);
    } else {
      // No valid fallback: preserve the specific direct-path rejection reason.
      d.eligibility = direct;
    }
  }

  d.summary = std::string("path ") + to_string(d.direct_class) + "; direct=" +
              to_string(d.direct_eligibility) + "; final=" + to_string(d.eligibility) +
              "; support=" + to_string(d.support);
  d.degraded = (d.direct_eligibility == PathEligibility::DIRECT_ALLOWED_DEGRADED);
  return ok(d);
}

const char* path_direction_suffix(PathClass path_class) noexcept {
  switch (path_class) {
    case PathClass::NIC_TO_GPU_DIRECT:
    case PathClass::STORAGE_TO_GPU_DIRECT:
    case PathClass::HOST_PINNED_TO_GPU:
    case PathClass::NIC_TO_HOST_STAGED_TO_GPU:
    case PathClass::STORAGE_TO_HOST_STAGED_TO_GPU:
      return "to_gpu";
    default:
      return "from_gpu";
  }
}

}  // namespace gpudirectfabric