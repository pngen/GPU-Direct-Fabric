#include "gpudirectfabric/coordinator.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace gpudirectfabric {

namespace {

ErrorCode error_for_eligibility(PathEligibility e) noexcept {
  switch (e) {
    case PathEligibility::DIRECT_ALLOWED:
    case PathEligibility::DIRECT_ALLOWED_DEGRADED:
      return ErrorCode::OK;
    case PathEligibility::REGISTRATION_REQUIRED: return ErrorCode::REGISTRATION_REQUIRED;
    case PathEligibility::REGISTRATION_STALE: return ErrorCode::REGISTRATION_STALE;
    case PathEligibility::ENDPOINT_STALE: return ErrorCode::STALE_ENDPOINT;
    case PathEligibility::DEVICE_STALE: return ErrorCode::STALE_GENERATION;
    case PathEligibility::TOPOLOGY_INCOMPATIBLE: return ErrorCode::TOPOLOGY_INCOMPATIBLE;
    case PathEligibility::PROVIDER_UNAVAILABLE: return ErrorCode::PROVIDER_UNAVAILABLE;
    case PathEligibility::NO_VALID_PATH: return ErrorCode::NO_VALID_PATH;
    case PathEligibility::REVALIDATION_REQUIRED: return ErrorCode::REVALIDATION_REQUIRED;
    case PathEligibility::INSUFFICIENT_EVIDENCE: return ErrorCode::DIRECT_UNAVAILABLE;
    case PathEligibility::POLICY_REJECTED: return ErrorCode::FALLBACK_DISALLOWED;
    case PathEligibility::INVALID_BUFFER: return ErrorCode::INVALID_BUFFER;
    default: return ErrorCode::UNSUPPORTED;
  }
}

}  // namespace

Coordinator::Coordinator(CoordinatorEpoch epoch) {
  state_.epoch = epoch;
}

Coordinator::~Coordinator() = default;

CoordinatorEpoch Coordinator::epoch() const noexcept {
  std::lock_guard<std::mutex> guard(mutex_);
  return state_.epoch;
}

void Coordinator::set_limits(std::uint32_t max_buffers, std::uint32_t max_registrations,
                             std::uint32_t max_endpoints, std::uint32_t max_plans,
                             std::uint32_t max_attempts) {
  std::lock_guard<std::mutex> guard(mutex_);
  state_.max_buffers = max_buffers;
  state_.max_registrations = max_registrations;
  state_.max_endpoints = max_endpoints;
  state_.max_plans = max_plans;
  state_.max_attempts = max_attempts;
}

std::uint64_t Coordinator::next_id_locked() { return ++state_.next_id; }
std::uint64_t Coordinator::next_freshness_locked() { return ++state_.freshness; }

bool Coordinator::worker_live_locked(const Authority& a) const {
  if (a.epoch.value() != state_.epoch.value()) return false;
  if (!a.worker_owned()) return true;
  auto it = state_.workers.find(a.worker.value());
  if (it == state_.workers.end()) return false;
  return it->second.second == a.boot;
}

void Coordinator::invalidate_buffer_dependents_locked(BufferId buffer) {
  for (auto& [k, r] : state_.registrations) {
    (void)k;
    if (r.buffer == buffer) r.state = RegistrationState::REVALIDATION_REQUIRED;
  }
  // Supersede plans that reference the old buffer generation.
  for (auto& [k, p] : state_.plans) {
    (void)k;
    if (p.source == buffer) p.authoritative = false;
  }
}

void Coordinator::invalidate_endpoint_dependents_locked(EndpointId endpoint) {
  for (auto& [k, r] : state_.registrations) {
    (void)k;
    if (r.endpoint == endpoint) r.state = RegistrationState::REVALIDATION_REQUIRED;
  }
  for (auto& [k, p] : state_.plans) {
    (void)k;
    if (p.destination == endpoint) p.authoritative = false;
  }
}

