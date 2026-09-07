#include "gpudirectfabric/backend.hpp"
#include "gpudirectfabric/backends/synthetic_backend.hpp"
#include "gpudirectfabric/backends/synthetic_seed.hpp"
#include "gpudirectfabric/coordinator.hpp"

#include <cassert>
#include <cstdio>

using namespace gpudirectfabric;

int main() {
  SyntheticBackend synth;
  Coordinator coord(CoordinatorEpoch(1));
  SyntheticSeedResult seed = seed_synthetic(coord, synth);

  // Coordinates authority for coordinator-owned buffers/registrations.
  Authority auth;
  auth.epoch = coord.epoch();

  // Create a CUDA device buffer on the synthetic device.
  CreateBufferParams bp;
  bp.domain = MemoryDomain::CUDA_DEVICE;
  bp.size = 1024 * 1024;
  bp.alignment = 64;
  bp.device = seed.device;
  bp.locality = Locality::LOCAL;
  bp.support = Support::SYNTHETIC;
  bp.provenance = Provenance::SYNTHETIC_FIXTURE;
  bp.authority = auth;
  bp.name = "gpu_src";
  auto bres = coord.create_buffer(bp);
  assert(bres);
  BufferId buf = bres->id;

  // Register the GPU buffer against the synthetic NIC endpoint.
  RegisterParams rp;
  rp.buffer = buf;
  rp.endpoint = seed.nic;
  rp.provider = seed.rdma;
  rp.authority = auth;
  rp.support = Support::SYNTHETIC;
  rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  rp.required_capabilities = {"direct_nic"};
  auto rreg = coord.register_buffer(rp, &synth);
  assert(rreg);
  std::printf("[smoke] registered buffer %llu reg %llu gen %llu\n",
              (unsigned long long)buf.value(), (unsigned long long)rreg->id.value(),
              (unsigned long long)rreg->generation.value());

  // Evaluate the GPU->NIC direct path.
  PlanRequestParams pr;
  pr.source = buf;
  pr.destination = seed.nic;
  pr.direction = Direction::GPU_TO_PEER;
  pr.policy = PathPolicy::PREFER_DIRECT;
  pr.size = 1024 * 1024;
  auto dec = coord.evaluate_path(pr);
  assert(dec);
  std::printf("[smoke] path eligibility=%s direct_elig=%s support=%s\n",
              to_string(dec->eligibility), to_string(dec->direct_eligibility), to_string(dec->support));
  assert(dec->eligibility == PathEligibility::DIRECT_ALLOWED_DEGRADED);

  // Plan, begin, complete.
  auto plan = coord.plan_transfer(pr, &synth);
  assert(plan);
  std::printf("[smoke] plan id=%llu eligibility=%s copies=%u\n",
              (unsigned long long)plan->id.value(), to_string(plan->eligibility), plan->copy_count);
  auto att = coord.begin_transfer(plan->id, auth, &synth);
  assert(att);
  auto rec = coord.complete_transfer(att->id, auth);
  assert(rec);
  std::printf("[smoke] receipt attempt=%llu state=%s integrity=%d bytes=%llu\n",
              (unsigned long long)att->id.value(), to_string(rec->state),
              rec->integrity_verified ? 1 : 0, (unsigned long long)rec->bytes);
  assert(rec->state == TransferState::COMPLETED);
  assert(rec->bytes == 1024 * 1024);

  // Unregistered buffer => REGISTRATION_REQUIRED.
  CreateBufferParams bp2 = bp;
  bp2.name = "gpu_unreg";
  auto b2 = coord.create_buffer(bp2);
  assert(b2);
  PlanRequestParams pr2 = pr;
  pr2.source = b2->id;
  auto dec2 = coord.evaluate_path(pr2);
  assert(dec2);
  std::printf("[smoke] unregistered path eligibility=%s\n", to_string(dec2->eligibility));
  assert(dec2->eligibility == PathEligibility::REGISTRATION_REQUIRED ||
         dec2->eligibility == PathEligibility::FALLBACK_REQUIRED);

  std::printf("[smoke] OK\n");
  return 0;
}
