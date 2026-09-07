#include "gpudirectfabric/backends/cuda_backend.hpp"
#include "gpudirectfabric/backends/synthetic_backend.hpp"
#include "gpudirectfabric/backends/synthetic_seed.hpp"
#include "gpudirectfabric/backends/unsupported_backend.hpp"
#include "gpudirectfabric/coordinator.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace gpudirectfabric;

static bool json = false;

static void seed(Coordinator& c) {
  SyntheticBackend synth;
  seed_synthetic(c, synth);
  CudaBackend cuda;
  auto devs = cuda.devices();
  for (const auto& d : devs) {
    DeviceAddParams dp; dp.name = d.name; dp.compute_capability = d.compute_capability;
    dp.peer_memory_supported = d.peer_memory_supported; dp.support = d.support; dp.provenance = d.provenance;
    c.add_device(dp);
  }
  ProviderAddParams pp; pp.name = "cuda"; pp.available = cuda.available(); pp.support = cuda.support();
  pp.provenance = cuda.provenance(); c.add_provider(pp);
  // Honestly report the missing RDMA/storage direct providers.
  ProviderAddParams rp; rp.name = "rdma"; rp.available = false; rp.support = Support::UNSUPPORTED;
  rp.provenance = Provenance::RDMA_PROVIDER; c.add_provider(rp);
  ProviderAddParams sp; sp.name = "storage"; sp.available = false; sp.support = Support::UNSUPPORTED;
  sp.provenance = Provenance::STORAGE_BACKEND; c.add_provider(sp);
}


int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string mode = "list-devices";
  std::string state;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--json") json = true;
    else if (a == "--state") { if (i + 1 < argc) state = argv[++i]; }
    else if (a.rfind("--", 0) != 0) mode = a;
  }
  Coordinator coord(CoordinatorEpoch(1));
  if (!state.empty()) {
    auto rec = Coordinator::recover_from_file(state, coord);
    if (!rec) { std::printf("state recovery failed: %s\n", rec.error().message.c_str()); return 2; }
  } else {
    seed(coord);
  }

  if (mode == "list-devices") {
    if (json) std::printf("{");
    auto d = coord.devices();
    if (json) std::printf("\"devices\":[");
    if (d) for (std::size_t i = 0; i < d->size(); ++i) {
      if (json) std::printf("%s{\"id\":%llu,\"name\":\"%s\",\"cc\":\"%s\",\"support\":\"%s\"}",
        i ? "," : "", (unsigned long long)(*d)[i].id.value(), (*d)[i].name.c_str(),
        (*d)[i].compute_capability.c_str(), to_string((*d)[i].support));
    } else {
      for (const auto& x : *d) std::printf("device %llu: %s cc=%s support=%s\n",
        (unsigned long long)x.id.value(), x.name.c_str(), x.compute_capability.c_str(), to_string(x.support));
    }
    if (json) std::printf("]}\n");
    return 0;
  }
  if (mode == "providers") {
    if (json) std::printf("{\"providers\":[");
    auto p = coord.providers();
    if (p) for (std::size_t i = 0; i < p->size(); ++i) {
      if (json) std::printf("%s{\"name\":\"%s\",\"available\":%d,\"support\":\"%s\"}", i ? "," : "",
        (*p)[i].name.c_str(), (*p)[i].available ? 1 : 0, to_string((*p)[i].support));
    } else { for (const auto& x : *p) std::printf("provider %s avail=%d support=%s\n", x.name.c_str(), x.available ? 1 : 0, to_string(x.support)); }
    if (json) std::printf("]}\n");
    return 0;
  }
  if (mode == "buffers") {
    auto b = coord.buffers();
    for (const auto& x : *b) std::printf("buffer %llu gen=%llu domain=%s reg=%s support=%s\n",
      (unsigned long long)x.id.value(), (unsigned long long)x.generation.value(), to_string(x.domain),
      to_string(x.registration_state), to_string(x.support));
    return 0;
  }
  if (mode == "capabilities") {
    auto c = coord.capabilities();
    for (const auto& x : *c) std::printf("cap %s::%s=%s support=%s\n", x.provider.c_str(), x.capability.c_str(),
      x.value.c_str(), to_string(x.support));
    return 0;
  }
  if (mode == "registrations") {
    auto r = coord.registrations();
    for (const auto& x : *r) std::printf("reg %llu gen=%llu buffer=%llu endpoint=%llu state=%s support=%s\n",
      (unsigned long long)x.id.value(), (unsigned long long)x.generation.value(), (unsigned long long)x.buffer.value(),
      (unsigned long long)x.endpoint.value(), to_string(x.state), to_string(x.support));
    return 0;
  }
  if (mode == "cuda-self-test") {
    CudaBackend cuda;
    auto rep = cuda.self_test();
    std::printf("CUDA: support=%s device=%s cc=%s alloc=%d pinned=%d kernel=%d parity=%d baseline=%d %s\n",
      to_string(rep.support), rep.device_name.c_str(), rep.compute_capability.c_str(),
      rep.allocation ? 1 : 0, rep.pinned_host ? 1 : 0, rep.kernel ? 1 : 0, rep.cpu_parity ? 1 : 0,
      rep.baseline_return ? 1 : 0, rep.summary.c_str());
    return 0;
  }
  if (mode == "query-eligibility") {
    // Requires --source and --destination ids.
    std::uint64_t src = 0, dst = 0; std::uint64_t size = 4096;
    for (int i = 1; i < argc; ++i) { if (std::strcmp(argv[i], "--source") == 0 && i+1 < argc) src = std::strtoull(argv[++i], nullptr, 10);
      if (std::strcmp(argv[i], "--destination") == 0 && i+1 < argc) dst = std::strtoull(argv[++i], nullptr, 10);
      if (std::strcmp(argv[i], "--size") == 0 && i+1 < argc) size = std::strtoull(argv[++i], nullptr, 10); }
    PlanRequestParams pr; pr.source = BufferId(src); pr.destination = EndpointId(dst); pr.size = size;
    auto d = coord.evaluate_path(pr);
    if (!d) { std::printf("eligibility error: %s\n", d.error().message.c_str()); return 1; }
    std::printf("direct=%s support=%s\n", to_string(d->direct_eligibility), to_string(d->support));
    for (const auto& v : d->violations) std::printf("  req %s => %s%s\n", v.requirement.c_str(), v.satisfied ? "OK" : "FAIL", v.detail.empty() ? "" : (": " + v.detail).c_str());
    if (d->fallback_available) std::printf("fallback: %s (copies=%u)\n", d->selected_fallback ? to_string(d->selected_fallback->path_class) : "?", d->selected_fallback ? d->selected_fallback->stage_count : 0);
    return 0;
  }
  if (mode == "stale") {
    auto r = coord.registrations();
    int stale = 0;
    for (const auto& x : *r) if (x.state == RegistrationState::REVALIDATION_REQUIRED || x.state == RegistrationState::FAILED || x.state == RegistrationState::RETIRED) ++stale;
    std::printf("stale/revalidation registrations: %d / %llu\n", stale, (unsigned long long)r->size());
    auto lw = coord.live_workers();
    std::printf("live workers: %llu\n", (unsigned long long)(lw ? lw->size() : 0));
    return 0;
  }
  std::printf("subcommands: list-devices providers buffers capabilities registrations cuda-self-test query-eligibility stale\n");
  return 0;
}