// ---------------------------------------------------------------------------
// Worker liveness
// ---------------------------------------------------------------------------
Result<void> Coordinator::register_worker(WorkerId worker, WorkerBootId boot, CoordinatorEpoch epoch) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (epoch.value() != state_.epoch.value()) {
    return err<void>(ErrorCode::STALE_EPOCH, "worker epoch mismatch");
  }
  if (!worker.valid()) {
    return err<void>(ErrorCode::INVALID_ARGUMENT, "invalid worker id");
  }
  auto maxit = state_.worker_max_boot.find(worker.value());
  const std::uint64_t prev_max = (maxit != state_.worker_max_boot.end()) ? maxit->second : 0;
  if (boot.value() < prev_max) {
    return err<void>(ErrorCode::STALE_BOOT, "worker boot id is stale");
  }
  if (prev_max != 0) {
    // A worker incarnation was seen before. If it is live with the same boot,
    // this is an idempotent re-connection. Otherwise, re-creating a dead worker
    // requires a strictly higher boot id (fresh incarnation).
    auto it = state_.workers.find(worker.value());
    if (it != state_.workers.end()) {
      if (it->second.second.value() == boot.value()) return ok();  // idempotent duplicate
      it->second.second = boot;  // incarnation advanced while live
      state_.worker_max_boot[worker.value()] = boot.value();
      return ok();
    }
    if (boot.value() <= prev_max) {
      return err<void>(ErrorCode::STALE_BOOT,
                       "resurrecting a dead worker requires a fresh boot id");
    }
    state_.worker_max_boot[worker.value()] = boot.value();
    state_.workers[worker.value()] = {worker, boot};
    return ok();
  }
  // First time this worker id has been seen.
  state_.worker_max_boot[worker.value()] = boot.value();
  state_.workers[worker.value()] = {worker, boot};
  return ok();
}

Result<void> Coordinator::mark_worker_dead(WorkerId worker) {
  std::lock_guard<std::mutex> guard(mutex_);
  state_.workers.erase(worker.value());
  // Invalidate process-owned dynamic evidence (registrations) for the worker.
  for (auto& [k, r] : state_.registrations) {
    (void)k;
    if (r.authority.worker == worker && r.authority.worker_owned()) {
      r.state = RegistrationState::REVALIDATION_REQUIRED;
    }
  }
  return ok();
}

Result<void> Coordinator::advance_epoch() {
  std::lock_guard<std::mutex> guard(mutex_);
  state_.epoch = state_.epoch.next();
  state_.workers.clear();  // process-local worker authority does not survive a restart
  for (auto& [k, r] : state_.registrations) {
    (void)k;
    if (r.state == RegistrationState::REGISTERED || r.state == RegistrationState::REGISTERING) {
      r.state = RegistrationState::REVALIDATION_REQUIRED;
    }
  }
  for (auto& [k, p] : state_.plans) {
    (void)k;
    p.authoritative = false;
  }
  return ok();
}

Result<std::vector<std::pair<WorkerId, WorkerBootId>>> Coordinator::live_workers() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<std::pair<WorkerId, WorkerBootId>> out;
  out.reserve(state_.workers.size());
  for (const auto& [k, v] : state_.workers) {
    (void)k;
    out.push_back(v);
  }
  return ok(std::move(out));
}

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------
Result<BufferRecord> Coordinator::create_buffer(const CreateBufferParams& p) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (p.size > 0 && p.alignment == 0) {
    return err<BufferRecord>(ErrorCode::INVALID_ARGUMENT, "alignment must be nonzero when size > 0");
  }
  if (state_.buffers.size() >= state_.max_buffers) {
    return err<BufferRecord>(ErrorCode::RESOURCE_LIMIT, "buffer limit reached");
  }
  if (p.authority.worker_owned() && !worker_live_locked(p.authority)) {
    return err<BufferRecord>(ErrorCode::STALE_BOOT, "buffer owner authority is not live");
  }
  BufferRecord b;
  b.id = BufferId(next_id_locked());
  b.generation = BufferGeneration(1);
  b.authority = p.authority;
  b.domain = p.domain;
  b.size = p.size;
  b.alignment = p.alignment;
  b.device = p.device;
  b.locality = p.locality;
  b.registration_state = RegistrationState::UNREGISTERED;
  b.lifecycle = BufferLifecycle::ACTIVE;
  b.support = p.support;
  b.provenance = p.provenance;
  b.freshness = next_freshness_locked();
  b.name = p.name;
  b.handle = nullptr;
  state_.buffers[b.id.value()] = b;
  return ok(b);
}

Result<BufferRecord> Coordinator::advance_buffer_generation(BufferId id, const Authority& authority) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = state_.buffers.find(id.value());
  if (it == state_.buffers.end()) {
    return err<BufferRecord>(ErrorCode::NOT_FOUND, "buffer not found");
  }
  BufferRecord& b = it->second;
  if (b.authority.worker_owned()) {
    if (authority != b.authority || !worker_live_locked(b.authority)) {
      return err<BufferRecord>(ErrorCode::STALE_BOOT, "buffer generation advance not authorized by owner");
    }
  }
  b.generation = b.generation.next();
  b.freshness = next_freshness_locked();
  invalidate_buffer_dependents_locked(id);
  return ok(b);
}

