// Installed-API example: clean UNSUPPORTED handling for GPUDirect RDMA/GDS on this platform.
#include <gpudirectfabric/backends/unsupported_backend.hpp>
#include <gpudirectfabric/coordinator.hpp>
#include <cstdio>
using namespace gpudirectfabric;
int main() {
  auto rdma = make_rdma_backend();
  auto storage = make_storage_backend();
  std::printf("rdma backend support=%s available=%d\n", to_string(rdma->support()), rdma->available() ? 1 : 0);
  std::printf("storage backend support=%s available=%d\n", to_string(storage->support()), storage->available() ? 1 : 0);
  Coordinator c(CoordinatorEpoch(1));
  ProviderAddParams rp; rp.name = "rdma"; rp.available = false; rp.support = Support::UNSUPPORTED;
  rp.provenance = Provenance::RDMA_PROVIDER;
  auto res = c.add_provider(rp);
  std::printf("rdma provider support=%s\n", res ? to_string(res->support) : "none");
  return 0;
}