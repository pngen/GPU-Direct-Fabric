#define _CRT_SECURE_NO_WARNINGS
#include "gpudirectfabric/backends/synthetic_backend.hpp"
#include "gpudirectfabric/backends/synthetic_seed.hpp"
#include "gpudirectfabric/backends/unsupported_backend.hpp"
#include "gpudirectfabric/coordinator.hpp"
#include "gpudirectfabric/protocol.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace gpudirectfabric;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static Authority coord_auth(const Coordinator& c) { Authority a; a.epoch = c.epoch(); return a; }

static void seed_and_buffer(SyntheticBackend& synth, Coordinator& coord, SyntheticSeedResult& seed,
                            BufferId& buf, EndpointId& nic, Authority& auth) {
  seed = seed_synthetic(coord, synth);
  auth = coord_auth(coord);
  nic = seed.nic;
  CreateBufferParams bp;
  bp.domain = MemoryDomain::CUDA_DEVICE;
  bp.size = 1024 * 1024;
  bp.alignment = 64;
  bp.device = seed.device;
  bp.support = Support::SYNTHETIC;
  bp.provenance = Provenance::SYNTHETIC_FIXTURE;
  bp.authority = auth;
  auto b = coord.create_buffer(bp);
  CHECK(b);
  buf = b->id;
}

static void test_basic_lifecycle() {
  SyntheticBackend synth;
  Coordinator coord(CoordinatorEpoch(1));
  SyntheticSeedResult seed;
  BufferId buf;
  EndpointId nic;
  Authority auth;
  seed_and_buffer(synth, coord, seed, buf, nic, auth);
  // Registration + idempotent duplicate does not double count.
  RegisterParams rp; rp.buffer = buf; rp.endpoint = nic; rp.provider = seed.rdma; rp.authority = auth;
  rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  auto reg = coord.register_buffer(rp, &synth);
  CHECK(reg);
  auto dup = coord.register_buffer(rp, &synth);
  CHECK(dup && dup->duplicated);
  auto regs = coord.registrations(); CHECK(regs && regs->size() == 1);
  // Buffer generation advance invalidates the registration.
  auto adv = coord.advance_buffer_generation(buf, auth);
  CHECK(adv);
  regs = coord.registrations(); CHECK(regs && regs->size() == 1 && (*regs)[0].state == RegistrationState::REVALIDATION_REQUIRED);
  // Conflicting registration with a different provider is rejected (fresh buffer).
  CreateBufferParams cb; cb.domain = MemoryDomain::CUDA_DEVICE; cb.size = 1024 * 1024; cb.alignment = 64;
  cb.device = seed.device; cb.support = Support::SYNTHETIC; cb.provenance = Provenance::SYNTHETIC_FIXTURE; cb.authority = auth;
  auto nb = coord.create_buffer(cb); CHECK(nb);
  RegisterParams rp3 = rp; rp3.buffer = nb->id;
  CHECK(coord.register_buffer(rp3, &synth));
  rp3.provider = seed.cuda;
  auto conf = coord.register_buffer(rp3, &synth);
  CHECK(!conf);
  // Retire buffer.
  CHECK(!!coord.retire_buffer(buf, auth));
  auto b = coord.get_buffer(buf); CHECK(b && b->lifecycle == BufferLifecycle::RETIRED);
}

static void test_path_outcomes() {
  SyntheticBackend synth;
  Coordinator coord(CoordinatorEpoch(1));
  SyntheticSeedResult seed;
  BufferId buf;
  EndpointId nic;
  Authority auth;
  seed_and_buffer(synth, coord, seed, buf, nic, auth);
  PlanRequestParams pr; pr.source = buf; pr.destination = nic; pr.size = 1024 * 1024;
  // Unregistered -> REGISTRATION_REQUIRED or FALLBACK_REQUIRED (host staging present).
  auto u = coord.evaluate_path(pr); CHECK(u && u->eligibility == PathEligibility::FALLBACK_REQUIRED);
  // Unregistered + DIRECT_ONLY -> must not stage.
  pr.policy = PathPolicy::DIRECT_ONLY;
  auto u2 = coord.evaluate_path(pr); CHECK(u2 && u2->eligibility == PathEligibility::REGISTRATION_REQUIRED && !u2->fallback_available);
  pr.policy = PathPolicy::PREFER_DIRECT;
  // Register, then a direct path is allowed (degraded, synthetic).
  RegisterParams rp; rp.buffer = buf; rp.endpoint = nic; rp.provider = seed.rdma; rp.authority = auth;
  rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  CHECK(coord.register_buffer(rp, &synth));
  auto d = coord.evaluate_path(pr);
  CHECK(d && d->eligibility == PathEligibility::DIRECT_ALLOWED_DEGRADED && d->support == Support::SYNTHETIC);
  // Plan is direct, 1 copy.
  auto plan = coord.plan_transfer(pr, &synth); CHECK(plan && plan->copy_count == 1 && plan->eligibility == PathEligibility::DIRECT_ALLOWED_DEGRADED);
  // Fallback: endpoint unhealthy -> ENDPOINT_STALE (no fallback when unhealthy? yes ENDPOINT_STALE).
  CHECK(coord.set_endpoint_health(nic, false, coord_auth(coord)));
  auto e = coord.evaluate_path(pr);
  CHECK(e && e->eligibility == PathEligibility::ENDPOINT_STALE && !e->fallback_available);
  CHECK(coord.set_endpoint_health(nic, true, coord_auth(coord)));
}