Result<void> Coordinator::retire_buffer(BufferId id, const Authority& authority) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = state_.buffers.find(id.value());
  if (it == state_.buffers.end()) {
    return err<void>(ErrorCode::NOT_FOUND, "buffer not found");
  }
  BufferRecord& b = it->second;
  if (b.authority.worker_owned()) {
    if (authority != b.authority || !worker_live_locked(b.authority)) {
      return err<void>(ErrorCode::STALE_BOOT, "buffer retire not authorized by owner");
    }
  }
  b.lifecycle = BufferLifecycle::RETIRED;
  b.freshness = next_freshness_locked();
  invalidate_buffer_dependents_locked(id);
  return ok();
}

Result<std::vector<BufferRecord>> Coordinator::buffers() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<BufferRecord> out;
  out.reserve(state_.buffers.size());
  for (const auto& [k, v] : state_.buffers) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const BufferRecord& a, const BufferRecord& b) {
    return a.id.value() < b.id.value();
  });
  return ok(std::move(out));
}

Result<BufferRecord> Coordinator::get_buffer(BufferId id) const {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = state_.buffers.find(id.value());
  if (it == state_.buffers.end()) {
    return err<BufferRecord>(ErrorCode::NOT_FOUND, "buffer not found");
  }
  return ok(it->second);
}

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------
Result<DeviceRecord> Coordinator::add_device(const DeviceAddParams& p) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (p.authority.worker_owned() && !worker_live_locked(p.authority)) {
    return err<DeviceRecord>(ErrorCode::STALE_BOOT, "device authority not live");
  }
  DeviceRecord d;
  d.id = DeviceId(next_id_locked());
  d.generation = DeviceGeneration(1);
  d.name = p.name;
  d.compute_capability = p.compute_capability;
  d.peer_memory_supported = p.peer_memory_supported;
  d.support = p.support;
  d.provenance = p.provenance;
  d.authority = p.authority;
  d.freshness = next_freshness_locked();
  state_.devices[d.id.value()] = d;
  return ok(d);
}

Result<std::vector<DeviceRecord>> Coordinator::devices() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<DeviceRecord> out;
  out.reserve(state_.devices.size());
  for (const auto& [k, v] : state_.devices) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const DeviceRecord& a, const DeviceRecord& b) {
    return a.id.value() < b.id.value();
  });
  return ok(std::move(out));
}

// ---------------------------------------------------------------------------
// Endpoints / nics / storage
// ---------------------------------------------------------------------------
Result<EndpointRecord> Coordinator::add_endpoint(const EndpointAddParams& p) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (state_.endpoints.size() >= state_.max_endpoints) {
    return err<EndpointRecord>(ErrorCode::RESOURCE_LIMIT, "endpoint limit reached");
  }
  if (p.authority.worker_owned() && !worker_live_locked(p.authority)) {
    return err<EndpointRecord>(ErrorCode::STALE_BOOT, "endpoint authority not live");
  }
  EndpointRecord e;
  e.id = EndpointId(next_id_locked());
  e.generation = EndpointGeneration(1);
  e.endpoint_class = p.endpoint_class;
  e.name = p.name;
  e.topology_hint = p.topology_hint;
  e.healthy = p.healthy;
  e.provider = p.provider;
  e.support = p.support;
  e.provenance = p.provenance;
  e.authority = p.authority;
  e.freshness = next_freshness_locked();
  state_.endpoints[e.id.value()] = e;
  return ok(e);
}

Result<EndpointRecord> Coordinator::advance_endpoint_generation(EndpointId id, const Authority& authority) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = state_.endpoints.find(id.value());
  if (it == state_.endpoints.end()) {
    return err<EndpointRecord>(ErrorCode::NOT_FOUND, "endpoint not found");
  }
  EndpointRecord& e = it->second;
  if (e.authority.worker_owned()) {
    if (authority != e.authority || !worker_live_locked(e.authority)) {
      return err<EndpointRecord>(ErrorCode::STALE_BOOT, "endpoint generation advance not authorized");
    }
  }
  e.generation = e.generation.next();
  e.freshness = next_freshness_locked();
  invalidate_endpoint_dependents_locked(id);
  return ok(e);
}

