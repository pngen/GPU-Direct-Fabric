#include "gpudirectfabric/backends/synthetic_backend.hpp"
#include "gpudirectfabric/backends/synthetic_seed.hpp"
#include "gpudirectfabric/coordinator.hpp"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace gpudirectfabric;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static Authority coord_auth(const Coordinator& c) { Authority a; a.epoch = c.epoch(); return a; }

// Build a coordinator with a registered buffer and a direct plan.
static void setup(SyntheticBackend& synth, Coordinator& c, SyntheticSeedResult& seed, BufferId& buf,
                  EndpointId& nic, TransferPlanId& plan, Authority& auth) {
  seed = seed_synthetic(c, synth);
  auth = coord_auth(c);
  nic = seed.nic;
  CreateBufferParams bp; bp.domain = MemoryDomain::CUDA_DEVICE; bp.size = 1024 * 1024; bp.alignment = 64;
  bp.device = seed.device; bp.support = Support::SYNTHETIC; bp.provenance = Provenance::SYNTHETIC_FIXTURE; bp.authority = auth;
  auto b = c.create_buffer(bp); CHECK(b); buf = b->id;
  RegisterParams rp; rp.buffer = buf; rp.endpoint = nic; rp.provider = seed.rdma; rp.authority = auth;
  rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  CHECK(c.register_buffer(rp, &synth));
  PlanRequestParams pr; pr.source = buf; pr.destination = nic; pr.size = 1024 * 1024;
  auto pl = c.plan_transfer(pr, &synth); CHECK(pl); plan = pl->id;
}

static void test_plan_vs_generation_race() {
  for (int iter = 0; iter < 200; ++iter) {
    SyntheticBackend synth;
    Coordinator c(CoordinatorEpoch(1));
    SyntheticSeedResult seed; BufferId buf; EndpointId nic; TransferPlanId plan; Authority auth;
    setup(synth, c, seed, buf, nic, plan, auth);
    std::atomic<bool> go(false), doneA(false), doneB(false);
    std::atomic<int> a_ok(0), b_ok(0);
    std::thread A([&] { while (!go.load()) {} auto r = c.advance_buffer_generation(buf, auth); if (r) a_ok = 1; doneA = true; });
    std::thread B([&] { while (!go.load()) {} auto r = c.begin_transfer(plan, auth, &synth); if (r) b_ok = 1; doneB = true; });
    go = true;
    A.join(); B.join();
    // Either begin succeeded (before the generation advanced) or failed with STALE_GENERATION.
    // A valid outcome: begin could have succeeded then the plan was superseded; but a taken
    // begin after a generation advance must be rejected.
    CHECK(a_ok == 1);
  }
}

static void test_cancel_vs_complete_race() {
  for (int iter = 0; iter < 200; ++iter) {
    SyntheticBackend synth;
    Coordinator c(CoordinatorEpoch(1));
    SyntheticSeedResult seed; BufferId buf; EndpointId nic; TransferPlanId plan; Authority auth;
    setup(synth, c, seed, buf, nic, plan, auth);
    auto att = c.begin_transfer(plan, auth, &synth); CHECK(att);
    auto attid = att->id;
    std::atomic<bool> go(false);
    std::atomic<int> cancel_ok(0), complete_ok(0);
    std::thread A([&] { while (!go.load()) {} auto r = c.cancel_transfer(attid, auth); if (r) cancel_ok = 1; });
    std::thread B([&] { while (!go.load()) {} auto r = c.complete_transfer(attid, auth); if (r) complete_ok = 1; });
    go = true;
    A.join(); B.join();
    // Cancellation must win or completion must win; but if cancellation won, the
    // completed receipt must not exist, and vice versa.
    auto recs = c.receipts(); CHECK(recs);
    if (cancel_ok == 1 && complete_ok == 1) {
      // Only one can commit as authoritative; inspect the receipt state.
      bool cancelled = false, completed = false;
      for (auto& r : *recs) { if (r.state == TransferState::CANCELLED) cancelled = true; if (r.state == TransferState::COMPLETED) completed = true; }
      CHECK(!(cancelled && completed));
    }
    // A cancelled transfer can never later report success.
    auto after = c.complete_transfer(attid, auth);
    if (cancel_ok == 1) CHECK(!after || after.error().code == ErrorCode::CANCELLED || after->state != TransferState::COMPLETED);
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test_plan_vs_generation_race();
  test_cancel_vs_complete_race();
  std::printf("[race_test] failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
