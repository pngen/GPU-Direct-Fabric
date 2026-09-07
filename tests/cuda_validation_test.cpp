#include "gpudirectfabric/backends/cuda_backend.hpp"

#include <cassert>
#include <cstdio>

using namespace gpudirectfabric;

int main() {
  CudaBackend cuda;
  CudaSelfTestReport rep = cuda.self_test();
  std::printf("[cuda] support=%s device=%s cc=%s total=%llu alloc=%d pinned=%d kernel=%d "
              "parity=%d baseline=%d %s\n",
              to_string(rep.support), rep.device_name.c_str(), rep.compute_capability.c_str(),
              (unsigned long long)rep.total_memory, rep.allocation ? 1 : 0, rep.pinned_host ? 1 : 0,
              rep.kernel ? 1 : 0, rep.cpu_parity ? 1 : 0, rep.baseline_return ? 1 : 0, rep.summary.c_str());
  if (!rep.device_discovery) {
    std::printf("[cuda] no CUDA device available; classified UNSUPPORTED\n");
    return 0;  // environment has no CUDA; not a failure
  }
  assert(rep.allocation);
  assert(rep.pinned_host);
  assert(rep.kernel);
  assert(rep.cpu_parity);
  assert(rep.baseline_return);
  assert(rep.support == Support::REAL);
  std::printf("[cuda] REAL CUDA validation PASSED\n");
  return 0;
}