Result<void> Coordinator::set_endpoint_health(EndpointId id, bool healthy, const Authority& authority) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = state_.endpoints.find(id.value());
  if (it == state_.endpoints.end()) {
    return err<void>(ErrorCode::NOT_FOUND, "endpoint not found");
  }
  EndpointRecord& e = it->second;
  if (e.authority.worker_owned() && (!worker_live_locked(e.authority) || authority != e.authority)) {
    return err<void>(ErrorCode::STALE_BOOT, "endpoint health update not authorized");
  }
  e.healthy = healthy;
  e.freshness = next_freshness_locked();
  return ok();
}

Result<std::vector<EndpointRecord>> Coordinator::endpoints() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<EndpointRecord> out;
  out.reserve(state_.endpoints.size());
  for (const auto& [k, v] : state_.endpoints) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const EndpointRecord& a, const EndpointRecord& b) {
    return a.id.value() < b.id.value();
  });
  return ok(std::move(out));
}

Result<NicRecord> Coordinator::add_nic(const NicRecord& rec) {
  std::lock_guard<std::mutex> guard(mutex_);
  NicRecord n = rec;
  if (!n.id.valid()) n.id = NicId(next_id_locked());
  if (n.generation.is_zero()) n.generation = NicGeneration(1);
  n.freshness = next_freshness_locked();
  state_.nics[n.id.value()] = n;
  return ok(n);
}

Result<std::vector<NicRecord>> Coordinator::nics() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<NicRecord> out;
  out.reserve(state_.nics.size());
  for (const auto& [k, v] : state_.nics) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const NicRecord& a, const NicRecord& b) {
    return a.id.value() < b.id.value();
  });
  return ok(std::move(out));
}

Result<StorageEndpointRecord> Coordinator::add_storage_endpoint(const StorageEndpointRecord& rec) {
  std::lock_guard<std::mutex> guard(mutex_);
  StorageEndpointRecord s = rec;
  if (!s.id.valid()) s.id = StorageEndpointId(next_id_locked());
  if (s.generation.is_zero()) s.generation = StorageGeneration(1);
  s.freshness = next_freshness_locked();
  state_.storage[s.id.value()] = s;
  return ok(s);
}

Result<std::vector<StorageEndpointRecord>> Coordinator::storage_endpoints() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<StorageEndpointRecord> out;
  out.reserve(state_.storage.size());
  for (const auto& [k, v] : state_.storage) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const StorageEndpointRecord& a, const StorageEndpointRecord& b) {
    return a.id.value() < b.id.value();
  });
  return ok(std::move(out));
}

// ---------------------------------------------------------------------------
// Providers / capabilities
// ---------------------------------------------------------------------------
Result<ProviderInfo> Coordinator::add_provider(const ProviderAddParams& p) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (p.name.empty()) {
    return err<ProviderInfo>(ErrorCode::INVALID_ARGUMENT, "provider name required");
  }
  // Deduplicate by name.
  for (auto& [k, v] : state_.providers) {
    (void)k;
    if (v.name == p.name) {
      v.available = p.available;
      v.support = p.support;
      v.provenance = p.provenance;
      return ok(v);
    }
  }
  ProviderInfo info;
  info.id = ProviderId(next_id_locked());
  info.name = p.name;
  info.available = p.available;
  info.support = p.support;
  info.provenance = p.provenance;
  state_.providers[info.id.value()] = info;
  return ok(info);
}

Result<std::vector<ProviderInfo>> Coordinator::providers() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<ProviderInfo> out;
  out.reserve(state_.providers.size());
  for (const auto& [k, v] : state_.providers) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const ProviderInfo& a, const ProviderInfo& b) {
    return a.id.value() < b.id.value();
  });
  return ok(std::move(out));
}

Result<CapabilityObservation> Coordinator::publish_capability(const CapabilityPublishParams& p) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (p.authority.worker_owned() && !worker_live_locked(p.authority)) {
    return err<CapabilityObservation>(ErrorCode::STALE_BOOT, "capability authority not live");
  }
  CapabilityObservation c;
  c.id = CapabilityObservationId(next_id_locked());
  c.provider = p.provider;
  c.capability = p.capability;
  c.value = p.value;
  c.support = p.support;
  c.provenance = p.provenance;
  c.authority = p.authority;
  c.freshness = next_freshness_locked();
  // Replace existing observation for the same (provider, capability) to avoid duplicates.
  for (auto& [k, v] : state_.capabilities) {
    (void)k;
    if (v.provider == p.provider && v.capability == p.capability) {
      v = c;
      return ok(v);
    }
  }
  state_.capabilities[c.id.value()] = c;
  return ok(c);
}