static void test_fallback() {
  SyntheticBackend synth;
  Coordinator coord(CoordinatorEpoch(1));
  SyntheticSeedResult seed;
  BufferId buf;
  EndpointId nic;
  Authority auth;
  seed_and_buffer(synth, coord, seed, buf, nic, auth);
  // No host_staging capability => no fallback. Remove it.
  CHECK(coord.revoke_capability("synthetic", "host_staging", coord_auth(coord)));
  PlanRequestParams pr; pr.source = buf; pr.destination = nic; pr.policy = PathPolicy::ALLOW_STAGING; pr.size = 1024 * 1024;
  auto u = coord.evaluate_path(pr);
  CHECK(u && u->eligibility == PathEligibility::REGISTRATION_REQUIRED && !u->fallback_available);
  CHECK(u && !u->fallback_disallowed);
  // Re-enable host staging and require direct-only (no staging).
  CapabilityPublishParams cp; cp.provider = "synthetic"; cp.capability = "host_staging"; cp.value = "true";
  cp.support = Support::SYNTHETIC; cp.provenance = Provenance::SYNTHETIC_FIXTURE; cp.authority = coord_auth(coord);
  CHECK(coord.publish_capability(cp));
  pr.policy = PathPolicy::DIRECT_ONLY;
  auto u2 = coord.evaluate_path(pr);
  CHECK(u2 && u2->eligibility == PathEligibility::REGISTRATION_REQUIRED && !u2->fallback_available);
  pr.policy = PathPolicy::ALLOW_STAGING;
  auto u3 = coord.evaluate_path(pr);
  CHECK(u3 && u3->eligibility == PathEligibility::FALLBACK_REQUIRED && u3->fallback_available && u3->fallback_candidates.size() == 1);
  // Build a fallback plan (2 stages).
  auto plan = coord.plan_transfer(pr, &synth);
  CHECK(plan && plan->copy_count == 2 && plan->stages.size() == 2 && plan->eligibility == PathEligibility::FALLBACK_REQUIRED);
}

static void test_transfer_authority() {
  SyntheticBackend synth;
  Coordinator coord(CoordinatorEpoch(1));
  SyntheticSeedResult seed;
  BufferId buf;
  EndpointId nic;
  Authority auth;
  seed_and_buffer(synth, coord, seed, buf, nic, auth);
  RegisterParams rp; rp.buffer = buf; rp.endpoint = nic; rp.provider = seed.rdma; rp.authority = auth;
  rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  CHECK(coord.register_buffer(rp, &synth));
  PlanRequestParams pr; pr.source = buf; pr.destination = nic; pr.size = 1024 * 1024;
  auto plan = coord.plan_transfer(pr, &synth); CHECK(plan);
  auto att = coord.begin_transfer(plan->id, auth, &synth); CHECK(att);
  auto rec = coord.complete_transfer(att->id, auth);
  CHECK(rec && rec->state == TransferState::COMPLETED && rec->bytes == 1024 * 1024 && rec->integrity_verified);
  // Idempotent completion.
  auto rec2 = coord.complete_transfer(att->id, auth); CHECK(rec2 && rec2->state == TransferState::COMPLETED);
  // Cancellation after commit is rejected.
  auto canc = coord.cancel_transfer(att->id, auth); CHECK(!canc && canc.error().code == ErrorCode::CANCELLED);
  // Fresh attempt, then cancel -> cancellation wins; cannot complete after cancel.
  auto plan2 = coord.plan_transfer(pr, &synth); CHECK(plan2);
  auto att2 = coord.begin_transfer(plan2->id, auth, &synth); CHECK(att2);
  auto canc2 = coord.cancel_transfer(att2->id, auth); CHECK(canc2 && canc2->state == TransferState::CANCELLED);
  auto comp_after = coord.complete_transfer(att2->id, auth);
  CHECK(!comp_after && comp_after.error().code == ErrorCode::CANCELLED);
}

