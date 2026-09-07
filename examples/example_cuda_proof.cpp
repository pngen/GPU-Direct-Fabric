// Installed-API example: real CUDA device / pinned-host proof (or clean UNSUPPORTED).
#include <gpudirectfabric/backends/cuda_backend.hpp>
#include <cstdio>
using namespace gpudirectfabric;
int main() {
  CudaBackend cuda;
  auto rep = cuda.self_test();
  std::printf("CUDA self-test: support=%s device=%s cc=%s parity=%d baseline=%d\n",
              to_string(rep.support), rep.device_name.c_str(), rep.compute_capability.c_str(),
              rep.cpu_parity ? 1 : 0, rep.baseline_return ? 1 : 0);
  return 0;
}