Result<void> Coordinator::revoke_capability(const std::string& provider, const std::string& capability,
                                            const Authority& authority) {
  std::lock_guard<std::mutex> guard(mutex_);
  for (auto it = state_.capabilities.begin(); it != state_.capabilities.end(); ++it) {
    auto& c = it->second;
    if (c.provider == provider && c.capability == capability) {
      if (c.authority.worker_owned() && (authority != c.authority || !worker_live_locked(c.authority))) {
        return err<void>(ErrorCode::STALE_BOOT, "capability revoke not authorized");
      }
      it = state_.capabilities.erase(it);
      return ok();
    }
  }
  return ok();  // revoking an absent capability is a no-op
}

Result<std::vector<CapabilityObservation>> Coordinator::capabilities() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<CapabilityObservation> out;
  out.reserve(state_.capabilities.size());
  for (const auto& [k, v] : state_.capabilities) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const CapabilityObservation& a, const CapabilityObservation& b) {
    if (a.provider != b.provider) return a.provider < b.provider;
    return a.capability < b.capability;
  });
  return ok(std::move(out));
}

// ---------------------------------------------------------------------------
// Registration (transactional)
// ---------------------------------------------------------------------------
Result<Registration> Coordinator::register_buffer(const RegisterParams& p, Backend* backend) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto bit = state_.buffers.find(p.buffer.value());
  if (bit == state_.buffers.end()) {
    return err<Registration>(ErrorCode::INVALID_BUFFER, "buffer does not exist");
  }
  BufferRecord& buffer = bit->second;
  if (buffer.lifecycle != BufferLifecycle::ACTIVE) {
    return err<Registration>(ErrorCode::INVALID_BUFFER, "buffer is retired");
  }
  auto eit = state_.endpoints.find(p.endpoint.value());
  if (eit == state_.endpoints.end()) {
    return err<Registration>(ErrorCode::NOT_FOUND, "endpoint does not exist");
  }
  EndpointRecord& endpoint = eit->second;
  if (endpoint.support == Support::UNSUPPORTED) {
    return err<Registration>(ErrorCode::REGISTRATION_FAILED, "cannot register against an unsupported endpoint");
  }
  if (state_.registrations.size() >= state_.max_registrations) {
    return err<Registration>(ErrorCode::RESOURCE_LIMIT, "registration limit reached");
  }
  if (p.authority.epoch.value() != state_.epoch.value()) {
    return err<Registration>(ErrorCode::STALE_EPOCH, "registration authority epoch is stale");
  }
  // Authority / owner checks.
  if (p.authority.worker_owned()) {
    if (!worker_live_locked(p.authority)) {
      return err<Registration>(ErrorCode::STALE_BOOT, "registering worker authority is not live");
    }
  }
  if (buffer.authority.worker_owned() && buffer.authority != p.authority) {
    return err<Registration>(ErrorCode::REGISTRATION_FAILED, "buffer is owned by a different worker");
  }
  if (buffer.authority.worker_owned() && buffer.authority.epoch != p.authority.epoch) {
    return err<Registration>(ErrorCode::STALE_EPOCH, "buffer and registration epochs differ");
  }

  // Find latest registration for (buffer, endpoint).
  const Registration* latest_reg = nullptr;
  for (const auto& [k, v] : state_.registrations) {
    (void)k;
    if (v.buffer == p.buffer && v.endpoint == p.endpoint) {
      if (latest_reg == nullptr || v.generation.value() > latest_reg->generation.value()) latest_reg = &v;
    }
  }

  // Idempotent duplicate registration: same (buffer,endpoint), REGISTERED, current
  // generations, same authority => return existing, do not double count.
  if (latest_reg != nullptr && latest_reg->state == RegistrationState::REGISTERED &&
      latest_reg->buffer_generation == buffer.generation &&
      latest_reg->endpoint_generation == endpoint.generation &&
      latest_reg->authority == p.authority &&
      latest_reg->provider == p.provider) {
    Registration dup = *latest_reg;
    dup.duplicated = true;
    return ok(dup);
  }

  // Conflicting registration: same (buffer,endpoint) already REGISTERED with a
  // different authority (or different provider) => fail clearly.
  if (latest_reg != nullptr && latest_reg->state == RegistrationState::REGISTERED) {
    if (latest_reg->authority != p.authority || latest_reg->provider != p.provider) {
      return err<Registration>(ErrorCode::REGISTRATION_FAILED,
                               "conflicting registration already exists for this buffer+endpoint");
    }
    return err<Registration>(ErrorCode::REGISTRATION_FAILED, "registration already active");
  }
  if (latest_reg != nullptr && latest_reg->state == RegistrationState::REGISTERING) {
    return err<Registration>(ErrorCode::REGISTRATION_FAILED, "registration is already in progress");
  }
  if (latest_reg != nullptr && latest_reg->state == RegistrationState::DEREGISTERING) {
    // Retire the in-flight deregistration, then proceed.
    for (auto& [k, v] : state_.registrations) {
      (void)k;
      if (v.id == latest_reg->id) v.state = RegistrationState::RETIRED;
    }
  }

  // Stage: backend registration.
  std::shared_ptr<BackendHandle> handle;
  Support support = p.support;
  Provenance prov = p.provenance;
  RegistrationId reg_id{};
  if (backend != nullptr) {
    RegistrationSpec spec;
    spec.buffer = buffer.id;
    spec.domain = buffer.domain;
    spec.buffer_generation = buffer.generation;
    spec.endpoint = endpoint.id;
    spec.endpoint_class = endpoint.endpoint_class;
    spec.endpoint_generation = endpoint.generation;
    spec.size = buffer.size;
    spec.alignment = buffer.alignment;
    spec.authority = p.authority;
    spec.support = buffer.support;
    auto rr = backend->register_buffer(spec);
    if (!rr) {
      return err<Registration>(ErrorCode::REGISTRATION_FAILED, rr.error().message);
    }
    reg_id = rr->id;
    support = rr->support;
    prov = rr->provenance;
    handle = std::move(rr->handle);
  }
  if (!reg_id.valid()) reg_id = RegistrationId(next_id_locked());
  RegistrationGeneration new_gen =
      (latest_reg != nullptr ? latest_reg->generation.next() : RegistrationGeneration(1));

  Registration reg;
  reg.id = reg_id;
  reg.generation = new_gen;
  reg.buffer = buffer.id;
  reg.buffer_generation = buffer.generation;
  reg.endpoint = endpoint.id;
  reg.endpoint_generation = endpoint.generation;
  reg.provider = p.provider;
  reg.authority = p.authority;
  reg.state = RegistrationState::REGISTERED;
  reg.support = support;
  reg.provenance = prov;
  reg.freshness = next_freshness_locked();
  reg.required_capabilities = p.required_capabilities;
  reg.handle = std::move(handle);
  state_.registrations[reg.id.value()] = reg;

  buffer.registration_state = RegistrationState::REGISTERED;
  buffer.registration_id = reg.id;
  buffer.registration_generation = reg.generation;
  buffer.freshness = next_freshness_locked();
  return ok(reg);
}