static void test_stale_plan_generation() {
  SyntheticBackend synth;
  Coordinator coord(CoordinatorEpoch(1));
  SyntheticSeedResult seed;
  BufferId buf;
  EndpointId nic;
  Authority auth;
  seed_and_buffer(synth, coord, seed, buf, nic, auth);
  RegisterParams rp; rp.buffer = buf; rp.endpoint = nic; rp.provider = seed.rdma; rp.authority = auth;
  rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  CHECK(coord.register_buffer(rp, &synth));
  PlanRequestParams pr; pr.source = buf; pr.destination = nic; pr.size = 1024 * 1024;
  auto plan = coord.plan_transfer(pr, &synth); CHECK(plan);
  // Advance the buffer generation => the plan becomes non-authoritative.
  CHECK(coord.advance_buffer_generation(buf, auth));
  auto att = coord.begin_transfer(plan->id, auth, &synth);
  CHECK(!att && att.error().code == ErrorCode::STALE_GENERATION);
}

static void test_worker_fencing() {
  SyntheticBackend synth;
  Coordinator coord(CoordinatorEpoch(1));
  // Register a worker and publish endpoint/capabilities/buffer/registration under its authority.
  Authority wa; wa.worker = WorkerId(7); wa.boot = WorkerBootId(1); wa.epoch = coord.epoch();
  CHECK(coord.register_worker(WorkerId(7), WorkerBootId(1), coord.epoch()));
  // Add a worker-owned endpoint.
  EndpointAddParams ep; ep.endpoint_class = EndpointClass::NIC; ep.name = "wnic"; ep.healthy = true;
  ep.support = Support::SYNTHETIC; ep.provenance = Provenance::SYNTHETIC_FIXTURE; ep.authority = wa;
  auto nic = coord.add_endpoint(ep); CHECK(nic);
  ProviderAddParams pp; pp.name = "rdma"; pp.available = true; pp.support = Support::SYNTHETIC;
  auto rdma = coord.add_provider(pp); CHECK(rdma);
  CapabilityPublishParams cp; cp.provider = "rdma"; cp.capability = "direct_nic"; cp.value = "true";
  cp.support = Support::SYNTHETIC; cp.provenance = Provenance::SYNTHETIC_FIXTURE; cp.authority = wa;
  CHECK(coord.publish_capability(cp));
  DeviceAddParams dp; dp.name = "wgpu"; dp.support = Support::SYNTHETIC; dp.authority = wa;
  auto dev = coord.add_device(dp); CHECK(dev);
  CreateBufferParams bp; bp.domain = MemoryDomain::CUDA_DEVICE; bp.size = 4096; bp.alignment = 64;
  bp.device = dev->id; bp.support = Support::SYNTHETIC; bp.provenance = Provenance::SYNTHETIC_FIXTURE; bp.authority = wa;
  auto b = coord.create_buffer(bp); CHECK(b);
  RegisterParams rp; rp.buffer = b->id; rp.endpoint = nic->id; rp.provider = rdma->id; rp.authority = wa;
  rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  auto reg = coord.register_buffer(rp, &synth); CHECK(reg && reg->state == RegistrationState::REGISTERED);
  // Worker death invalidates the registration.
  CHECK(coord.mark_worker_dead(WorkerId(7)));
  auto regs = coord.registrations(); CHECK(regs && regs->size() == 1 && (*regs)[0].state == RegistrationState::REVALIDATION_REQUIRED);
  // Stale boot re-register is rejected (boot 1 < prior max? test with same boot after death).
  auto stale = coord.register_worker(WorkerId(7), WorkerBootId(1), coord.epoch());
  CHECK(!stale && stale.error().code == ErrorCode::STALE_BOOT);
  // Fresh boot accepted.
  CHECK(coord.register_worker(WorkerId(7), WorkerBootId(2), coord.epoch()));
}

static void test_stale_deregistration() {
  SyntheticBackend synth;
  Coordinator coord(CoordinatorEpoch(1));
  SyntheticSeedResult seed;
  BufferId buf;
  EndpointId nic;
  Authority auth;
  seed_and_buffer(synth, coord, seed, buf, nic, auth);
  RegisterParams rp; rp.buffer = buf; rp.endpoint = nic; rp.provider = seed.rdma; rp.authority = auth;
  rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  auto reg = coord.register_buffer(rp, &synth); CHECK(reg);
  // Advance buffer generation (registration becomes stale), then register a newer one.
  CHECK(coord.advance_buffer_generation(buf, auth));
  auto reg2 = coord.register_buffer(rp, &synth); CHECK(reg2);
  // Deregister the OLD registration is stale and must not destroy the newer one.
  auto dr = coord.deregister_buffer(reg->id, auth);
  CHECK(!dr && dr.error().code == ErrorCode::STALE_GENERATION);
  auto regs = coord.registrations(); CHECK(regs && regs->size() == 2);
  auto latest = coord.registrations(); CHECK(latest && (*latest)[1].state == RegistrationState::REGISTERED);
  // Double deregistration of the current one is idempotent.
  CHECK(coord.deregister_buffer(reg2->id, auth));
  CHECK(coord.deregister_buffer(reg2->id, auth));
}

