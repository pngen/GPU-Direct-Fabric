// Installed-API example: direct path decision + fallback.
#include <gpudirectfabric/backends/synthetic_backend.hpp>
#include <gpudirectfabric/backends/synthetic_seed.hpp>
#include <gpudirectfabric/coordinator.hpp>
#include <cstdio>
using namespace gpudirectfabric;
int main() {
  SyntheticBackend synth;
  Coordinator c(CoordinatorEpoch(1));
  SyntheticSeedResult seed = seed_synthetic(c, synth);
  Authority auth; auth.epoch = c.epoch();
  CreateBufferParams bp; bp.domain = MemoryDomain::CUDA_DEVICE; bp.size = 1u << 20; bp.alignment = 64;
  bp.device = seed.device; bp.support = Support::SYNTHETIC; bp.provenance = Provenance::SYNTHETIC_FIXTURE; bp.authority = auth;
  auto b = c.create_buffer(bp);
  RegisterParams rp; rp.buffer = b->id; rp.endpoint = seed.nic; rp.provider = seed.rdma; rp.authority = auth;
  rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  auto r = c.register_buffer(rp, &synth);
  PlanRequestParams pr; pr.source = b->id; pr.destination = seed.nic; pr.size = 1u << 20;
  auto d = c.evaluate_path(pr);
  std::printf("direct eligibility=%s support=%s fallback=%d\n", to_string(d->eligibility), to_string(d->support), d->fallback_available ? 1 : 0);
  // DIRECT_ONLY never stages.
  pr.policy = PathPolicy::DIRECT_ONLY;
  auto d2 = c.evaluate_path(pr);
  std::printf("DIRECT_ONLY eligibility=%s fallback_available=%d fallback_disallowed=%d\n",
              to_string(d2->eligibility), d2->fallback_available ? 1 : 0, d2->fallback_disallowed ? 1 : 0);
  return 0;
}