Result<void> Coordinator::deregister_buffer(RegistrationId id, const Authority& authority) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = state_.registrations.find(id.value());
  if (it == state_.registrations.end()) {
    return err<void>(ErrorCode::NOT_FOUND, "registration not found");
  }
  Registration& reg = it->second;
  if (reg.state == RegistrationState::RETIRED || reg.state == RegistrationState::UNREGISTERED) {
    return ok();  // idempotent
  }
  // Authority check.
  if (reg.authority.worker_owned() && !worker_live_locked(reg.authority)) {
    // Worker already dead; the registration is already invalidated. Idempotent.
    reg.state = RegistrationState::RETIRED;
    return ok();
  }
  if (reg.authority.worker_owned() && authority != reg.authority) {
    return err<void>(ErrorCode::STALE_BOOT, "deregistration not authorized by registration owner");
  }
  // Stale deregistration must not destroy a newer registration: if the target
  // registration is not the current one for (buffer,endpoint), reject.
  const Registration* latest_reg = nullptr;
  for (const auto& [k, v] : state_.registrations) {
    (void)k;
    if (v.buffer == reg.buffer && v.endpoint == reg.endpoint) {
      if (latest_reg == nullptr || v.generation.value() > latest_reg->generation.value()) latest_reg = &v;
    }
  }
  if (latest_reg != nullptr && latest_reg->id != reg.id) {
    return err<void>(ErrorCode::STALE_GENERATION, "stale deregistration rejected (newer registration exists)");
  }

  reg.state = RegistrationState::DEREGISTERING;
  // Backend deregistration (best-effort, does not change authority state).
  if (reg.handle != nullptr) {
    // The handle is opaque; the backend owns it. We clear it here as the
    // process-local evidence is retired with this registration.
    reg.handle.reset();
  }
  reg.state = RegistrationState::RETIRED;
  reg.freshness = next_freshness_locked();

  // If this was the buffer's current registration, clear the buffer ref.
  auto bit = state_.buffers.find(reg.buffer.value());
  if (bit != state_.buffers.end()) {
    BufferRecord& b = bit->second;
    if (b.registration_id == reg.id) {
      b.registration_state = RegistrationState::UNREGISTERED;
      b.registration_id = RegistrationId{};
      b.registration_generation = RegistrationGeneration{};
      b.freshness = next_freshness_locked();
    }
  }
  return ok();
}

