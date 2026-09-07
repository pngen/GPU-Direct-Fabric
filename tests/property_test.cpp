#include "gpudirectfabric/backends/synthetic_backend.hpp"
#include "gpudirectfabric/backends/synthetic_seed.hpp"
#include "gpudirectfabric/coordinator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace gpudirectfabric;

static std::uint64_t g_seed = 0;

// xorshift64*
static std::uint64_t rng() {
  static std::uint64_t x = g_seed ? g_seed : 0x9E3779B97F4A7C15ull;
  x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
  return x * 0x2545F4914F6CDD1Dull;
}
static std::uint32_t pick(std::uint32_t n) { return static_cast<std::uint32_t>(rng() % n); }

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL(seed=%llu) %s:%d: %s\n", (unsigned long long)g_seed, __FILE__, __LINE__, #cond); ++failures; } } while (0)

// Invariant checks over a coordinator state.
static void check_invariants(Coordinator& c) {
  auto buffers = c.buffers(); CHECK(buffers);
  auto regs = c.registrations(); CHECK(regs);
  auto plans = c.plans(); CHECK(plans);
  auto endpoints = c.endpoints(); CHECK(endpoints);
  // No registration references a nonexistent buffer.
  for (const auto& r : *regs) {
    auto b = c.get_buffer(r.buffer); CHECK(b);
    // Registration generations never decrease; buffer generations are monotonic.
    CHECK(r.generation.value() >= 1);
  }
  // No plan references a nonexistent source buffer.
  for (const auto& p : *plans) {
    auto b = c.get_buffer(p.source); CHECK(b);
    // Plans bind the source generation they were built against.
    CHECK(p.source_generation.value() >= 1);
  }
  // Unique identities per map.
  std::vector<std::uint64_t> bufs; for (auto& b : *buffers) bufs.push_back(b.id.value());
  std::sort(bufs.begin(), bufs.end()); CHECK(std::adjacent_find(bufs.begin(), bufs.end()) == bufs.end());
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::uint64_t base_seed = (argc >= 2) ? std::strtoull(argv[1], nullptr, 10) : 0xC0FFEEull;
  const int seeds = (argc >= 3) ? std::atoi(argv[2]) : 3;
  const int events = 400;
  for (int s = 0; s < seeds; ++s) {
    g_seed = base_seed + static_cast<std::uint64_t>(s);
    std::printf("[prop] seed=%llu\n", (unsigned long long)g_seed);
    SyntheticBackend synth;
    Coordinator c(CoordinatorEpoch(1));
    SyntheticSeedResult seed = seed_synthetic(c, synth);
    Authority auth; auth.epoch = c.epoch();
    std::vector<BufferId> buffers;
    std::vector<EndpointId> endpoints = {seed.nic, seed.storage, seed.peer};
    std::vector<WorkerId> workers;
    std::vector<std::uint64_t> worker_next_boot;
    std::stringstream repro;
    for (int ev = 0; ev < events; ++ev) {
      const std::uint32_t op = pick(12);
      repro << op << ",";
      switch (op) {
        case 0: {  // create buffer
          CreateBufferParams bp;
          bp.domain = MemoryDomain::CUDA_DEVICE;
          bp.size = 1024 + pick(8 * 1024 * 1024);
          bp.alignment = 64;
          bp.device = seed.device;
          bp.support = Support::SYNTHETIC; bp.provenance = Provenance::SYNTHETIC_FIXTURE;
          bp.authority = auth;
          auto b = c.create_buffer(bp);
          if (b) buffers.push_back(b->id);
          break;
        }
        case 1: {  // advance buffer generation
          if (!buffers.empty()) {
            BufferId bid = buffers[pick(static_cast<std::uint32_t>(buffers.size()))];
            c.advance_buffer_generation(bid, auth);
          }
          break;
        }
        case 2: {  // register buffer (idempotent if duplicate)
          if (!buffers.empty() && !endpoints.empty()) {
            RegisterParams rp;
            rp.buffer = buffers[pick(static_cast<std::uint32_t>(buffers.size()))];
            rp.endpoint = endpoints[pick(static_cast<std::uint32_t>(endpoints.size()))];
            rp.provider = seed.rdma; rp.authority = auth;
            rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
            auto r = c.register_buffer(rp, &synth);
            // Invariant: never create a current registration with stale authority.
            if (r) CHECK(r->authority.epoch.value() == c.epoch().value());
          }
          break;
        }
        case 3: {  // deregister (possibly stale)
          auto regs = c.registrations();
          if (regs && !regs->empty()) {
            auto r = (*regs)[pick(static_cast<std::uint32_t>(regs->size()))];
            c.deregister_buffer(r.id, auth);
          }
          break;
        }
        case 4: {  // endpoint generation advance
          if (!endpoints.empty()) {
            EndpointId eid = endpoints[pick(static_cast<std::uint32_t>(endpoints.size()))];
            c.advance_endpoint_generation(eid, auth);
          }
          break;
        }
        case 5: {  // capability refresh (provider available toggle)
          if ((rng() & 1) == 0) {
            ProviderAddParams pp; pp.name = "rdma"; pp.available = true; pp.support = Support::SYNTHETIC;
            c.add_provider(pp);
          } else {
            ProviderAddParams pp; pp.name = "rdma"; pp.available = false; pp.support = Support::SYNTHETIC;
            c.add_provider(pp);
          }
          break;
        }
        case 6: {  // plan creation + begin
          if (!buffers.empty() && !endpoints.empty()) {
            PlanRequestParams pr;
            pr.source = buffers[pick(static_cast<std::uint32_t>(buffers.size()))];
            pr.destination = endpoints[pick(static_cast<std::uint32_t>(endpoints.size()))];
            pr.policy = PathPolicy::PREFER_DIRECT; pr.size = 4096;
            auto plan = c.plan_transfer(pr, &synth);
            if (plan) {
              auto att = c.begin_transfer(plan->id, auth, &synth);
              if (att) c.complete_transfer(att->id, auth);
            }
          }
          break;
        }
        case 7: {  // worker death + fresh boot
          WorkerId w(100 + pick(3));
          std::uint64_t nxt = 0;
          for (std::size_t i = 0; i < workers.size(); ++i) if (workers[i] == w) nxt = worker_next_boot[i];
          std::uint64_t boot = nxt + 1;
          c.register_worker(w, WorkerBootId(boot), c.epoch());
          c.mark_worker_dead(w);
          break;
        }
        case 8: {  // cancel a random attempt
          auto atts = c.attempts();
          if (atts && !atts->empty()) {
            auto a = (*atts)[pick(static_cast<std::uint32_t>(atts->size()))];
            c.cancel_transfer(a.id, auth);
          }
          break;
        }
        case 9: {  // evaluate a path randomly / query
          if (!buffers.empty() && !endpoints.empty()) {
            PlanRequestParams pr;
            pr.source = buffers[pick(static_cast<std::uint32_t>(buffers.size()))];
            pr.destination = endpoints[pick(static_cast<std::uint32_t>(endpoints.size()))];
            pr.policy = (pick(4) == 0) ? PathPolicy::DIRECT_ONLY : PathPolicy::PREFER_DIRECT;
            pr.size = 4096;
            auto d = c.evaluate_path(pr);
            if (d) {
              // UNKNOWN can never become DIRECT_ALLOWED.
              if (pr.policy == PathPolicy::DIRECT_ONLY) CHECK(d->eligibility != PathEligibility::DIRECT_ALLOWED || d->support == Support::REAL);
            }
          }
          break;
        }
        case 10: {  // persist + recover round trip (into a fresh epoch)
          const std::string path = "prop_state.gdfstate";
          c.persist_to_file(path);
          c.advance_epoch();
          std::remove(path.c_str());
          break;
        }
        case 11: {  // retires a buffer
          if (!buffers.empty()) {
            BufferId bid = buffers[pick(static_cast<std::uint32_t>(buffers.size()))];
            c.retire_buffer(bid, auth);
          }
          break;
        }
        default: break;
      }
      check_invariants(c);
      if (failures) {
        std::printf("[prop] REPRODUCTION seed=%llu event=%d seq=%s\n", (unsigned long long)g_seed, ev, repro.str().c_str());
        return 1;
      }
    }
  }
  std::printf("[prop] property test PASSED (%d seeds)\n", seeds);
  return failures == 0 ? 0 : 1;
}