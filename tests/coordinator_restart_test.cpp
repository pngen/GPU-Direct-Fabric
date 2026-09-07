#include "gpudirectfabric/backends/synthetic_backend.hpp"
#include "gpudirectfabric/backends/synthetic_seed.hpp"
#include "gpudirectfabric/coordinator.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace gpudirectfabric;

int main(int argc, char** argv) {
  const std::string state = (argc >= 2) ? std::string(argv[1]) : "coordinator_restart.gdfstate";
  std::remove(state.c_str());

  // --- First coordinator incarnation (epoch 1) ---
  Coordinator coord1(CoordinatorEpoch(1));
  SyntheticBackend synth;
  SyntheticSeedResult seed = seed_synthetic(coord1, synth);
  Authority auth;
  auth.epoch = coord1.epoch();

  CreateBufferParams bp;
  bp.domain = MemoryDomain::CUDA_DEVICE;
  bp.size = 1024 * 1024;
  bp.alignment = 64;
  bp.device = seed.device;
  bp.support = Support::SYNTHETIC;
  bp.provenance = Provenance::SYNTHETIC_FIXTURE;
  bp.authority = auth;
  bp.name = "restart_buffer";
  auto buf = coord1.create_buffer(bp);
  assert(buf);

  RegisterParams rp;
  rp.buffer = buf->id;
  rp.endpoint = seed.nic;
  rp.provider = seed.rdma;
  rp.authority = auth;
  rp.support = Support::SYNTHETIC;
  rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  rp.required_capabilities = {"direct_nic"};
  auto reg = coord1.register_buffer(rp, &synth);
  assert(reg);
  assert(reg->state == RegistrationState::REGISTERED);
  std::printf("[cr] epoch1 registered reg gen=%llu state=%s\n",
              (unsigned long long)reg->generation.value(), to_string(reg->state));

  // Persist durable state.
  auto save = coord1.persist_to_file(state);
  assert(save);
  std::printf("[cr] persisted to %s\n", state.c_str());

  // --- Second coordinator incarnation: recover into a fresh process authority ---
  Coordinator coord2(CoordinatorEpoch(2));
  auto rec = Coordinator::recover_from_file(state, coord2);
  assert(rec);
  // Fresh coordinator epoch: the recovered epoch from the file is overwritten by
  // advance_epoch to reflect the new process incarnation.
  auto adv = coord2.advance_epoch();
  assert(adv);
  assert(coord2.epoch().value() != 1);
  std::printf("[cr] epoch2=%llu\n", (unsigned long long)coord2.epoch().value());

  auto regs2 = coord2.registrations();
  assert(regs2);
  bool reval_found = false;
  for (const auto& r : *regs2) {
    if (r.buffer == buf->id && r.endpoint == seed.nic && r.state == RegistrationState::REVALIDATION_REQUIRED) {
      reval_found = true;
    }
  }
  std::printf("[cr] recovered registration requires revalidation=%d\n", reval_found ? 1 : 0);
  assert(reval_found);

  // A fresh direct path built on the recovered buffer must NOT be directly eligible.
  PlanRequestParams pr;
  pr.source = buf->id;
  pr.destination = seed.nic;
  pr.direction = Direction::GPU_TO_PEER;
  pr.policy = PathPolicy::PREFER_DIRECT;
  pr.size = 1024 * 1024;
  auto dec = coord2.evaluate_path(pr);
  assert(dec);
  std::printf("[cr] post-restart path eligibility=%s\n", to_string(dec->eligibility));
  assert(dec->eligibility != PathEligibility::DIRECT_ALLOWED &&
         dec->eligibility != PathEligibility::DIRECT_ALLOWED_DEGRADED);

  // Stale-epoch traffic (authority with epoch 1) must be rejected.
  RegisterParams stale = rp;
  stale.authority = auth;  // auth.epoch == 1 (stale)
  auto stale_reg = coord2.register_buffer(stale, &synth);
  assert(!stale_reg);
  std::printf("[cr] stale-epoch register rejected code=%s\n", to_string(stale_reg.error().code));
  assert(stale_reg.error().code == ErrorCode::STALE_BOOT || stale_reg.error().code == ErrorCode::STALE_EPOCH);

  std::remove(state.c_str());
  std::printf("[cr] COORDINATOR RESTART / RECOVERY PROOF PASSED\n");
  return 0;
}