Result<std::vector<Registration>> Coordinator::registrations() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<Registration> out;
  out.reserve(state_.registrations.size());
  for (const auto& [k, v] : state_.registrations) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const Registration& a, const Registration& b) {
    return a.id.value() < b.id.value();
  });
  return ok(std::move(out));
}

// ---------------------------------------------------------------------------
// Path evidence + evaluation + planning
// ---------------------------------------------------------------------------
PathEvidence Coordinator::build_evidence_locked(const PlanRequestParams& p) const {
  PathEvidence ev;
  ev.epoch = state_.epoch;
  auto bit = state_.buffers.find(p.source.value());
  if (bit != state_.buffers.end()) ev.source = bit->second;
  auto eit = state_.endpoints.find(p.destination.value());
  if (eit != state_.endpoints.end()) ev.destination = eit->second;
  if (ev.source.device.valid()) {
    auto dit = state_.devices.find(ev.source.device.value());
    if (dit != state_.devices.end()) ev.device = dit->second;
  }
  ev.registrations.reserve(state_.registrations.size());
  for (const auto& [k, v] : state_.registrations) {
    (void)k;
    ev.registrations.push_back(v);
  }
  ev.capabilities.reserve(state_.capabilities.size());
  for (const auto& [k, v] : state_.capabilities) {
    (void)k;
    ev.capabilities.push_back(v);
  }
  ev.devices.reserve(state_.devices.size());
  for (const auto& [k, v] : state_.devices) {
    (void)k;
    ev.devices.push_back(v);
  }
  ev.endpoints.reserve(state_.endpoints.size());
  for (const auto& [k, v] : state_.endpoints) {
    (void)k;
    ev.endpoints.push_back(v);
  }
  ev.providers.reserve(state_.providers.size());
  for (const auto& [k, v] : state_.providers) {
    (void)k;
    ev.providers.push_back(v);
  }
  ev.live_workers.reserve(state_.workers.size());
  for (const auto& [k, v] : state_.workers) {
    (void)k;
    ev.live_workers.push_back(v);
  }
  return ev;
}

Result<PathDecision> Coordinator::evaluate_path(const PlanRequestParams& p) {
  PathRequest req;
  req.source = p.source;
  req.destination = p.destination;
  req.direction = p.direction;
  req.policy = p.policy;
  req.size = p.size;
  PathEvidence ev;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    ev = build_evidence_locked(p);
  }
  PathPlanner planner;
  return planner.evaluate(req, ev);
}

