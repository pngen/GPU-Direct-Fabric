// Installed-API example: stale registration rejection + worker incarnation fencing.
#include <gpudirectfabric/backends/synthetic_backend.hpp>
#include <gpudirectfabric/backends/synthetic_seed.hpp>
#include <gpudirectfabric/coordinator.hpp>
#include <cstdio>
using namespace gpudirectfabric;
int main() {
  SyntheticBackend synth;
  Coordinator c(CoordinatorEpoch(1));
  SyntheticSeedResult seed = seed_synthetic(c, synth);
  Authority wa; wa.worker = WorkerId(1); wa.boot = WorkerBootId(1); wa.epoch = c.epoch();
  c.register_worker(WorkerId(1), WorkerBootId(1), c.epoch());
  EndpointAddParams ep; ep.endpoint_class = EndpointClass::NIC; ep.name = "nic"; ep.healthy = true;
  ep.support = Support::SYNTHETIC; ep.provenance = Provenance::SYNTHETIC_FIXTURE; ep.authority = wa;
  auto nic = c.add_endpoint(ep);
  ProviderAddParams pp; pp.name = "rdma"; pp.available = true; pp.support = Support::SYNTHETIC;
  auto rd = c.add_provider(pp);
  CapabilityPublishParams cp; cp.provider = "rdma"; cp.capability = "direct_nic"; cp.value = "true";
  cp.support = Support::SYNTHETIC; cp.provenance = Provenance::SYNTHETIC_FIXTURE; cp.authority = wa;
  c.publish_capability(cp);
  DeviceAddParams dp; dp.name = "gpu"; dp.support = Support::SYNTHETIC; dp.authority = wa;
  auto dev = c.add_device(dp);
  CreateBufferParams bp; bp.domain = MemoryDomain::CUDA_DEVICE; bp.size = 1u << 20; bp.alignment = 64;
  bp.device = dev->id; bp.support = Support::SYNTHETIC; bp.provenance = Provenance::SYNTHETIC_FIXTURE; bp.authority = wa;
  auto b = c.create_buffer(bp);
  RegisterParams rp; rp.buffer = b->id; rp.endpoint = nic->id; rp.provider = rd->id; rp.authority = wa;
  rp.support = Support::SYNTHETIC; rp.provenance = Provenance::SYNTHETIC_FIXTURE;
  auto r = c.register_buffer(rp, &synth);
  std::printf("registration state=%s\n", to_string(r->state));
  // Worker death => dynamic registration invalidated.
  c.mark_worker_dead(WorkerId(1));
  auto regs = c.registrations();
  std::printf("after worker death state=%s\n", to_string((*regs)[0].state));
  // Stale boot re-register rejected.
  auto stale = c.register_worker(WorkerId(1), WorkerBootId(1), c.epoch());
  std::printf("stale boot rejected? %d code=%s\n", !stale ? 1 : 0, stale.error().code == ErrorCode::STALE_BOOT ? "STALE_BOOT" : "other");
  return 0;
}