static void test_persistence_corruption() {
  SyntheticBackend synth;
  Coordinator coord(CoordinatorEpoch(1));
  SyntheticSeedResult seed;
  BufferId buf;
  EndpointId nic;
  Authority auth;
  seed_and_buffer(synth, coord, seed, buf, nic, auth);
  const std::string path = "core_test_state.gdfstate";
  CHECK(coord.persist_to_file(path));
  // Truncated file rejected.
  { std::FILE* f = std::fopen(path.c_str(), "rb"); CHECK(f); std::fseek(f, 0, SEEK_END); long n = std::ftell(f);
    std::vector<char> data(n); std::rewind(f); std::fread(data.data(), 1, n, f); std::fclose(f);
    std::FILE* g = std::fopen(path.c_str(), "wb"); std::fwrite(data.data(), 1, n / 2, g); std::fclose(g); }
  Coordinator c2(CoordinatorEpoch(2));
  auto rec = Coordinator::recover_from_file(path, c2); CHECK(!rec);
  // Trailing garbage rejected.
  { std::FILE* f = std::fopen(path.c_str(), "rb"); std::vector<char> data; char cbuf[64]; CHECK(f);
    while (!std::feof(f)) { std::size_t r = std::fread(cbuf, 1, 64, f); if (r) data.insert(data.end(), cbuf, cbuf + r); }
    std::fclose(f); data.push_back(99);
    std::FILE* g = std::fopen(path.c_str(), "wb"); std::fwrite(data.data(), 1, data.size(), g); std::fclose(g); }
  Coordinator c3(CoordinatorEpoch(2));
  auto rec2 = Coordinator::recover_from_file(path, c3); CHECK(!rec2);
  // Corrupt CRC rejected.
  { std::FILE* f = std::fopen(path.c_str(), "rb+"); CHECK(f); std::fseek(f, -4, SEEK_END); std::fputc((std::fgetc(f) ^ 0xFF), f); std::fclose(f); }
  Coordinator c4(CoordinatorEpoch(2));
  auto rec3 = Coordinator::recover_from_file(path, c4); CHECK(!rec3);
  // Absurd payload length rejected.
  { std::FILE* f = std::fopen(path.c_str(), "rb+"); CHECK(f); std::fseek(f, 8, SEEK_SET);
    std::uint8_t lenb[4] = {0xFF, 0xFF, 0xFF, 0xFF}; std::fwrite(lenb, 1, 4, f); std::fclose(f); }
  Coordinator c5(CoordinatorEpoch(2));
  auto rec4 = Coordinator::recover_from_file(path, c5); CHECK(!rec4);
  std::remove(path.c_str());
}

static void test_malformed_protocol() {
  auto bad = [](const std::vector<std::uint8_t>& w) { return decode_frame(w).ok; };
  // Valid minimal frame with empty payload.
  Frame f; f.type = MessageType::HELLO;
  std::vector<std::uint8_t> wire = encode_frame(f);
  CHECK(decode_frame(wire).ok);
  // Bad magic.
  std::vector<std::uint8_t> b = wire; b[0] = 0; CHECK(!bad(b));
  // Trailing bytes (length mismatch).
  b = wire; b.push_back(0); CHECK(!bad(b));
  // Absurd length.
  b = wire; b[8] = 0xFF; b[9] = 0xFF; b[10] = 0xFF; b[11] = 0x7F; CHECK(!bad(b));
  // Bad checksum.
  b = wire; b.back() ^= 0xFF; CHECK(!bad(b));
  // Bad version.
  b = wire; b[5] = 0x02; CHECK(!bad(b));
  // Malformed message type.
  b = wire; b[6] = 200; CHECK(!bad(b));
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test_basic_lifecycle();
  test_path_outcomes();
  test_fallback();
  test_transfer_authority();
  test_stale_plan_generation();
  test_worker_fencing();
  test_stale_deregistration();
  test_persistence_corruption();
  test_malformed_protocol();
  std::printf("[core_test] failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}