Result<TransferPlan> Coordinator::plan_transfer(const PlanRequestParams& p, Backend* backend) {
  (void)backend;
  // First evaluate using a snapshot (read path).
  auto decision = evaluate_path(p);
  if (!decision) {
    return err<TransferPlan>(decision.error().code, decision.error().message);
  }
  const PathDecision& d = *decision;

  if (d.eligibility != PathEligibility::DIRECT_ALLOWED &&
      d.eligibility != PathEligibility::DIRECT_ALLOWED_DEGRADED &&
      !(d.eligibility == PathEligibility::FALLBACK_REQUIRED && d.fallback_available)) {
    return err<TransferPlan>(error_for_eligibility(d.eligibility), d.direct_rejection);
  }

  std::lock_guard<std::mutex> guard(mutex_);
  if (state_.plans.size() >= state_.max_plans) {
    return err<TransferPlan>(ErrorCode::RESOURCE_LIMIT, "plan limit reached");
  }
  auto bit = state_.buffers.find(p.source.value());
  if (bit == state_.buffers.end()) {
    return err<TransferPlan>(ErrorCode::STALE_BUFFER, "source buffer disappeared");
  }
  const BufferRecord& src = bit->second;
  if (src.generation.value() != d.source_generation.value()) {
    return err<TransferPlan>(ErrorCode::STALE_GENERATION, "source buffer generation advanced since snapshot");
  }
  auto eit = state_.endpoints.find(p.destination.value());
  if (eit == state_.endpoints.end()) {
    return err<TransferPlan>(ErrorCode::STALE_ENDPOINT, "destination endpoint disappeared");
  }
  const EndpointRecord& dst = eit->second;
  if (dst.generation.value() != d.destination_generation.value()) {
    return err<TransferPlan>(ErrorCode::STALE_ENDPOINT, "destination endpoint generation advanced since snapshot");
  }

  const bool direct = (d.eligibility == PathEligibility::DIRECT_ALLOWED ||
                       d.eligibility == PathEligibility::DIRECT_ALLOWED_DEGRADED);

  const Registration* reg = nullptr;
  for (const auto& [k, v] : state_.registrations) {
    (void)k;
    if (v.buffer == src.id && v.endpoint == dst.id && v.state == RegistrationState::REGISTERED) {
      if (reg == nullptr || v.generation.value() > reg->generation.value()) reg = &v;
    }
  }
  if (direct && reg == nullptr) {
    return err<TransferPlan>(ErrorCode::REGISTRATION_STALE, "registration required for direct path vanished");
  }

  TransferPlan plan;
  plan.id = TransferPlanId(next_id_locked());
  plan.path_generation = PathGeneration(next_id_locked());
  plan.generation = TransferGeneration(1);
  plan.source = src.id;
  plan.source_generation = src.generation;
  plan.destination = dst.id;
  plan.destination_generation = dst.generation;
  plan.policy = p.policy;
  plan.expected_bytes = p.size;
  plan.eligibility = d.eligibility;
  plan.support = d.support;
  plan.provenance = d.provenance;
  plan.epoch = state_.epoch;
  plan.authority = src.authority;
  plan.explanation = d.summary;
  plan.authoritative = true;

  if (reg != nullptr) {
    plan.registration_id = reg->id;
    plan.registration_generation = reg->generation;
    plan.registration_buffer_generation = reg->buffer_generation;
  }

  if (direct) {
    TransferStage stage;
    stage.path_class = d.direct_class;
    stage.support = d.support;
    stage.provenance = d.provenance;
    stage.src = src.id;
    stage.endpoint = dst.id;
    stage.direct = true;
    stage.description = std::string("direct ") + to_string(d.direct_class);
    plan.stages.push_back(std::move(stage));
    plan.copy_count = 1;
  } else {
    // Fallback via pinned-host staging (in-lock allocation; no nested lock).
    BufferRecord tmp;
    tmp.id = BufferId(next_id_locked());
    tmp.generation = BufferGeneration(1);
    tmp.authority = src.authority;
    tmp.domain = MemoryDomain::HOST_PINNED;
    tmp.size = p.size;
    tmp.alignment = 64;
    tmp.locality = Locality::LOCAL;
    tmp.support = Support::SYNTHETIC;
    tmp.provenance = Provenance::SYNTHETIC_FIXTURE;
    tmp.lifecycle = BufferLifecycle::ACTIVE;
    tmp.freshness = next_freshness_locked();
    tmp.name = "fallback_staging";
    state_.buffers[tmp.id.value()] = tmp;

    PathClass cls = d.selected_fallback ? d.selected_fallback->path_class : PathClass::GPU_TO_HOST_PINNED;
    TransferStage s1;
    s1.path_class = cls;
    s1.support = Support::SYNTHETIC;
    s1.provenance = Provenance::SYNTHETIC_FIXTURE;
    s1.src = src.id;
    s1.dst = tmp.id;
    s1.direct = false;
    s1.description = "GPU -> pinned host staging";
    TransferStage s2;
    s2.path_class = cls;
    s2.support = Support::SYNTHETIC;
    s2.provenance = Provenance::SYNTHETIC_FIXTURE;
    s2.src = tmp.id;
    s2.endpoint = dst.id;
    s2.direct = false;
    s2.description = "pinned host staging -> endpoint";
    plan.stages.push_back(std::move(s1));
    plan.stages.push_back(std::move(s2));
    plan.copy_count = 2;
  }

  state_.plans[plan.id.value()] = plan;
  return ok(plan);
}


}  // namespace gpudirectfabric