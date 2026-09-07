// Control-plane microbenchmarks. Measures completed work (not enqueue).
#include <gpudirectfabric/backends/synthetic_backend.hpp>
#include <gpudirectfabric/backends/synthetic_seed.hpp>
#include <gpudirectfabric/coordinator.hpp>
#include <chrono>
#include <cstdio>
#include <vector>
using namespace gpudirectfabric;
using Clock = std::chrono::steady_clock;
int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  SyntheticBackend synth;
  Coordinator c(CoordinatorEpoch(1));
  SyntheticSeedResult seed = seed_synthetic(c, synth);
  Authority auth; auth.epoch = c.epoch();
  const int N = 1000;
  std::vector<BufferId> bufs;
  for (int i = 0; i < N; ++i) {
    CreateBufferParams bp; bp.domain = MemoryDomain::CUDA_DEVICE; bp.size = 1u << 20; bp.alignment = 64;
    bp.device = seed.device; bp.support = Support::SYNTHETIC; bp.provenance = Provenance::SYNTHETIC_FIXTURE; bp.authority = auth;
    auto b = c.create_buffer(bp); bufs.push_back(b->id);
  }
  auto t0 = Clock::now();
  for (int i = 0; i < N; ++i) { RegisterParams rp; rp.buffer = bufs[(std::size_t)i]; rp.endpoint = seed.nic; rp.provider = seed.rdma; rp.authority = auth; rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE; auto r = c.register_buffer(rp, &synth); (void)r; }
  auto t1 = Clock::now();
  auto reg_ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  // Eligibility evaluation.
  t0 = Clock::now();
  PlanRequestParams pr; pr.source = bufs[0]; pr.destination = seed.nic; pr.size = 1u << 20;
  for (int i = 0; i < N; ++i) { auto d = c.evaluate_path(pr); (void)d; }
  t1 = Clock::now();
  auto el_ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  // Plan creation.
  t0 = Clock::now();
  for (int i = 0; i < N; ++i) { auto p = c.plan_transfer(pr, &synth); (void)p; }
  t1 = Clock::now();
  auto plan_ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  std::printf("bench: register=%lldus/eval=%lldus/plan=%lldus for N=%d (completed work)\n",
              (long long)(reg_ms / N), (long long)(el_ms / N), (long long)(plan_ms / N), N);
  return 0;
}